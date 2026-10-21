[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$SourceRoot
)

$ErrorActionPreference = "Stop"

function Get-ReleaseWorkflowPushTagList {
  param(
    [Parameter(Mandatory = $true)][string]$WorkflowPath
  )

  # Anchored to the active top-level on.push.tags block; comments and other lists cannot match.
  $workflow = Get-Content -LiteralPath $WorkflowPath -Raw
  $on = [regex]::Match($workflow, "(?ms)^on:\r?\n(?<body>.*?)(?=^\S)")
  $push = if ($on.Success) {
    [regex]::Match($on.Groups["body"].Value, "(?ms)^  push:\r?\n(?<body>.*?)(?=^  \S|\z)")
  }
  $tags = if ($push -and $push.Success) {
    [regex]::Match($push.Groups["body"].Value, "(?ms)^    tags:\r?\n(?<body>.*?)(?=^    \S|\z)")
  }
  if (-not $tags -or -not $tags.Success) {
    return @()
  }
  return ,@([regex]::Matches($tags.Groups["body"].Value, "(?m)^      - '(?<tag>[^']+)'\s*$") |
      ForEach-Object { $_.Groups["tag"].Value })
}

function Get-MetadataUploadPathList {
  param(
    [Parameter(Mandatory = $true)][string]$WorkflowPath
  )

  # Anchored to the active metadata step, then to its block-scalar path list; other upload
  # steps and commented-out entries cannot match.
  $workflow = Get-Content -LiteralPath $WorkflowPath -Raw
  $step = [regex]::Match(
    $workflow,
    "(?ms)^[ \t]*- name: Upload build metadata for script regression\r?\n(?<body>.*?)(?=^[ \t]*- name:|\z)")
  if (-not $step.Success) {
    throw "The workflow has no active 'Upload build metadata for script regression' step"
  }
  $path = [regex]::Match(
    $step.Groups["body"].Value,
    "(?ms)^[ \t]+path: \|\r?\n(?<body>(?:^[ \t]+\S.*(?:\r?\n|$))*)")
  if (-not $path.Success) {
    throw "The metadata upload step has no block-scalar path list"
  }
  return @([regex]::Matches($path.Groups["body"].Value, "(?m)^[ \t]+(?<path>[^\r\n]+?)[ \t]*\r?$") |
      ForEach-Object { $_.Groups["path"].Value })
}

Describe "CI Rust provisioning" {
  It "provisions the reviewed Rust release in the common composite action" {
    $actionPath = Join-Path $SourceRoot ".github/actions/toolchain/action.yml"
    $action = Get-Content -LiteralPath $actionPath -Raw

    $action | Should Match "rustup toolchain install 1\.99\.1"
    $action | Should Match "--target x86_64-pc-windows-msvc"
    $action | Should Match "--target i686-pc-windows-msvc"
    $action | Should Match "rustup run 1\.99\.1 rustc -vV"
    $action | Should Match ([regex]::Escape('release: 1\.99\.1'))
  }

  It "keeps the pin out of the reusable build workflow" {
    $build = Get-Content -LiteralPath (Join-Path $SourceRoot ".github/workflows/build.yml") -Raw
    ($build -match "rustup toolchain install") | Should Be $false
  }

  It "routes every CI job that configures CMake through the common toolchain action" {
    $ci = Get-Content -LiteralPath (Join-Path $SourceRoot ".github/workflows/ci.yml") -Raw
    $uses = [regex]::Matches($ci, "uses:\s*\./repo/\.github/actions/toolchain").Count
    $uses | Should Be 3

    $build = Get-Content -LiteralPath (Join-Path $SourceRoot ".github/workflows/build.yml") -Raw
    ([regex]::Matches($build, "uses:\s*\./repo/\.github/actions/toolchain").Count) | Should Be 1
  }
}

Describe "CI build metadata upload" {
  It "uploads the Ninja rules file the script-regression compdb graph needs" {
    $workflowPath = Join-Path $SourceRoot ".github/workflows/build.yml"
    $paths = @(Get-MetadataUploadPathList -WorkflowPath $workflowPath)
    $expected = @(
      'repo/build/${{ env.PRESET }}/build.ninja'
      'repo/build/${{ env.PRESET }}/CMakeCache.txt'
      'repo/build/${{ env.PRESET }}/**/CTestTestfile.cmake'
      'repo/build/${{ env.PRESET }}/CMakeFiles/rules.ninja'
    )
    $paths.Count | Should Be $expected.Count
    foreach ($entry in $expected) {
      ($paths -ccontains $entry) | Should Be $true
    }
  }
}

Describe "Release workflow tag triggers" {
  It "keeps the three existing plugin release tags" {
    $tags = Get-ReleaseWorkflowPushTagList -WorkflowPath (Join-Path $SourceRoot ".github/workflows/release.yml")
    foreach ($tag in @("avif/v*", "exr/v*", "rpgmvp/v*")) {
      ($tags -contains $tag) | Should Be $true
    }
  }

  It "includes the svg release tag in the active push triggers" {
    $tags = Get-ReleaseWorkflowPushTagList -WorkflowPath (Join-Path $SourceRoot ".github/workflows/release.yml")
    ($tags -contains "svg/v*") | Should Be $true
  }
}
