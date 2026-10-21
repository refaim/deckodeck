[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$BuildDir,

  # The identifier/path passed to PVDKIT_RESVG_RUST_TOOLCHAIN. The required compiler release is
  # independent of it: CI selects the named 1.99.1 toolchain, while an offline configure may
  # select an existing absolute toolchain directory.
  [string]$SelectedToolchain = "",
  [string]$RustTarget = "x86_64-pc-windows-msvc"
)

$ErrorActionPreference = "Stop"
$RequiredRelease = "1.99.1"

function Get-RustSelection {
  param([string]$Command)

  $rustup = [regex]::Match(
    $Command,
    '(?i)(?<exe>"[^"]*rustup(?:\.exe)?"|\S*[\\/]rustup(?:\.exe)?)\s+run\s+' +
    '(?<tool>"[^"]+"|''[^'']+''|\S+)\s+cargo(?:\.exe)?\s+rustc(?:\s|$)')
  if ($rustup.Success) {
    return [pscustomobject]@{
      Kind = "rustup-run"
      Executable = $rustup.Groups["exe"].Value.Trim('"', "'")
      Toolchain = $rustup.Groups["tool"].Value.Trim('"', "'")
    }
  }

  $cargo = [regex]::Match(
    $Command,
    '(?i)(?<exe>"[^"]*cargo(?:\.exe)?"|\S*[\\/]cargo(?:\.exe)?)\s+\+' +
    '(?<tool>"[^"]+"|''[^'']+''|\S+)\s+rustc(?:\s|$)')
  if ($cargo.Success) {
    return [pscustomobject]@{
      Kind = "cargo-plus"
      Executable = $cargo.Groups["exe"].Value.Trim('"', "'")
      Toolchain = $cargo.Groups["tool"].Value.Trim('"', "'")
    }
  }

  if ($Command -match "(?i)cargo(?:\.exe)?\s+rustc(?:\s|$)") {
    return [pscustomobject]@{
      Kind = "ambient"
      Executable = ""
      Toolchain = ""
    }
  }
  throw "The command does not invoke Cargo for resvg-capi: $Command"
}

function Assert-SelectionIdentifier {
  param([object]$Selection)

  if ($Selection.Toolchain -ceq $RequiredRelease) {
    return
  }
  if (-not [IO.Path]::IsPathRooted($Selection.Toolchain)) {
    throw "Selected toolchain '$($Selection.Toolchain)' is neither the required named release '$RequiredRelease' nor an explicit absolute path"
  }
  if (-not (Test-Path -LiteralPath $Selection.Toolchain -PathType Container)) {
    throw "Selected absolute toolchain '$($Selection.Toolchain)' does not exist"
  }
  $rustc = Join-Path $Selection.Toolchain "bin/rustc.exe"
  if (-not (Test-Path -LiteralPath $rustc -PathType Leaf)) {
    throw "Selected absolute toolchain '$($Selection.Toolchain)' has no bin/rustc.exe"
  }
}

function Get-RustcReport {
  param([object]$Selection)

  if ($Selection.Kind -eq "ambient") {
    throw "Ambient Cargo does not select Rust $RequiredRelease; refusing to probe an unrelated compiler"
  }
  if ($Selection.Kind -eq "cargo-plus") {
    # A Cargo +toolchain shim is normally installed beside rustup.exe. Probe through that
    # rustup so the report still comes from the selected compiler rather than ambient Cargo.
    $rustup = Join-Path (Split-Path -Parent $Selection.Executable) "rustup.exe"
    if (-not (Test-Path -LiteralPath $rustup -PathType Leaf)) {
      throw "The Cargo +toolchain selection has no sibling rustup.exe for a compiler probe"
    }
    $output = & $rustup run $Selection.Toolchain rustc -vV 2>&1
    $exitCode = $LASTEXITCODE
    $text = $output -join "`n"
    if ($exitCode -ne 0) {
      throw "rustup run $($Selection.Toolchain) rustc -vV failed with exit code $exitCode`: $text"
    }
    return $text
  }

  $output = & $Selection.Executable run $Selection.Toolchain rustc -vV 2>&1
  $exitCode = $LASTEXITCODE
  $text = $output -join "`n"
  if ($exitCode -ne 0) {
    throw "rustup run $($Selection.Toolchain) rustc -vV failed with exit code $exitCode`: $text"
  }
  return $text
}

function Assert-ReportedRelease {
  param(
    [string]$CompilerReport,
    [string]$ExpectedRelease = "1.99.1",
    [string]$Context = "selected rustc"
  )

  $match = [regex]::Match($CompilerReport, "(?m)^release:\s*(\S+)\s*$")
  if (-not $match.Success) {
    throw "$Context report has no 'release:' line: $CompilerReport"
  }
  $actual = $match.Groups[1].Value
  if ($actual -cne $ExpectedRelease) {
    throw "$Context reports Rust $actual, expected the reviewed release $ExpectedRelease"
  }
}

Describe "SVG Cargo toolchain pin" {
  It "parses quoted rustup and Cargo executable paths without conflating selection with release" {
    $named = Get-RustSelection '"C:\Program Files\rustup\bin\rustup.exe" run 1.99.1 cargo rustc --manifest-path C:\resvg\Cargo.toml'
    $named.Kind | Should Be "rustup-run"
    $named.Executable | Should Be "C:\Program Files\rustup\bin\rustup.exe"
    $named.Toolchain | Should Be "1.99.1"
    Assert-SelectionIdentifier $named

    $absolute = Get-RustSelection '"C:\Program Files\rustup\bin\rustup.exe" run "C:\Tools\rust 1.99.1" cargo rustc --manifest-path C:\resvg\Cargo.toml'
    $absolute.Kind | Should Be "rustup-run"
    $absolute.Toolchain | Should Be "C:\Tools\rust 1.99.1"

    $cargo = Get-RustSelection '"C:\Program Files\cargo\bin\cargo.exe" +1.99.1 rustc --manifest-path C:\resvg\Cargo.toml'
    $cargo.Kind | Should Be "cargo-plus"
    $cargo.Executable | Should Be "C:\Program Files\cargo\bin\cargo.exe"
    $cargo.Toolchain | Should Be "1.99.1"

    $unrecognised = $false
    try {
      Assert-SelectionIdentifier ([pscustomobject]@{ Toolchain = "1.98.0" })
    } catch {
      $unrecognised = $true
    }
    $unrecognised | Should Be $true
  }

  It "does not accept the ambient Cargo graph" {
    $buildFile = Join-Path $BuildDir "build.ninja"
    $command = Select-String -LiteralPath $buildFile -Pattern "^\s*COMMAND = .*resvg-capi" |
      ForEach-Object { $_.Line.Trim() } |
      Select-Object -First 1
    if (-not $command) {
      throw "No configured resvg-capi Cargo command found in $buildFile"
    }

    $selection = Get-RustSelection $command
    ($selection.Kind -eq "ambient") | Should Be $false
    Assert-SelectionIdentifier $selection
    if ($SelectedToolchain) {
      $selection.Toolchain | Should Be $SelectedToolchain
    }
  }

  It "accepts actual Rust 1.99.1 reports for both named and absolute selections and rejects 1.98" {
    $buildFile = Join-Path $BuildDir "build.ninja"
    $command = Select-String -LiteralPath $buildFile -Pattern "^\s*COMMAND = .*resvg-capi" |
      ForEach-Object { $_.Line.Trim() } |
      Select-Object -First 1
    if (-not $command) {
      throw "No configured resvg-capi Cargo command found in $buildFile"
    }
    $selection = Get-RustSelection $command
    $actualReport = Get-RustcReport $selection

    Assert-ReportedRelease $actualReport $RequiredRelease "named selection"
    Assert-ReportedRelease $actualReport $RequiredRelease "absolute-path selection"

    $mismatch = $actualReport -replace "(?m)^release:\s*\S+\s*$", "release: 1.98.0"
    $thrown = $false
    try {
      Assert-ReportedRelease $mismatch $RequiredRelease "mismatch control"
    } catch {
      $thrown = $true
    }
    $thrown | Should Be $true
  }

  It "keeps the locked release staticlib build with the upstream default features" {
    $buildFile = Join-Path $BuildDir "build.ninja"
    $command = Select-String -LiteralPath $buildFile -Pattern "^\s*COMMAND = .*resvg-capi" |
      ForEach-Object { $_.Line.Trim() } |
      Select-Object -First 1
    if (-not $command) {
      throw "No configured resvg-capi Cargo command found in $buildFile"
    }

    $command | Should Match "--locked"
    $command | Should Match "--release"
    $command | Should Match ("--target\s+" + [regex]::Escape($RustTarget))
    $command | Should Match "--crate-type\s+staticlib"
    $command | Should Match ([regex]::Escape("RUSTFLAGS=-C target-feature=+crt-static"))
    ($command -match "--no-default-features") | Should Be $false
  }
}
