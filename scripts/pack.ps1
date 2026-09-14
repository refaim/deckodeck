[CmdletBinding()]
param(
  # Build-directory suffix (build/release<Suffix>, build/release-x86<Suffix>); defaults to the
  # PVDKIT_BUILD_SUFFIX environment variable, like the presets themselves.
  [string]$Suffix = $env:PVDKIT_BUILD_SUFFIX,
  # Plugin ids (directory names under plugins/) to package; every plugin found in the build
  # directories by default. As separate arguments or comma-separated (`-Plugins avif,rpgmvp`
  # reaches a script started with `powershell -File` as one string).
  [string[]]$Plugins = @(),
  # Where the zips go; a relative path is resolved against the repository root.
  [string]$DistDir = "dist"
)

# Packages the plugins from EXISTING release build directories: no configure, no build, no lint,
# no ASan. For every plugin found through build/release<Suffix>/plugins/<id>/package/manifest.json
# and its x86 counterpart (both architectures must be present), runs the import-table and
# export-table gates on the DLL and checks that its FileVersion equals the manifest version -
# every gate on every DLL before any zip is written, so a failure leaves nothing behind - and
# writes <DistDir>/<NAME>-<version>-<arch>.zip holding exactly <NAME>.pvd, readme_en.txt,
# readme_ru.txt, ChangeLog and LICENSES.txt (manifest.json is build metadata and stays out). A zip
# of the same name is overwritten; other zips are left alone. Prints each zip path with its
# SHA-256 on the information stream and emits one object per zip (Name, Version, Architecture,
# Zip, Sha256) on the pipeline. scripts/package.ps1 builds everything from scratch, runs the
# gates and then calls this script; the release workflow calls it on the CI build directories.
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
$Plugins = @($Plugins | ForEach-Object { $_ -split "," } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
if (-not [IO.Path]::IsPathRooted($DistDir)) {
  $DistDir = Join-Path $repository $DistDir
}
$DistDir = [IO.Path]::GetFullPath($DistDir)
# The gates run in the same PowerShell edition as this script (pwsh on the CI runner, Windows
# PowerShell on the reference machine).
$powerShell = if ($PSVersionTable.PSEdition -eq "Core") { "pwsh" } else { "powershell" }
# What the user gets: the DLL plus the four documents CMake staged next to the manifest.
$packageDocuments = @("readme_en.txt", "readme_ru.txt", "ChangeLog", "LICENSES.txt")

# Every packaged plugin of one architecture: id, manifest, package directory and DLL path.
function Get-BuiltPluginList {
  param(
    [string]$Architecture,
    [string]$BuildDirectory
  )

  $pluginsDirectory = Join-Path $BuildDirectory "plugins"
  if (-not (Test-Path -LiteralPath $pluginsDirectory -PathType Container)) {
    throw "$pluginsDirectory does not exist: build the release preset for $Architecture first (suffix '$Suffix')"
  }
  $manifests = @(Get-ChildItem -LiteralPath $pluginsDirectory -Recurse -File -Filter "manifest.json" |
    Where-Object { $_.Directory.Name -eq "package" -and $_.Directory.Parent.Parent.FullName -eq $pluginsDirectory } |
    Sort-Object FullName)
  $built = @()
  foreach ($manifestFile in $manifests) {
    $id = $manifestFile.Directory.Parent.Name
    if ($Plugins.Count -ne 0 -and $Plugins -notcontains $id) {
      continue
    }
    $manifest = Get-Content -LiteralPath $manifestFile.FullName -Raw | ConvertFrom-Json
    if ($manifest.architecture -ne $Architecture) {
      throw "$($manifestFile.FullName) says architecture '$($manifest.architecture)', expected '$Architecture'"
    }
    $pluginDirectory = $manifestFile.Directory.Parent.FullName
    $candidates = @(Get-ChildItem -LiteralPath $pluginDirectory -Recurse -File -Filter $manifest.file)
    if ($candidates.Count -ne 1) {
      throw "Expected exactly one $($manifest.file) under $pluginDirectory, found $($candidates.Count)"
    }
    foreach ($document in $packageDocuments) {
      if (-not (Test-Path -LiteralPath (Join-Path $manifestFile.Directory.FullName $document) -PathType Leaf)) {
        throw "$document was not staged in $($manifestFile.Directory.FullName)"
      }
    }
    $built += [pscustomobject]@{
      Id = $id
      Name = $manifest.name
      Version = $manifest.version
      Architecture = $Architecture
      PackageDirectory = $manifestFile.Directory.FullName
      Plugin = $candidates[0].FullName
    }
  }
  foreach ($id in $Plugins) {
    if (@($built | Where-Object { $_.Id -eq $id }).Count -eq 0) {
      throw "Plugin '$id' has no package manifest under $pluginsDirectory ($Architecture)"
    }
  }
  if ($built.Count -eq 0) {
    throw "No plugin package manifest under $pluginsDirectory ($Architecture)"
  }
  return $built
}

$builds = @(
  [pscustomobject]@{ Architecture = "x64"; BuildDirectory = Join-Path $repository "build\release$Suffix" },
  [pscustomobject]@{ Architecture = "x86"; BuildDirectory = Join-Path $repository "build\release-x86$Suffix" }
)
$built = @()
foreach ($build in $builds) {
  $built += @(Get-BuiltPluginList -Architecture $build.Architecture -BuildDirectory $build.BuildDirectory)
}
# Every plugin ships both architectures: the id sets of the two build directories must agree.
foreach ($build in $builds) {
  $ids = @($built | Where-Object { $_.Architecture -eq $build.Architecture } | ForEach-Object { $_.Id })
  foreach ($other in $builds | Where-Object { $_.Architecture -ne $build.Architecture }) {
    foreach ($id in $ids) {
      if (@($built | Where-Object { $_.Architecture -eq $other.Architecture -and $_.Id -eq $id }).Count -eq 0) {
        throw "Plugin '$id' is built for $($build.Architecture) but not for $($other.Architecture) ($($other.BuildDirectory))"
      }
    }
  }
}

# Every gate for every DLL first, so a failure on any plugin or architecture leaves no zip behind
# (the zips are written only after the whole set passed).
foreach ($entry in $built) {
  Invoke-Checked $powerShell @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                               (Join-Path $PSScriptRoot "check-imports.ps1"), "-Path", $entry.Plugin)
  Invoke-Checked $powerShell @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                               (Join-Path $PSScriptRoot "check-exports.ps1"), "-Path", $entry.Plugin)
  # The manifest version is the one the plugin's CMakeLists.txt declared (pvdkit_plugin_identity),
  # the same source that fills the VERSIONINFO resource; the DLL must agree.
  $fileVersion = (Get-Item -LiteralPath $entry.Plugin).VersionInfo.FileVersion
  if ($fileVersion -ne $entry.Version) {
    throw "$($entry.Plugin) carries FileVersion '$fileVersion', expected '$($entry.Version)'"
  }
}

New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
$staging = Join-Path ([IO.Path]::GetTempPath()) "pvdkit-pack-$PID"
$results = @()
try {
  foreach ($entry in $built) {
    if (Test-Path -LiteralPath $staging) {
      Remove-Item -LiteralPath $staging -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $staging | Out-Null
    Copy-Item -LiteralPath $entry.Plugin -Destination (Join-Path $staging "$($entry.Name).pvd")
    foreach ($document in $packageDocuments) {
      Copy-Item -LiteralPath (Join-Path $entry.PackageDirectory $document) -Destination (Join-Path $staging $document)
    }

    $zip = Join-Path $DistDir "$($entry.Name)-$($entry.Version)-$($entry.Architecture).zip"
    if (Test-Path -LiteralPath $zip) {
      Remove-Item -LiteralPath $zip -Force
    }
    Compress-Archive -Path (Join-Path $staging "*") -DestinationPath $zip -CompressionLevel Optimal
    $sha256 = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Information -MessageData "$zip" -InformationAction Continue
    Write-Information -MessageData "  SHA-256: $sha256" -InformationAction Continue
    $results += [pscustomobject]@{
      Name = $entry.Name
      Version = $entry.Version
      Architecture = $entry.Architecture
      Zip = $zip
      Sha256 = $sha256
    }
  }
} finally {
  if (Test-Path -LiteralPath $staging) {
    Remove-Item -LiteralPath $staging -Recurse -Force
  }
}

$results
