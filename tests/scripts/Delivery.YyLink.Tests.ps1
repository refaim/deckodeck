[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$BuildDirX64,

  [string]$BuildDirX86 = ""
)

$ErrorActionPreference = "Stop"

function Get-NinjaBuildBlock {
  param(
    [string]$BuildDir,
    [string]$TargetFile
  )

  $buildFile = Join-Path $BuildDir "build.ninja"
  $lines = Get-Content -LiteralPath $buildFile
  $normalizedTarget = $TargetFile.Replace("/", "\")
  $start = -1
  for ($index = 0; $index -lt $lines.Count; $index++) {
    if ($lines[$index] -match "^build\s+$([regex]::Escape($normalizedTarget))(?:\s|:)") {
      $start = $index
      break
    }
  }
  if ($start -lt 0) {
    throw "Target $TargetFile is absent from $buildFile"
  }

  $block = [Collections.Generic.List[string]]::new()
  for ($index = $start; $index -lt $lines.Count; $index++) {
    if ($index -gt $start -and $lines[$index] -match "^build ") {
      break
    }
    $block.Add($lines[$index])
  }
  return ($block -join "`n")
}

function Assert-YyLinkContract {
  param(
    [string]$BuildDir,
    [string]$Architecture
  )

  $arch = if ($Architecture -eq "x64") { "x64" } else { "x86" }
  $machine = if ($Architecture -eq "x64") { "x64" } else { "x86" }
  $libraryDirectory = "Lib/6.1.7600.0/$arch"
  $escapedLibraryDirectory = [regex]::Escape($libraryDirectory.Replace("/", "\"))

  $plugin = Get-NinjaBuildBlock $BuildDir "plugins/svg/SVG.pvd"
  $plugin | Should Match $escapedLibraryDirectory
  $plugin | Should Match "/machine:$machine"
  $plugin | Should Match ([regex]::Escape("/ENTRY:DllMainCRTStartupForYY_Thunks"))

  $adapter = Get-NinjaBuildBlock $BuildDir "plugins/svg/tests/adapters/svg_adapter_tests.exe"
  $adapter | Should Match $escapedLibraryDirectory
  $adapter | Should Match "/machine:$machine"

  $sequence = Get-NinjaBuildBlock $BuildDir "plugins/svg/tests/e2e/svg_sequence_tests.exe"
  $sequence | Should Match $escapedLibraryDirectory
  $sequence | Should Match "/machine:$machine"

  foreach ($executable in @($adapter, $sequence)) {
    ($executable -match "/ENTRY:DllMainCRTStartupForYY_Thunks") | Should Be $false
    ($executable -match "YY_ThunksOriginalDllMainCRTStartup") | Should Be $false
  }
}

Describe "SVG YY-Thunks link graph" {
  It "links the x64 plugin and C API executables against the x64 overlay" {
    Assert-YyLinkContract $BuildDirX64 "x64"
  }

  It "links an x86 build against the x86 overlay when supplied" {
    if (-not $BuildDirX86) {
      Set-TestInconclusive "BuildDirX86 was not supplied"
    }
    Assert-YyLinkContract $BuildDirX86 "x86"
  }
}
