[CmdletBinding()]
param(
  # Build-directory suffix (build/release<Suffix>, build/release-x86<Suffix>); defaults to the
  # AVIFPVD_BUILD_SUFFIX environment variable, like the presets themselves.
  [string]$Suffix = $env:AVIFPVD_BUILD_SUFFIX,
  # Remove the two build directories first so the DLLs come from a from-scratch build.
  [switch]$Clean
)

# Builds AVIF.pvd for both architectures with the `release` and `release-x86` presets, runs the
# import-table and export-table gates on each DLL and copies them to dist/x64/AVIF.pvd and
# dist/x86/AVIF.pvd. Tool output goes to the host; the pipeline receives one object per
# architecture (x64 first: Architecture, Preset, BuildDirectory, Plugin, Size) for callers such
# as scripts/package.ps1.
$ErrorActionPreference = "Stop"

function Invoke-Checked {
  param(
    [string]$Command,
    [string[]]$Arguments
  )

  & $Command @Arguments | Out-Host
  if ($LASTEXITCODE -ne 0) {
    throw "$Command $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
  }
}

$repository = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$hadSuffix = Test-Path -LiteralPath "Env:AVIFPVD_BUILD_SUFFIX"
$previousSuffix = $env:AVIFPVD_BUILD_SUFFIX
$env:AVIFPVD_BUILD_SUFFIX = $Suffix

Push-Location $repository
try {
  $builds = @(
    [pscustomobject]@{ Architecture = "x64"; Preset = "release" },
    [pscustomobject]@{ Architecture = "x86"; Preset = "release-x86" }
  )
  foreach ($build in $builds) {
    $buildDirectory = Join-Path $repository "build\$($build.Preset)$Suffix"
    if ($Clean -and (Test-Path -LiteralPath $buildDirectory)) {
      $buildRoot = [IO.Path]::GetFullPath((Join-Path $repository "build")) + [IO.Path]::DirectorySeparatorChar
      if (-not ([IO.Path]::GetFullPath($buildDirectory)).StartsWith($buildRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove a build directory outside $buildRoot"
      }
      Remove-Item -LiteralPath $buildDirectory -Recurse -Force
    }

    Invoke-Checked "cmake" @("--preset", $build.Preset)
    Invoke-Checked "cmake" @("--build", "--preset", $build.Preset)

    $plugin = Join-Path $buildDirectory "src\pvd\AVIF.pvd"
    if (-not (Test-Path -LiteralPath $plugin -PathType Leaf)) {
      throw "The $($build.Preset) preset did not produce $plugin"
    }
    Invoke-Checked "powershell" @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                                  (Join-Path $PSScriptRoot "check-imports.ps1"), "-Path", $plugin)
    Invoke-Checked "powershell" @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                                  (Join-Path $PSScriptRoot "check-exports.ps1"), "-Path", $plugin)

    $distDirectory = Join-Path $repository "dist\$($build.Architecture)"
    New-Item -ItemType Directory -Force -Path $distDirectory | Out-Null
    $destination = Join-Path $distDirectory "AVIF.pvd"
    Copy-Item -LiteralPath $plugin -Destination $destination -Force
    Write-Host "$($build.Architecture): $destination ($((Get-Item -LiteralPath $destination).Length) bytes)"
    [pscustomobject]@{
      Architecture = $build.Architecture
      Preset = $build.Preset
      BuildDirectory = $buildDirectory
      Plugin = $destination
      Size = (Get-Item -LiteralPath $destination).Length
    }
  }
} finally {
  if ($hadSuffix) {
    $env:AVIFPVD_BUILD_SUFFIX = $previousSuffix
  } else {
    Remove-Item -LiteralPath "Env:AVIFPVD_BUILD_SUFFIX" -ErrorAction SilentlyContinue
  }
  Pop-Location
}
