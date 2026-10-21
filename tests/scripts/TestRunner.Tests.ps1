[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
Import-Module Pester -RequiredVersion 3.4.0
. (Join-Path $PSScriptRoot "pester-summary.ps1")

function New-FixtureRunner {
  param(
    [string]$Root,
    [switch]$IncludeDistributionNotices
  )

  $scripts = Join-Path $Root "scripts"
  $repository = Join-Path $Root "repository"
  $buildX64 = Join-Path $Root "build-x64"
  $buildX86 = Join-Path $Root "build-x86"
  $legacy = Join-Path $Root "legacy"
  foreach ($directory in @($scripts, $repository, $buildX64, $buildX86, $legacy)) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
  }

  Copy-Item -LiteralPath (Join-Path $PSScriptRoot "Run-InfrastructureTests.ps1") -Destination $scripts
  Copy-Item -LiteralPath (Join-Path $PSScriptRoot "pester-summary.ps1") -Destination $scripts

  # Stand-in suites keep the fixture invocation away from the real compiler and CLI work while
  # still exercising the runner's own registration and required-file checks.
  $stub = @'
param(
  [string]$RepositoryRoot,
  [string]$LegacyPackageInputDir,
  [string]$BuildDir,
  [string]$Configuration,
  [string]$SourceRoot,
  [string]$BuildDirX64,
  [string]$BuildDirX86,
  [string]$SelectedToolchain
)
Describe "fixture suite" {
  It "passes" {
    $true | Should Be $true
  }
}
'@
  $suites = @(
    "ImportPolicy.Tests.ps1",
    "CTest.ImportPolicy.Tests.ps1",
    "BuildAll.ImportPolicy.Tests.ps1",
    "Pack.LegacyManifest.Tests.ps1",
    "Delivery.Manifest.Tests.ps1",
    "Delivery.SvgPackage.Tests.ps1",
    "Delivery.RustPin.Tests.ps1",
    "Delivery.YyLink.Tests.ps1",
    "Pack.ExtraDocuments.Tests.ps1",
    "ExtraDocumentValidation.Tests.ps1",
    "Delivery.CiWiring.Tests.ps1",
    "TestRunner.Tests.ps1"
  )
  if ($IncludeDistributionNotices) {
    $suites += "DistributionNotices.Tests.ps1"
  }
  foreach ($suite in $suites) {
    [IO.File]::WriteAllText(
      (Join-Path $scripts $suite),
      $stub,
      [Text.UTF8Encoding]::new($false))
  }

  return [pscustomobject]@{
    Runner = Join-Path $scripts "Run-InfrastructureTests.ps1"
    Scripts = $scripts
    RepositoryRoot = $repository
    BuildDirX64 = $buildX64
    BuildDirX86 = $buildX86
    LegacyPackageInputDir = $legacy
  }
}

function Set-FixtureSuite {
  param(
    [object]$Fixture,
    [string]$Name,
    [string]$Content
  )

  [IO.File]::WriteAllText(
    (Join-Path $Fixture.Scripts $Name),
    $Content,
    [Text.UTF8Encoding]::new($false))
}

function Invoke-FixtureRunner {
  param([object]$Fixture)

  $previousErrorAction = $ErrorActionPreference
  try {
    # Merge native stderr as text without letting the expected failure terminate the assertion.
    $ErrorActionPreference = "Continue"
    $output = & powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File `
      $Fixture.Runner `
      -RepositoryRoot $Fixture.RepositoryRoot `
      -BuildDirX64 $Fixture.BuildDirX64 `
      -BuildDirX86 $Fixture.BuildDirX86 `
      -LegacyPackageInputDir $Fixture.LegacyPackageInputDir 2>&1 | Out-String
    $exitCode = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $previousErrorAction
  }
  return [pscustomobject]@{
    ExitCode = $exitCode
    Output = $output
  }
}

function Invoke-GitHubShapeFixtureRunner {
  param([object]$Fixture)

  $wrapper = Join-Path $Fixture.Scripts "GitHubShapeWrapper.ps1"
  [IO.File]::WriteAllText(
    $wrapper,
    @'
param(
  [Parameter(Mandatory = $true)]
  [string]$Runner,

  [Parameter(Mandatory = $true)]
  [string]$RepositoryRoot,

  [Parameter(Mandatory = $true)]
  [string]$BuildDirX64,

  [Parameter(Mandatory = $true)]
  [string]$BuildDirX86,

  [Parameter(Mandatory = $true)]
  [string]$LegacyPackageInputDir
)

$ErrorActionPreference = "Stop"
. $Runner `
  -RepositoryRoot $RepositoryRoot `
  -BuildDirX64 $BuildDirX64 `
  -BuildDirX86 $BuildDirX86 `
  -LegacyPackageInputDir $LegacyPackageInputDir
if (Test-Path variable:\LASTEXITCODE) {
  exit $LASTEXITCODE
}
'@,
    [Text.UTF8Encoding]::new($false))

  $previousErrorAction = $ErrorActionPreference
  try {
    $ErrorActionPreference = "Continue"
    $output = & powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $wrapper `
      -Runner $Fixture.Runner `
      -RepositoryRoot $Fixture.RepositoryRoot `
      -BuildDirX64 $Fixture.BuildDirX64 `
      -BuildDirX86 $Fixture.BuildDirX86 `
      -LegacyPackageInputDir $Fixture.LegacyPackageInputDir 2>&1 | Out-String
    $exitCode = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $previousErrorAction
  }
  return [pscustomobject]@{
    ExitCode = $exitCode
    Output = $output
  }
}

Describe "Pester 3.4 infrastructure runner summary" {
  It "counts a real inconclusive result from TestResult.Result" {
    $fixture = Join-Path $TestDrive "Inconclusive.Tests.ps1"
    [IO.File]::WriteAllText(
      $fixture,
      @'
Describe "fixture" {
  It "is inconclusive" {
    Set-TestInconclusive "control"
  }
}
'@,
      [Text.UTF8Encoding]::new($false))

    $escapedFixture = $fixture.Replace("'", "''")
    $probe = @"
Import-Module Pester -RequiredVersion 3.4.0
`$result = Invoke-Pester -Script '$escapedFixture' -PassThru -Quiet
`$result.TestResult | Select-Object Name, Result | ConvertTo-Json -Compress
"@
    $json = & powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command $probe
    $summary = Get-PesterSummary -Result ([pscustomobject]@{
      TestResult = @($json | ConvertFrom-Json)
    })

    $summary.Total | Should Be 1
    $summary.Passed | Should Be 0
    $summary.Failed | Should Be 0
    $summary.Inconclusive | Should Be 1
    (Test-PesterSummarySuccessful -Summary $summary) | Should Be $false
  }

  It "rejects an empty suite even when it reports no failures" {
    $summary = Get-PesterSummary -Result ([pscustomobject]@{ TestResult = @() })
    $summary.Total | Should Be 0
    (Test-PesterSummarySuccessful -Summary $summary) | Should Be $false
  }

  It "rejects skipped, pending and unknown outcomes" {
    $summary = Get-PesterSummary -Result ([pscustomobject]@{
        TestResult = @(
          [pscustomobject]@{ Result = "Passed" },
          [pscustomobject]@{ Result = "Ignored" },
          [pscustomobject]@{ Result = "Pending" },
          [pscustomobject]@{ Result = "Mystery" }
        )
      })
    $summary.Skipped | Should Be 1
    $summary.Pending | Should Be 1
    $summary.Unknown | Should Be 1
    (Test-PesterSummarySuccessful -Summary $summary) | Should Be $false
  }
}

Describe "infrastructure runner suite registration" {
  It "fails closed when the distribution notice suite is absent" {
    $fixture = New-FixtureRunner (Join-Path $TestDrive "missing-notices")
    $result = Invoke-FixtureRunner $fixture

    $result.ExitCode | Should Not Be 0
    $result.Output | Should Match "Required Pester suite 'DistributionNotices\.Tests\.ps1' does not exist"
  }

  It "runs the distribution notice suite when it is present" {
    $fixture = New-FixtureRunner (Join-Path $TestDrive "all-suites") -IncludeDistributionNotices
    $result = Invoke-FixtureRunner $fixture

    $result.ExitCode | Should Be 0
    $result.Output | Should Match "distribution-notices"
  }

  It "returns zero through the GitHub-shaped wrapper after a passing test leaves a native exit code" {
    $fixture = New-FixtureRunner (Join-Path $TestDrive "github-shape-native-code") -IncludeDistributionNotices
    Set-FixtureSuite -Fixture $fixture -Name "DistributionNotices.Tests.ps1" -Content @'
param(
  [string]$RepositoryRoot
)
Describe "fixture suite" {
  It "passes after a native command reports failure" {
    & cmd.exe /d /c "exit /b 128"
    $LASTEXITCODE | Should Be 128
  }
}
'@

    $result = Invoke-GitHubShapeFixtureRunner $fixture

    $passingSuiteRows = @(
      $result.Output -split "`r?`n" |
        Where-Object { $_ -match '^\s*\S+\s+1\s+1\s+0\s+0\s+0\s+0\s+0\s*$' }
    )
    $passingSuiteRows.Count | Should Be 13
    $result.Output | Should Match "distribution-notices"
    $result.ExitCode | Should Be 0
  }

  It "keeps a genuinely failing suite nonzero through the GitHub-shaped wrapper" {
    $fixture = New-FixtureRunner (Join-Path $TestDrive "github-shape-failing-suite") -IncludeDistributionNotices
    Set-FixtureSuite -Fixture $fixture -Name "DistributionNotices.Tests.ps1" -Content @'
param()
Describe "fixture suite" {
  It "fails" {
    $false | Should Be $true
  }
}
'@

    $result = Invoke-GitHubShapeFixtureRunner $fixture

    $result.ExitCode | Should Not Be 0
    $result.Output | Should Match "Pester suites failed"
  }
}
