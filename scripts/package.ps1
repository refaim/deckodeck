[CmdletBinding()]
param(
  # Build-directory suffix for the packaging builds; kept apart from development builds.
  [string]$Suffix = "-pkg"
)

# Builds every plugin for x64 and x86 from scratch (scripts/build-all.ps1 -Clean: the `release`
# and `release-x86` presets, the import-table and export-table gates on each DLL) and packs each
# into dist/<NAME>-<version>-<arch>.zip with the README.txt and LICENSES.txt that the plugin's
# CMake configuration staged next to it (plugins/<id>/package/README.txt.in and the licence texts
# of the ports it lists, as vcpkg installed them). Prints the zip paths and SHA-256.
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

# --- asan: the AddressSanitizer gate (docs/tasks/task10-leaks.md, level 2) runs the whole test
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

$distDirectory = Join-Path $repository "dist"
New-Item -ItemType Directory -Force -Path $distDirectory | Out-Null
$staging = Join-Path $distDirectory "staging"

$results = @()
foreach ($build in $built) {
  # The manifest version is the one the plugin's CMakeLists.txt declared (pvdkit_plugin_identity),
  # the same source that fills the VERSIONINFO resource; the DLL must agree.
  $versionInfo = (Get-Item -LiteralPath $build.Plugin).VersionInfo
  if ($versionInfo.FileVersion -ne $build.Version) {
    throw "$($build.Plugin) carries FileVersion '$($versionInfo.FileVersion)', expected '$($build.Version)'"
  }
  foreach ($name in @("README.txt", "LICENSES.txt")) {
    if (-not (Test-Path -LiteralPath (Join-Path $build.PackageDirectory $name) -PathType Leaf)) {
      throw "$name was not staged in $($build.PackageDirectory)"
    }
  }

  if (Test-Path -LiteralPath $staging) {
    Remove-Item -LiteralPath $staging -Recurse -Force
  }
  New-Item -ItemType Directory -Force -Path $staging | Out-Null
  Copy-Item -LiteralPath $build.Plugin -Destination (Join-Path $staging "$($build.Name).pvd")
  Copy-Item -LiteralPath (Join-Path $build.PackageDirectory "README.txt") -Destination (Join-Path $staging "README.txt")
  Copy-Item -LiteralPath (Join-Path $build.PackageDirectory "LICENSES.txt") -Destination (Join-Path $staging "LICENSES.txt")

  $zip = Join-Path $distDirectory "$($build.Name)-$($build.Version)-$($build.Architecture).zip"
  if (Test-Path -LiteralPath $zip) {
    Remove-Item -LiteralPath $zip -Force
  }
  Compress-Archive -Path (Join-Path $staging "*") -DestinationPath $zip -CompressionLevel Optimal
  Remove-Item -LiteralPath $staging -Recurse -Force

  $results += [pscustomobject]@{
    Zip = $zip
    Sha256 = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
  }
}

foreach ($result in $results) {
  Write-Output "$($result.Zip)"
  Write-Output "  SHA-256: $($result.Sha256)"
}
