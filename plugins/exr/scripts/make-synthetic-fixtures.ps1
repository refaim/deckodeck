[CmdletBinding()]
param(
  [string]$Destination,
  [string]$ReferenceHeader
)

# Regenerates the synthetic EXR fixtures and the reference pixel table with the OpenEXR Python
# binding, pinned to the library version the plugin links (3.4.13) so lossy schemes (PXR24, B44,
# DWA, HTJ2K) decode to the same values the reference records. `uv` fetches the pinned wheels into
# its cache; nothing is installed system-wide. The pictures are formulas (make_synthetic_fixtures.py),
# so a rerun reproduces every file byte for byte; fixtures/SOURCES.md pins their SHA-256.

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Destination)) {
  $Destination = Join-Path $PSScriptRoot "..\fixtures"
}
if ([string]::IsNullOrWhiteSpace($ReferenceHeader)) {
  $ReferenceHeader = Join-Path $PSScriptRoot "..\tests\support\ReferencePixels.hpp"
}

if (-not (Get-Command uv -ErrorAction SilentlyContinue)) {
  throw "uv must be available on PATH (https://docs.astral.sh/uv/)"
}

New-Item -ItemType Directory -Force -Path $Destination | Out-Null
$destination = (Resolve-Path $Destination).Path
$referenceHeader = [IO.Path]::GetFullPath($ReferenceHeader)

# The pinned corpus files that carry a reference too (luminance-chroma files are referenced
# against the C++ library in the adapter test instead and are not listed here).
$corpus = @(
  "t01.exr", "t02.exr", "t05.exr", "t07.exr", "t09.exr", "t13.exr", "t14.exr",
  "AllHalfValues.exr", "BrightRingsNanInf.exr", "GammaChart.exr", "GrayRampsHorizontal.exr",
  "WideColorGamut.exr", "WideFloatRange.exr", "stripes.exr", "Garden.exr", "ColorCodedLevels.exr",
  "PeriodicPattern.exr"
) | ForEach-Object { Join-Path $destination $_ } | Where-Object { Test-Path $_ }

$script = Join-Path $PSScriptRoot "make_synthetic_fixtures.py"
& uv run --python 3.12 --with "OpenEXR==3.4.13" --with "numpy==2.3.3" python $script $destination $referenceHeader @corpus
if ($LASTEXITCODE -ne 0) {
  throw "make_synthetic_fixtures.py failed with exit code $LASTEXITCODE"
}

# The header is a source file like any other: the lint gate's clang-format must find nothing to do.
. (Join-Path $PSScriptRoot "..\..\..\scripts\llvm-dir.ps1")
$clangFormat = Join-Path (Get-LlvmDir) "clang-format.exe"
& $clangFormat -i --style=file $referenceHeader
if ($LASTEXITCODE -ne 0) {
  throw "clang-format failed with exit code $LASTEXITCODE"
}
