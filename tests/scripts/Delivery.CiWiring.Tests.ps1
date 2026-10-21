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
