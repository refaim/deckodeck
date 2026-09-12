[CmdletBinding()]
param(
  # Build-directory suffix (build/release<Suffix>, build/release-x86<Suffix>); defaults to the
  # PVDKIT_BUILD_SUFFIX environment variable, like the presets themselves.
  [string]$Suffix = $env:PVDKIT_BUILD_SUFFIX,
  # Remove the two build directories first so the DLLs come from a from-scratch build.
  [switch]$Clean
)

# Builds every plugin for both architectures with the `release` and `release-x86` presets, runs
# the import-table and export-table gates on each DLL and copies them to dist/x64/<NAME>.pvd and
# dist/x86/<NAME>.pvd. Plugins are discovered through the package manifests CMake writes
# (build/<preset>/plugins/<id>/package/manifest.json: name, version, architecture, file). Tool
# output goes to the host; the pipeline receives one object per plugin and architecture (x64
# first: Name, Version, Architecture, Preset, BuildDirectory, PackageDirectory, Plugin, Size) for
# callers such as scripts/package.ps1.
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
$hadSuffix = Test-Path -LiteralPath "Env:PVDKIT_BUILD_SUFFIX"
$previousSuffix = $env:PVDKIT_BUILD_SUFFIX
$env:PVDKIT_BUILD_SUFFIX = $Suffix

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

    $manifests = @(Get-ChildItem -LiteralPath (Join-Path $buildDirectory "plugins") -Recurse -File -Filter "manifest.json" |
      Where-Object { $_.Directory.Name -eq "package" } |
      Sort-Object FullName)
    if ($manifests.Count -eq 0) {
      throw "The $($build.Preset) preset produced no plugin package manifest under $buildDirectory\plugins"
    }

    foreach ($manifestFile in $manifests) {
      $manifest = Get-Content -LiteralPath $manifestFile.FullName -Raw | ConvertFrom-Json
      if ($manifest.architecture -ne $build.Architecture) {
        throw "$($manifestFile.FullName) says architecture '$($manifest.architecture)', expected '$($build.Architecture)'"
      }
      $pluginDirectory = $manifestFile.Directory.Parent.FullName
      $candidates = @(Get-ChildItem -LiteralPath $pluginDirectory -Recurse -File -Filter $manifest.file)
      if ($candidates.Count -ne 1) {
        throw "Expected exactly one $($manifest.file) under $pluginDirectory, found $($candidates.Count)"
      }
      $plugin = $candidates[0].FullName
      Invoke-Checked "powershell" @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                                    (Join-Path $PSScriptRoot "check-imports.ps1"), "-Path", $plugin)
      Invoke-Checked "powershell" @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                                    (Join-Path $PSScriptRoot "check-exports.ps1"), "-Path", $plugin)

      $distDirectory = Join-Path $repository "dist\$($build.Architecture)"
      New-Item -ItemType Directory -Force -Path $distDirectory | Out-Null
      $destination = Join-Path $distDirectory $manifest.file
      Copy-Item -LiteralPath $plugin -Destination $destination -Force
      # Progress for the operator; the pipeline itself carries only the plugin objects below.
      Write-Information -MessageData "$($build.Architecture): $destination ($((Get-Item -LiteralPath $destination).Length) bytes)" -InformationAction Continue
      [pscustomobject]@{
        Name = $manifest.name
        Version = $manifest.version
        Architecture = $build.Architecture
        Preset = $build.Preset
        BuildDirectory = $buildDirectory
        PackageDirectory = $manifestFile.Directory.FullName
        Plugin = $destination
        Size = (Get-Item -LiteralPath $destination).Length
      }
    }
  }
} finally {
  if ($hadSuffix) {
    $env:PVDKIT_BUILD_SUFFIX = $previousSuffix
  } else {
    Remove-Item -LiteralPath "Env:PVDKIT_BUILD_SUFFIX" -ErrorAction SilentlyContinue
  }
  Pop-Location
}
