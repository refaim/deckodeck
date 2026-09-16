[CmdletBinding()]
param(
  [string]$Destination,
  [switch]$VerifyOnly
)

# Downloads the pinned openexr-images corpus files into plugins/exr/fixtures (or verifies the ones
# there) against scripts/openexr-images.sha256. The commit is pinned; the checksums attest that the
# bytes in this repository are exactly what that commit holds (provenance and license are the
# repository's, documented in fixtures/SOURCES.md).

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Destination)) {
  $Destination = Join-Path $PSScriptRoot "..\fixtures"
}

$commit = "e38ffb0790f62f05a6f083a6fa4cac150b3b7452"
$baseUri = "https://raw.githubusercontent.com/AcademySoftwareFoundation/openexr-images/$commit"
$fixtures = [ordered]@{
  "DisplayWindow/t01.exr" = "t01.exr"
  "DisplayWindow/t02.exr" = "t02.exr"
  "DisplayWindow/t05.exr" = "t05.exr"
  "DisplayWindow/t07.exr" = "t07.exr"
  "DisplayWindow/t09.exr" = "t09.exr"
  "DisplayWindow/t13.exr" = "t13.exr"
  "DisplayWindow/t14.exr" = "t14.exr"
  "TestImages/AllHalfValues.exr" = "AllHalfValues.exr"
  "TestImages/BrightRingsNanInf.exr" = "BrightRingsNanInf.exr"
  "TestImages/GammaChart.exr" = "GammaChart.exr"
  "TestImages/GrayRampsHorizontal.exr" = "GrayRampsHorizontal.exr"
  "TestImages/WideColorGamut.exr" = "WideColorGamut.exr"
  "TestImages/WideFloatRange.exr" = "WideFloatRange.exr"
  "TestImages/stripes.exr" = "stripes.exr"
  "LuminanceChroma/Garden.exr" = "Garden.exr"
  "MultiResolution/ColorCodedLevels.exr" = "ColorCodedLevels.exr"
  "MultiResolution/PeriodicPattern.exr" = "PeriodicPattern.exr"
  "Chromaticities/Rec709_YC.exr" = "Rec709_YC.exr"
  "Chromaticities/XYZ_YC.exr" = "XYZ_YC.exr"
}
$manifestPath = Join-Path $PSScriptRoot "openexr-images.sha256"
$checksums = @{}
foreach ($line in Get-Content -LiteralPath $manifestPath) {
  if ($line -notmatch '^([0-9a-fA-F]{64})\s{2}(.+)$') {
    throw "Invalid checksum manifest line: $line"
  }
  $checksums[$Matches[2]] = $Matches[1].ToLowerInvariant()
}
if ($checksums.Count -ne $fixtures.Count) {
  throw "Checksum manifest has $($checksums.Count) entries; expected $($fixtures.Count)"
}

function Assert-FixtureChecksum {
  param(
    [string]$Path,
    [string]$Name
  )

  if (-not $checksums.ContainsKey($Name)) {
    throw "Checksum manifest has no entry for $Name"
  }
  $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
  if ($actual -ne $checksums[$Name]) {
    throw "SHA-256 mismatch for $Name`: expected $($checksums[$Name]), got $actual"
  }
}

New-Item -ItemType Directory -Force -Path $Destination | Out-Null

foreach ($fixture in $fixtures.GetEnumerator()) {
  $output = Join-Path $Destination $fixture.Value
  if ($VerifyOnly) {
    Assert-FixtureChecksum $output $fixture.Value
    continue
  }

  $uri = "$baseUri/$($fixture.Key)"
  $download = "$output.download"
  Write-Output "Fetching $uri"
  try {
    Invoke-WebRequest -UseBasicParsing -Uri $uri -OutFile $download
    Assert-FixtureChecksum $download $fixture.Value
    Move-Item -LiteralPath $download -Destination $output -Force
  } finally {
    Remove-Item -LiteralPath $download -Force -ErrorAction SilentlyContinue
  }
}

if ($VerifyOnly) {
  Write-Output "Verified SHA-256 for $($fixtures.Count) pinned files at openexr-images commit $commit."
} else {
  Write-Output "Fetched and verified $($fixtures.Count) pinned files at openexr-images commit $commit."
}
