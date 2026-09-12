[CmdletBinding()]
param(
  [string]$Destination,
  [switch]$VerifyOnly
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Destination)) {
  $Destination = Join-Path $PSScriptRoot "..\tests\fixtures"
}

$commit = "66663952a677bb8a13ea1530d5694775d7d143d4"
$baseUri = "https://raw.githubusercontent.com/AOMediaCodec/libavif/$commit/tests/data"
$fixtures = [ordered]@{
  "white_1x1.avif" = "white_1x1.avif"
  "io/kodim03_yuv420_8bpc.avif" = "kodim03_yuv420_8bpc.avif"
  "io/cosmos1650_yuv444_10bpc_p3pq.avif" = "cosmos1650_yuv444_10bpc_p3pq.avif"
  "alpha_noispe.avif" = "alpha_noispe.avif"
  "abc_color_irot_alpha_irot.avif" = "abc_color_irot_alpha_irot.avif"
  "abc_color_irot_alpha_NOirot.avif" = "abc_color_irot_alpha_NOirot.avif"
  "clap_irot_imir_non_essential.avif" = "clap_irot_imir_non_essential.avif"
  "clop_irot_imor.avif" = "clop_irot_imor.avif"
  "sofa_grid1x5_420.avif" = "sofa_grid1x5_420.avif"
  "color_grid_alpha_nogrid.avif" = "color_grid_alpha_nogrid.avif"
  "colors-animated-8bpc.avif" = "colors-animated-8bpc.avif"
  "colors-animated-8bpc-alpha-exif-xmp.avif" = "colors-animated-8bpc-alpha-exif-xmp.avif"
  "colors-animated-12bpc-keyframes-0-2-3.avif" = "colors-animated-12bpc-keyframes-0-2-3.avif"
  "colors_hdr_rec2020.avif" = "colors_hdr_rec2020.avif"
  "colors_sdr_srgb.avif" = "colors_sdr_srgb.avif"
  "paris_icc_exif_xmp.avif" = "paris_icc_exif_xmp.avif"
  "draw_points_idat_progressive.avif" = "draw_points_idat_progressive.avif"
  "extended_pixi.avif" = "extended_pixi.avif"
  "weld_sato_12B_8B_q0.avif" = "weld_sato_12B_8B_q0.avif"
  "README.md" = "LIBAVIF_DATA_README.md"
}
$manifestPath = Join-Path $PSScriptRoot "libavif-fixtures.sha256"
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
  Write-Output "Verified SHA-256 for $($fixtures.Count) pinned files at libavif commit $commit."
} else {
  Write-Output "Fetched and verified $($fixtures.Count) pinned files at libavif commit $commit."
}
