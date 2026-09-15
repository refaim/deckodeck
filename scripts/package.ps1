[CmdletBinding()]
param(
  # Build-directory suffix for the packaging builds; kept apart from development builds.
  [string]$Suffix = "-pkg"
)

# The release pipeline on one machine: builds every plugin for x64 and x86 from scratch
# (scripts/build-all.ps1 -Clean: the `release` and `release-x86` presets, the import-table and
# export-table gates on each DLL), runs scripts/lint.ps1 on both release directories and the
# `asan` preset from scratch, then packs the zips with scripts/pack.ps1
# (dist/<NAME>-<version>-<arch>.zip with the static English/Russian readmes, ChangeLog and the
# LICENSES.txt that the plugin's CMake configuration staged). pack.ps1 prints the zip paths and
# SHA-256 and its objects are forwarded to the pipeline.
$ErrorActionPreference = "Stop"

$repository = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
# Remove previous zips before anything is built so a gate failure cannot leave stale archives
# that look like fresh output.
Get-ChildItem -LiteralPath (Join-Path $repository "dist") -File -ErrorAction SilentlyContinue |
  Where-Object { $_.Name -match '^.+-\d+\.\d+\.\d+-(x64|x86)\.zip$' } |
  Remove-Item -Force
$built = @(& (Join-Path $PSScriptRoot "build-all.ps1") -Suffix $Suffix -Clean)
if ($built.Count -eq 0) {
  throw "build-all.ps1 reported no plugins"
}
$architectures = @($built | ForEach-Object { $_.Architecture } | Sort-Object -Unique)
if (($architectures -join ",") -ne "x64,x86") {
  throw "build-all.ps1 was expected to report x64 and x86 plugins, got: $($built | Out-String)"
}

# --- lint: the static-analysis gate runs once per architecture on the release build it just made
# (clang-tidy and cppcheck read that build's compile_commands.json, BinSkim its DLLs; clang-format
# and PSScriptAnalyzer cover the tree). Any finding aborts packaging before a zip exists.
foreach ($buildDirectory in @($built | ForEach-Object { $_.BuildDirectory } | Sort-Object -Unique)) {
  & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "lint.ps1") `
    -BuildDir $buildDirectory -ReleaseDir $buildDirectory | Out-Host
  if ($LASTEXITCODE -ne 0) {
    throw "scripts/lint.ps1 reported findings for $buildDirectory"
  }
}
# --- end lint ---

# --- asan: the AddressSanitizer gate (level 2 of the leak gate) runs the whole test
# suite - leak scenarios and hostile corpus included - once, on x64, with every target instrumented
# (the `asan` preset; its build directory is build/asan<Suffix>, rebuilt from scratch like the
# release ones). Any ASan report fails a test and therefore the packaging. The preset itself
# documents why there is no LeakSanitizer on Windows and no x86 variant.
$asanDirectory = Join-Path $repository "build\asan$Suffix"
if (Test-Path -LiteralPath $asanDirectory) {
  $buildRoot = [IO.Path]::GetFullPath((Join-Path $repository "build")) + [IO.Path]::DirectorySeparatorChar
  if (-not ([IO.Path]::GetFullPath($asanDirectory)).StartsWith($buildRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to remove a build directory outside $buildRoot"
  }
  Remove-Item -LiteralPath $asanDirectory -Recurse -Force
}
$hadSuffix = Test-Path -LiteralPath "Env:PVDKIT_BUILD_SUFFIX"
$previousSuffix = $env:PVDKIT_BUILD_SUFFIX
$env:PVDKIT_BUILD_SUFFIX = $Suffix
Push-Location $repository
try {
  foreach ($step in @(@("--preset", "asan"), @("--build", "--preset", "asan"))) {
    & cmake @step | Out-Host
    if ($LASTEXITCODE -ne 0) {
      throw "cmake $($step -join ' ') failed with exit code $LASTEXITCODE"
    }
  }
  & ctest --preset asan | Out-Host
  if ($LASTEXITCODE -ne 0) {
    throw "ctest --preset asan failed with exit code $LASTEXITCODE (an AddressSanitizer report or a failing test)"
  }
} finally {
  if ($hadSuffix) {
    $env:PVDKIT_BUILD_SUFFIX = $previousSuffix
  } else {
    Remove-Item -LiteralPath "Env:PVDKIT_BUILD_SUFFIX" -ErrorAction SilentlyContinue
  }
  Pop-Location
}
# --- end asan ---

# pack.ps1 re-runs the two table gates, checks each DLL's FileVersion against its manifest and
# writes the zips (overwriting same-named ones only; the cleanup above already removed the rest).
& (Join-Path $PSScriptRoot "pack.ps1") -Suffix $Suffix -DistDir (Join-Path $repository "dist")
