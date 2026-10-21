[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$SourceRoot,

  # Optional configured build, used to check that the selected plugin ids reached vcpkg.
  [string]$BuildDir = ""
)

$ErrorActionPreference = "Stop"

function Resolve-SvgVcpkgCommand {
  if (-not [string]::IsNullOrEmpty($env:VCPKG_ROOT)) {
    foreach ($name in @("vcpkg.exe", "vcpkg.cmd", "vcpkg.bat")) {
      $candidate = Join-Path $env:VCPKG_ROOT $name
      if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        return $candidate
      }
    }
    throw "VCPKG_ROOT '$($env:VCPKG_ROOT)' does not contain vcpkg.exe, vcpkg.cmd, or vcpkg.bat"
  }

  $command = Get-Command vcpkg -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $command) {
    return $null
  }
  return $command.Source
}

function Invoke-SvgVcpkgDryRun {
  param(
    [Parameter(Mandatory = $true)][string]$SourceRoot,
    [Parameter(Mandatory = $true)][string]$WorkRoot
  )

  $vcpkg = Resolve-SvgVcpkgCommand
  if (-not $vcpkg) {
    return $null
  }

  Push-Location $SourceRoot
  try {
    # The expected vcpkg stderr warning must stay data; a genuine failure remains the exit code.
    $previousErrorAction = $ErrorActionPreference
    try {
      $ErrorActionPreference = "Continue"
      $output = & $vcpkg install --dry-run --no-downloads `
        --triplet x64-windows-static-clang `
        "--overlay-triplets=$(Join-Path $SourceRoot 'triplets')" `
        --x-feature=svg `
        "--x-install-root=$(Join-Path $WorkRoot 'installed')" `
        "--x-buildtrees-root=$(Join-Path $WorkRoot 'buildtrees')" `
        "--x-packages-root=$(Join-Path $WorkRoot 'packages')" 2>&1
      $exitCode = $LASTEXITCODE
    } finally {
      $ErrorActionPreference = $previousErrorAction
    }
  } finally {
    Pop-Location
  }

  return [pscustomobject]@{
    Output = @($output)
    ExitCode = $exitCode
    CommandPath = $vcpkg
  }
}

function Initialize-FakeVcpkg {
  param(
    [Parameter(Mandatory = $true)][string]$Directory,
    [Parameter(Mandatory = $true)][int]$ExitCode,
    [Parameter(Mandatory = $true)][string]$MarkerText,
    [switch]$Warn
  )

  New-Item -ItemType Directory -Path $Directory -Force | Out-Null
  $lines = @('@echo off', "echo $MarkerText>`"%SVG_VCPKG_MARKER%`"")
  if ($Warn) {
    $lines += 'echo warning: fake vcpkg 1>&2'
  }
  $lines += "exit /b $ExitCode"
  $path = Join-Path $Directory "vcpkg.cmd"
  Set-Content -LiteralPath $path -Value $lines -Encoding ASCII
  return $path
}

function Invoke-SvgVcpkgFixture {
  param(
    [string]$VcpkgRoot,
    [string]$PathPrepend,
    [Parameter(Mandatory = $true)][string]$MarkerPath,
    [Parameter(Mandatory = $true)][string]$SourceRoot,
    [Parameter(Mandatory = $true)][string]$WorkRoot
  )

  $previousRoot = $env:VCPKG_ROOT
  $previousPath = $env:PATH
  $previousMarker = $env:SVG_VCPKG_MARKER
  try {
    if ([string]::IsNullOrEmpty($VcpkgRoot)) {
      Remove-Item Env:\VCPKG_ROOT -ErrorAction SilentlyContinue
    } else {
      $env:VCPKG_ROOT = $VcpkgRoot
    }
    $env:PATH = if ($PathPrepend) { "$PathPrepend;$previousPath" } else { $previousPath }
    $env:SVG_VCPKG_MARKER = $MarkerPath

    $errorRecord = $null
    $result = $null
    try {
      $result = Invoke-SvgVcpkgDryRun -SourceRoot $SourceRoot -WorkRoot $WorkRoot
    } catch {
      $errorRecord = $_
    }
    $marker = if (Test-Path -LiteralPath $MarkerPath -PathType Leaf) {
      (Get-Content -LiteralPath $MarkerPath -Raw).Trim()
    } else {
      $null
    }
    return [pscustomobject]@{
      Result = $result
      ErrorRecord = $errorRecord
      Marker = $marker
    }
  } finally {
    if ($null -eq $previousRoot) {
      Remove-Item Env:\VCPKG_ROOT -ErrorAction SilentlyContinue
    } else {
      $env:VCPKG_ROOT = $previousRoot
    }
    $env:PATH = $previousPath
    if ($null -eq $previousMarker) {
      Remove-Item Env:\SVG_VCPKG_MARKER -ErrorAction SilentlyContinue
    } else {
      $env:SVG_VCPKG_MARKER = $previousMarker
    }
  }
}

Describe "SVG vcpkg manifest contract" {
  It "declares the svg feature with no codec dependencies and keeps it in default-features" {
    $manifestPath = Join-Path $SourceRoot "vcpkg.json"
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json

    $defaultFeatures = @($manifest.'default-features')
    ($defaultFeatures -contains "svg") | Should Be $true
    foreach ($existing in @("avif", "exr", "rpgmvp")) {
      ($defaultFeatures -contains $existing) | Should Be $true
    }

    $feature = $manifest.features.svg
    if (-not $feature) {
      throw "vcpkg.json has no svg feature"
    }
    $dependencyCount = if ($null -eq $feature.dependencies) { 0 } else { @($feature.dependencies).Count }
    $dependencyCount | Should Be 0

    # The existing codec features remain present and keep their dependencies.
    @($manifest.features.avif.dependencies)[0].name | Should Be "libavif"
    @($manifest.features.exr.dependencies)[0] | Should Be "openexr"
    @($manifest.features.rpgmvp.dependencies)[0] | Should Be "libspng"
  }

  It "forwards selected plugin ids as manifest features without default features" {
    $root = Get-Content -LiteralPath (Join-Path $SourceRoot "cmake/vcpkg-root.cmake") -Raw
    $root | Should Match ([regex]::Escape('set(VCPKG_MANIFEST_FEATURES ${PVDKIT_PLUGINS})'))
    $root | Should Match ([regex]::Escape('set(VCPKG_MANIFEST_NO_DEFAULT_FEATURES ON)'))

    if ($BuildDir) {
      $cache = Get-Content -LiteralPath (Join-Path $BuildDir "CMakeCache.txt") -Raw
      $selection = [regex]::Match($cache, "(?m)^PVDKIT_PLUGINS:STRING=(.*)$").Groups[1].Value
      $selected = @()
      if ($selection.Trim()) {
        $selected = @($selection -split ";" | ForEach-Object { $_.Trim() } | Where-Object { $_ })
      }
      if ($selected.Count -gt 0) {
        ($selected -contains "svg") | Should Be $true
      } else {
        # The ordinary preset selects every plugin; SVG is configured through vcpkg.json's
        # default features rather than an explicit cache selector.
        $manifest = Get-Content -LiteralPath (Join-Path $SourceRoot "vcpkg.json") -Raw | ConvertFrom-Json
        (@($manifest.'default-features') -contains "svg") | Should Be $true
      }

      $buildFile = Join-Path $BuildDir "build.ninja"
      (Select-String -LiteralPath $buildFile -Pattern '^build plugins\\svg\\SVG\.pvd' -Quiet) |
        Should Be $true
      (Test-Path -LiteralPath (Join-Path $BuildDir "plugins/svg/package/manifest.json") -PathType Leaf) |
        Should Be $true

      # A metadata-only configure may use OFF; the normal product configure must remain ON.
      $cache | Should Match "VCPKG_MANIFEST_INSTALL:BOOL=(ON|OFF)"
    }
  }

  It "accepts the ordinary empty all-plugin selector and explicit SVG selectors" {
    $defaults = @("avif", "exr", "rpgmvp", "svg")
    foreach ($cacheText in @(
        "PVDKIT_PLUGINS:STRING=",
        "PVDKIT_PLUGINS:STRING=svg",
        "PVDKIT_PLUGINS:STRING=avif;svg")) {
      $selection = [regex]::Match($cacheText, "^PVDKIT_PLUGINS:STRING=(.*)$").Groups[1].Value
      if ($selection.Trim()) {
        $selected = @($selection -split ";" | ForEach-Object { $_.Trim() } | Where-Object { $_ })
        ($selected -contains "svg") | Should Be $true
      } else {
        ($defaults -contains "svg") | Should Be $true
      }
    }

    $selector = [regex]::Match("PVDKIT_PLUGINS:STRING=avif", "^PVDKIT_PLUGINS:STRING=(.*)$").Groups[1].Value
    (@($selector -split ";" | ForEach-Object { $_.Trim() } | Where-Object { $_ }) -contains "svg") | Should Be $false
  }

  It "is accepted as a real svg feature by vcpkg in a dry run" {
    $result = Invoke-SvgVcpkgDryRun -SourceRoot $SourceRoot -WorkRoot (Join-Path $TestDrive "vcpkg-dry")
    if ($null -eq $result) {
      Set-TestInconclusive "vcpkg is neither configured nor on PATH"
    }
    $text = $result.Output -join "`n"
    $result.ExitCode | Should Be 0
    $text | Should Not Match "not a feature supported"
  }
}

Describe "SVG vcpkg command selection" {
  It "prefers the configured VCPKG_ROOT executable over PATH" {
    $rootDir = Join-Path $TestDrive ("vcpkg root " + [guid]::NewGuid().ToString("N"))
    $pathDir = Join-Path $TestDrive ("vcpkg path " + [guid]::NewGuid().ToString("N"))
    $marker = Join-Path $TestDrive ("vcpkg-marker-" + [guid]::NewGuid().ToString("N") + ".txt")
    $rootCommand = Initialize-FakeVcpkg -Directory $rootDir -ExitCode 0 -MarkerText "root" -Warn
    Initialize-FakeVcpkg -Directory $pathDir -ExitCode 3 -MarkerText "path" | Out-Null

    $case = Invoke-SvgVcpkgFixture -VcpkgRoot $rootDir -PathPrepend $pathDir -MarkerPath $marker `
      -SourceRoot $SourceRoot -WorkRoot (Join-Path $TestDrive "work-selection")

    $case.ErrorRecord | Should Be $null
    $case.Result.CommandPath | Should Be $rootCommand
    $case.Result.ExitCode | Should Be 0
    $case.Marker | Should Be "root"
  }

  It "treats a benign vcpkg stderr warning as data" {
    $rootDir = Join-Path $TestDrive ("vcpkg warning root " + [guid]::NewGuid().ToString("N"))
    $marker = Join-Path $TestDrive ("vcpkg-marker-" + [guid]::NewGuid().ToString("N") + ".txt")
    Initialize-FakeVcpkg -Directory $rootDir -ExitCode 0 -MarkerText "warning" -Warn | Out-Null

    # The same fake is first on PATH so the original bare invocation also reaches it.
    $case = Invoke-SvgVcpkgFixture -VcpkgRoot $rootDir -PathPrepend $rootDir -MarkerPath $marker `
      -SourceRoot $SourceRoot -WorkRoot (Join-Path $TestDrive "work-warning")

    $case.ErrorRecord | Should Be $null
    $case.Result.ExitCode | Should Be 0
    ($case.Result.Output -join "`n") | Should Match "warning: fake vcpkg"
    $case.Marker | Should Be "warning"
  }

  It "reports a genuine vcpkg failure instead of treating it as success" {
    $rootDir = Join-Path $TestDrive ("vcpkg failing root " + [guid]::NewGuid().ToString("N"))
    $pathDir = Join-Path $TestDrive ("vcpkg passing path " + [guid]::NewGuid().ToString("N"))
    $marker = Join-Path $TestDrive ("vcpkg-marker-" + [guid]::NewGuid().ToString("N") + ".txt")
    Initialize-FakeVcpkg -Directory $rootDir -ExitCode 7 -MarkerText "root" | Out-Null
    Initialize-FakeVcpkg -Directory $pathDir -ExitCode 0 -MarkerText "path" | Out-Null

    $case = Invoke-SvgVcpkgFixture -VcpkgRoot $rootDir -PathPrepend $pathDir -MarkerPath $marker `
      -SourceRoot $SourceRoot -WorkRoot (Join-Path $TestDrive "work-failure")

    $case.ErrorRecord | Should Be $null
    $case.Result.ExitCode | Should Be 7
    $case.Marker | Should Be "root"
  }

  It "rejects an invalid configured VCPKG_ROOT instead of falling back to PATH" {
    $rootDir = Join-Path $TestDrive ("vcpkg empty root " + [guid]::NewGuid().ToString("N"))
    $pathDir = Join-Path $TestDrive ("vcpkg fallback path " + [guid]::NewGuid().ToString("N"))
    $marker = Join-Path $TestDrive ("vcpkg-marker-" + [guid]::NewGuid().ToString("N") + ".txt")
    New-Item -ItemType Directory -Path $rootDir -Force | Out-Null
    Initialize-FakeVcpkg -Directory $pathDir -ExitCode 0 -MarkerText "path" | Out-Null

    $case = Invoke-SvgVcpkgFixture -VcpkgRoot $rootDir -PathPrepend $pathDir -MarkerPath $marker `
      -SourceRoot $SourceRoot -WorkRoot (Join-Path $TestDrive "work-invalid")

    ($null -ne $case.ErrorRecord) | Should Be $true
    $case.ErrorRecord.Exception.Message | Should Match "VCPKG_ROOT"
    $case.Marker | Should Be $null
  }

  It "falls back to a vcpkg on PATH when VCPKG_ROOT is unset" {
    $pathDir = Join-Path $TestDrive ("vcpkg path only " + [guid]::NewGuid().ToString("N"))
    $marker = Join-Path $TestDrive ("vcpkg-marker-" + [guid]::NewGuid().ToString("N") + ".txt")
    $pathCommand = Initialize-FakeVcpkg -Directory $pathDir -ExitCode 0 -MarkerText "path"

    $case = Invoke-SvgVcpkgFixture -VcpkgRoot $null -PathPrepend $pathDir -MarkerPath $marker `
      -SourceRoot $SourceRoot -WorkRoot (Join-Path $TestDrive "work-fallback")

    $case.ErrorRecord | Should Be $null
    $case.Result.CommandPath | Should Be $pathCommand
    $case.Result.ExitCode | Should Be 0
    $case.Marker | Should Be "path"
  }
}

Describe "SVG upstream source contract" {
  function Invoke-SvgUpstreamSourceCapture {
    (Get-Command cmake -ErrorAction Stop) | Out-Null

    # A unique fixture root keeps each case on a fresh CMake cache with no override variables.
    $fixture = Join-Path $TestDrive ("svg-upstream-source-" + [guid]::NewGuid().ToString("N"))
    $capture = Join-Path $fixture "capture"
    $modules = Join-Path $fixture "modules"
    $build = Join-Path $fixture "build"
    New-Item -ItemType Directory -Path $capture, $modules, $build -Force | Out-Null

    # The product recipe includes FetchContent itself; this boundary module keeps the capture
    # stubs installed instead of letting CMake redefine the real commands.
    Set-Content -LiteralPath (Join-Path $modules "FetchContent.cmake") `
      -Value "# Source-contract fixture: FetchContent commands are stubbed by the caller." -Encoding ASCII

    $pluginCmake = (Resolve-Path -LiteralPath (Join-Path $SourceRoot "plugins/svg/CMakeLists.txt")).Path
    $fixtureCmake = @'
cmake_minimum_required(VERSION 3.20)
project(svg_upstream_source_contract NONE)

include(FetchContent)

function(FetchContent_Declare contentName)
  cmake_parse_arguments(arg "" "URL;URL_HASH" "" ${ARGN})
  if(contentName STREQUAL "svg_resvg_source")
    file(WRITE "${CAPTURE_DIR}/url.txt" "${arg_URL}")
    file(WRITE "${CAPTURE_DIR}/hash.txt" "${arg_URL_HASH}")
  endif()
endfunction()

function(FetchContent_MakeAvailable)
  message(FATAL_ERROR "PVDKIT_SVG_UPSTREAM_SOURCE_CAPTURED")
endfunction()

list(INSERT CMAKE_MODULE_PATH 0 "${SVG_SOURCE_CONTRACT_MODULES}")
include("${SVG_SOURCE_CONTRACT_PLUGIN_CMAKE}")
'@
    Set-Content -LiteralPath (Join-Path $fixture "CMakeLists.txt") -Value $fixtureCmake -Encoding ASCII

    # The expected CMake fatal error writes to stderr; keep it as data instead of a PowerShell stop.
    $previousErrorAction = $ErrorActionPreference
    try {
      $ErrorActionPreference = "Continue"
      $output = & cmake -S $fixture -B $build `
        "-DCAPTURE_DIR=$capture" `
        "-DSVG_SOURCE_CONTRACT_MODULES=$modules" `
        "-DSVG_SOURCE_CONTRACT_PLUGIN_CMAKE=$pluginCmake" 2>&1
      $exitCode = $LASTEXITCODE
    } finally {
      $ErrorActionPreference = $previousErrorAction
    }
    $text = $output -join "`n"

    # A configure must stop at the fixture sentinel, not at an unrelated CMake or download error.
    $text | Should Match ([regex]::Escape("PVDKIT_SVG_UPSTREAM_SOURCE_CAPTURED"))
    $exitCode | Should Not Be 0

    $urlPath = Join-Path $capture "url.txt"
    $hashPath = Join-Path $capture "hash.txt"
    (Test-Path -LiteralPath $urlPath -PathType Leaf) | Should Be $true
    (Test-Path -LiteralPath $hashPath -PathType Leaf) | Should Be $true
    return @{
      Url = (Get-Content -LiteralPath $urlPath -Raw).Trim()
      Hash = (Get-Content -LiteralPath $hashPath -Raw).Trim()
    }
  }

  It "evaluates the v-prefixed resvg tag archive URL" {
    $captured = Invoke-SvgUpstreamSourceCapture
    $captured.Url | Should Be "https://github.com/linebender/resvg/archive/refs/tags/v0.48.1.tar.gz"
  }

  It "evaluates the published resvg archive SHA-256" {
    $captured = Invoke-SvgUpstreamSourceCapture
    $captured.Hash | Should Be "SHA256=40dafea6b4b9d01e9d28b6d49f1e912daf3e9055676ad9179a5a2db6e7386945"
  }
}
