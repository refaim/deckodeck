[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$RepositoryRoot,

  [Parameter(Mandatory = $true)]
  [string]$BuildDirX64,

  [Parameter(Mandatory = $true)]
  [string]$BuildDirX86,

  [Parameter(Mandatory = $true)]
  [string]$LegacyPackageInputDir,

  [string]$SelectedToolchain = "1.99.1",

  # Optional directory for Pester's NUnit XML files.
  [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
Import-Module Pester -RequiredVersion 3.4.0
. (Join-Path $PSScriptRoot "pester-summary.ps1")

$RepositoryRoot = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$BuildDirX64 = (Resolve-Path -LiteralPath $BuildDirX64).Path
$BuildDirX86 = (Resolve-Path -LiteralPath $BuildDirX86).Path
$LegacyPackageInputDir = (Resolve-Path -LiteralPath $LegacyPackageInputDir).Path
if ($OutputDirectory) {
  New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
  $OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
}

$specs = @(
  @{ Name = "import-policy"; File = "ImportPolicy.Tests.ps1"; Parameters = @{} },
  @{ Name = "ctest"; File = "CTest.ImportPolicy.Tests.ps1"; Parameters = @{ BuildDir = $BuildDirX64; Configuration = "Release" } },
  @{ Name = "build-all"; File = "BuildAll.ImportPolicy.Tests.ps1"; Parameters = @{ RepositoryRoot = $RepositoryRoot; LegacyPackageInputDir = $LegacyPackageInputDir } },
  @{ Name = "pack"; File = "Pack.LegacyManifest.Tests.ps1"; Parameters = @{ RepositoryRoot = $RepositoryRoot; LegacyPackageInputDir = $LegacyPackageInputDir } },
  @{ Name = "manifest"; File = "Delivery.Manifest.Tests.ps1"; Parameters = @{ SourceRoot = $RepositoryRoot; BuildDir = $BuildDirX64 } },
  @{ Name = "svg-package"; File = "Delivery.SvgPackage.Tests.ps1"; Parameters = @{ SourceRoot = $RepositoryRoot; BuildDir = $BuildDirX64 } },
  @{ Name = "rust-pin"; File = "Delivery.RustPin.Tests.ps1"; Parameters = @{ BuildDir = $BuildDirX64; SelectedToolchain = $SelectedToolchain } },
  @{ Name = "yy-link"; File = "Delivery.YyLink.Tests.ps1"; Parameters = @{ BuildDirX64 = $BuildDirX64; BuildDirX86 = $BuildDirX86 } },
  @{ Name = "extra-documents"; File = "Pack.ExtraDocuments.Tests.ps1"; Parameters = @{ RepositoryRoot = $RepositoryRoot; SourceRoot = $RepositoryRoot; LegacyPackageInputDir = $LegacyPackageInputDir } },
  @{ Name = "extra-leaf-policy"; File = "ExtraDocumentValidation.Tests.ps1"; Parameters = @{ SourceRoot = $RepositoryRoot } },
  @{ Name = "ci-wiring"; File = "Delivery.CiWiring.Tests.ps1"; Parameters = @{ SourceRoot = $RepositoryRoot } },
  @{ Name = "test-runner"; File = "TestRunner.Tests.ps1"; Parameters = @{} },
  @{ Name = "distribution-notices"; File = "DistributionNotices.Tests.ps1"; Parameters = @{ RepositoryRoot = $RepositoryRoot } }
)

$summaries = @()
foreach ($spec in $specs) {
  $path = Join-Path $PSScriptRoot $spec.File
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "Required Pester suite '$($spec.File)' does not exist"
  }

  $scriptSpec = @{
    Path = $path
    Parameters = $spec.Parameters
  }
  $invokeArguments = @{
    Script = $scriptSpec
    PassThru = $true
  }
  if ($OutputDirectory) {
    $invokeArguments.OutputFile = Join-Path $OutputDirectory "$($spec.Name).xml"
    $invokeArguments.OutputFormat = "NUnitXml"
  }

  $previousErrorAction = $ErrorActionPreference
  try {
    # Pester 3.4's XML writer probes Get-CimInstance for environment metadata; a sandbox may
    # report access denied there without affecting the assertions.
    $ErrorActionPreference = "Continue"
    $result = Invoke-Pester @invokeArguments
  } finally {
    $ErrorActionPreference = $previousErrorAction
  }

  $summary = Get-PesterSummary -Result $result
  $summary | Add-Member -NotePropertyName Suite -NotePropertyValue $spec.Name
  $summaries += $summary
}

$summaries |
  Select-Object Suite, Total, Passed, Failed, Inconclusive, Skipped, Pending, Unknown |
  Format-Table -AutoSize

$unsuccessful = @($summaries | Where-Object { -not (Test-PesterSummarySuccessful -Summary $_) })
if ($unsuccessful.Count -ne 0) {
  throw "Pester suites failed, were empty, or contained skipped/inconclusive/pending tests: $($unsuccessful.Suite -join ', ')"
}

$summaries

# Expected native failures inside passing tests leave LASTEXITCODE nonzero; the guard above
# already made every unsuccessful summary fatal, so report the successful run explicitly.
exit 0
