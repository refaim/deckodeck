[CmdletBinding()]
param(
  # Build-directory suffix for the packaging builds; kept apart from development builds.
  [string]$Suffix = "-pkg"
)

# Builds AVIF.pvd for x64 and x86 from scratch (scripts/build-all.ps1 -Clean: the `release` and
# `release-x86` presets, the import-table and export-table gates on each DLL) and packs each into
# dist/AVIF-<version>-<arch>.zip with a README.txt and the licence texts of libavif, dav1d and
# libyuv as vcpkg installed them (share/<port>/copyright). Prints the zip paths and SHA-256.
$ErrorActionPreference = "Stop"

$repository = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
# Remove previous zips before anything is built so a gate failure cannot leave stale archives
# that look like fresh output.
Get-ChildItem -LiteralPath (Join-Path $repository "dist") -Filter "AVIF-*.zip" -File -ErrorAction SilentlyContinue |
  Remove-Item -Force
$built = @(& (Join-Path $PSScriptRoot "build-all.ps1") -Suffix $Suffix -Clean)
if ($built.Count -ne 2 -or $built[0].Architecture -ne "x64" -or $built[1].Architecture -ne "x86") {
  throw "build-all.ps1 was expected to report the x64 and x86 plugins, got: $($built | Out-String)"
}
$plugins = @($built | ForEach-Object { $_.Plugin })

$builds = @(
  [pscustomobject]@{ Architecture = "x64"; Bits = "64-bit"; Preset = "release";     Triplet = "x64-windows-static-clang"; Plugin = $plugins[0] },
  [pscustomobject]@{ Architecture = "x86"; Bits = "32-bit"; Preset = "release-x86"; Triplet = "x86-windows-static-clang"; Plugin = $plugins[1] }
)

# The version is the CMake project version as the build configured it (project(VERSION) in
# CMakeLists.txt, the same source that fills the VERSIONINFO resource).
$cache = Get-Content -LiteralPath (Join-Path $repository "build\$($builds[0].Preset)$Suffix\CMakeCache.txt")
$version = ($cache | Where-Object { $_ -match '^CMAKE_PROJECT_VERSION:\w+=(.+)$' } | ForEach-Object { $Matches[1] } | Select-Object -First 1)
if (-not $version) {
  throw "CMAKE_PROJECT_VERSION was not found in the $($builds[0].Preset)$Suffix cache"
}
$author = ($cache | Where-Object { $_ -match '^AVIFPVD_AUTHOR:\w+=(.+)$' } | ForEach-Object { $Matches[1] } | Select-Object -First 1)
$copyright = ($cache | Where-Object { $_ -match '^AVIFPVD_COPYRIGHT:\w+=(.+)$' } | ForEach-Object { $Matches[1] } | Select-Object -First 1)

$distDirectory = Join-Path $repository "dist"
New-Item -ItemType Directory -Force -Path $distDirectory | Out-Null
$staging = Join-Path $distDirectory "staging"

$results = @()
foreach ($build in $builds) {
  $versionInfo = (Get-Item -LiteralPath $build.Plugin).VersionInfo
  if ($versionInfo.FileVersion -ne $version) {
    throw "$($build.Plugin) carries FileVersion '$($versionInfo.FileVersion)', expected '$version'"
  }

  $installed = Join-Path $repository "build\$($build.Preset)$Suffix\vcpkg_installed\$($build.Triplet)\share"
  $licenses = @()
  foreach ($library in @(
      [pscustomobject]@{ Port = "libavif"; Name = "libavif (BSD-2-Clause)" },
      [pscustomobject]@{ Port = "dav1d";   Name = "dav1d (BSD-2-Clause)" },
      [pscustomobject]@{ Port = "libyuv";  Name = "libyuv (BSD-3-Clause)" })) {
    # The text is whatever vcpkg installed for the port (upstream COPYING/LICENSE files, UTF-8).
    $copyrightFile = Join-Path $installed "$($library.Port)\copyright"
    if (-not (Test-Path -LiteralPath $copyrightFile -PathType Leaf)) {
      throw "Licence text of $($library.Port) was not found at $copyrightFile"
    }
    $licenses += "=" * 78
    $licenses += "$($library.Name) - as installed by vcpkg in share/$($library.Port)/copyright"
    $licenses += "=" * 78
    $licenses += ""
    $licenses += (Get-Content -LiteralPath $copyrightFile -Raw -Encoding UTF8).TrimEnd()
    $licenses += ""
    $licenses += ""
  }

  $readme = @(
    "AVIF.pvd $version ($($build.Architecture)) - AVIF decoder plugin for PictureView 3 (Far Manager)",
    "",
    "INSTALL",
    "  Copy AVIF.pvd next to 0PictureView.dll in PictureView's plugin folder",
    "  (typically %FARPROFILE%\Plugins\PictureView or <Far>\Plugins\PictureView) and",
    "  restart Far Manager. This build is for the $($build.Bits) Far Manager / PictureView;",
    "  the other architecture is a separate package. The plugin registers with",
    "  priority 10 and is picked up by PictureView's automatic format detection.",
    "",
    "WHAT IS SUPPORTED",
    "  - Still images and image sequences (avis): every frame is a page with its",
    "    display time.",
    "  - 8, 10 and 12-bit sources, 4:4:4 / 4:2:2 / 4:2:0 / 4:0:0, grid images,",
    "    progressive files.",
    "  - Alpha (straight, 32-bit BGRA output); opaque images are 24-bit BGR.",
    "  - clap, irot and imir transformative properties, applied in that order.",
    "  - Files from archives and virtual panels (the host hands over the whole file",
    "    in memory).",
    "  - Statically linked: imports KERNEL32.dll only; no runtime, WIC codec or",
    "    GDI+ needed.",
    "",
    "LIMITATIONS",
    "  - HDR (PQ/HLG) sources are converted by matrix only; there is no tone mapping.",
    "  - ICC profiles are ignored (the PVD interface has no colour management).",
    "  - Gain maps, layered (a1lx) selection, progressive preview rendering and",
    "    16-bit output are not supported; EXIF orientation is not applied (AVIF's",
    "    irot/imir take precedence by specification).",
    "  - clap/irot/imir properties not marked essential are refused by libavif.",
    "  - Images are limited to 16384 x 16384 pixels in area (268 megapixels) and",
    "    32768 pixels per side.",
    "",
    "AUTHOR",
    "  $author",
    "  $copyright",
    "",
    "VERSION",
    "  $version ($($versionInfo.Comments))",
    "",
    "LICENCES",
    "  The plugin bundles libavif, dav1d and libyuv; their licence texts are in",
    "  LICENSES.txt."
  )

  if (Test-Path -LiteralPath $staging) {
    Remove-Item -LiteralPath $staging -Recurse -Force
  }
  New-Item -ItemType Directory -Force -Path $staging | Out-Null
  Copy-Item -LiteralPath $build.Plugin -Destination (Join-Path $staging "AVIF.pvd")
  [IO.File]::WriteAllLines((Join-Path $staging "README.txt"), [string[]]$readme, [Text.UTF8Encoding]::new($false))
  [IO.File]::WriteAllLines((Join-Path $staging "LICENSES.txt"), [string[]]$licenses, [Text.UTF8Encoding]::new($false))

  $zip = Join-Path $distDirectory "AVIF-$version-$($build.Architecture).zip"
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
