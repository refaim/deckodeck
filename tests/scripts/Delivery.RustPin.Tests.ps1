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

function Get-NinjaExecutable {
  param([string]$BuildDir)

  $cachePath = Join-Path $BuildDir "CMakeCache.txt"
  if (Test-Path -LiteralPath $cachePath -PathType Leaf) {
    $cached = Select-String -LiteralPath $cachePath -Pattern '^CMAKE_MAKE_PROGRAM:FILEPATH=(?<path>.+)$' |
      Select-Object -First 1
    if ($cached) {
      $value = $cached.Matches[0].Groups["path"].Value.Trim()
      if (Test-Path -LiteralPath $value -PathType Leaf) {
        return $value
      }
    }
  }
  $command = Get-Command ninja -ErrorAction SilentlyContinue | Select-Object -First 1
  if ($command) {
    return $command.Source
  }
  throw "No Ninja executable: $cachePath has no usable CMAKE_MAKE_PROGRAM and ninja is not on PATH"
}

function Get-NinjaCompDb {
  param([string]$BuildDir)

  $ninja = Get-NinjaExecutable $BuildDir
  # -x expands response files, so the actual archive is visible in the link command; the query
  # only reads the graph and can never compile or regenerate it.
  $output = & $ninja -C $BuildDir -t compdb -x 2>&1
  $exitCode = $LASTEXITCODE
  $text = $output -join "`n"
  if ($exitCode -ne 0) {
    throw "ninja -C $BuildDir -t compdb -x failed with exit code $exitCode`: $text"
  }
  try {
    $parsed = $text | ConvertFrom-Json
  } catch {
    throw "ninja -C $BuildDir -t compdb -x did not return JSON: $($_.Exception.Message)"
  }
  # Windows PowerShell 5.1 returns a JSON array as one nested array object; flatten it so both
  # hosts yield one element per compdb entry.
  $entries = [Collections.Generic.List[object]]::new()
  foreach ($entry in $parsed) {
    [void]$entries.Add($entry)
  }
  return $entries.ToArray()
}

function Get-CompDbBuildRoot {
  param([string]$QueryDirectory)

  $cachePath = Join-Path $QueryDirectory "CMakeCache.txt"
  if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
    throw "The Ninja graph has no CMakeCache.txt at $cachePath"
  }
  $recorded = Select-String -LiteralPath $cachePath -Pattern '^CMAKE_CACHEFILE_DIR:INTERNAL=(?<path>.+)$' |
    Select-Object -First 1
  if (-not $recorded) {
    throw "$cachePath has no CMAKE_CACHEFILE_DIR:INTERNAL entry"
  }
  $value = $recorded.Matches[0].Groups["path"].Value.Trim()
  if ([string]::IsNullOrWhiteSpace($value) -or -not [IO.Path]::IsPathRooted($value)) {
    throw "$cachePath records an invalid build root '$value'"
  }
  return [IO.Path]::GetFullPath($value)
}

function Resolve-CompDbOutput {
  param(
    [object]$Entry,
    [string]$BuildRoot
  )

  $value = $Entry.output
  if (-not [IO.Path]::IsPathRooted($value)) {
    $value = Join-Path $BuildRoot $value
  }
  return [IO.Path]::GetFullPath($value)
}

function Get-ResvgCargoEntry {
  param([object[]]$CompDb)

  $entries = @($CompDb | Where-Object {
      -not [string]::IsNullOrWhiteSpace($_.command) -and
      $_.command -match "(?i)\bresvg-capi\b" -and
      $_.command -match "(?i)cargo(?:\.exe)?\s+rustc(?:\s|$)"
    })
  if ($entries.Count -ne 1) {
    throw "Expected exactly one resvg-capi Cargo entry in the Ninja compdb, found $($entries.Count)"
  }
  $buildRoot = Get-CompDbBuildRoot $entries[0].directory
  return [pscustomobject]@{
    Directory = $buildRoot
    RawOutput = $entries[0].output
    Output = (Resolve-CompDbOutput $entries[0] $buildRoot)
    Command = $entries[0].command
  }
}

function Get-SvgLinkEntry {
  param(
    [object[]]$CompDb,
    [string]$BuildDir
  )

  $buildRoot = Get-CompDbBuildRoot $BuildDir
  $expectedOutput = [IO.Path]::GetFullPath((Join-Path $buildRoot "plugins/svg/SVG.pvd"))
  $entries = @($CompDb | Where-Object {
      -not [string]::IsNullOrWhiteSpace($_.command) -and
      ([string]::Equals(
        (Resolve-CompDbOutput $_ $buildRoot),
        $expectedOutput,
        [StringComparison]::OrdinalIgnoreCase))
    })
  if ($entries.Count -ne 1) {
    throw "Expected exactly one SVG.pvd link command in the Ninja compdb, found $($entries.Count)"
  }
  return [pscustomobject]@{
    Output = (Resolve-CompDbOutput $entries[0] $buildRoot)
    Command = $entries[0].command
  }
}

function Test-CommandContainsPath {
  param(
    [string]$Command,
    [string]$Path
  )

  $normalizedCommand = $Command.Replace('\', '/')
  $normalizedPath = $Path.Replace('\', '/')
  # A path argument boundary: Ninja-expanded commands quote or space-separate their arguments,
  # so a substring of a different path cannot satisfy the assertion.
  $pattern = '(^|[\s"])' + [regex]::Escape($normalizedPath) + '($|[\s"])'
  return [regex]::IsMatch($normalizedCommand, $pattern, [Text.RegularExpressions.RegexOptions]::IgnoreCase)
}

function Get-PathRelativeTo {
  param(
    [string]$BaseDirectory,
    [string]$Path
  )

  $baseUri = [Uri]::new(([IO.Path]::GetFullPath($BaseDirectory).TrimEnd('\') + '\'))
  $pathUri = [Uri]::new([IO.Path]::GetFullPath($Path))
  return [Uri]::UnescapeDataString($baseUri.MakeRelativeUri($pathUri).ToString()).Replace('/', '\')
}

function Get-ExpectedProductionArchive {
  param(
    [object]$Entry,
    [string]$RustTarget
  )

  $targetDir = [regex]::Match($Entry.Command, "(?i)--target-dir\s+(?<value>""[^""]+""|\S+)")
  if (-not $targetDir.Success) {
    throw "The resvg-capi command has no --target-dir: $($Entry.Command)"
  }
  $target = [regex]::Match($Entry.Command, "(?i)--target\s+(?<value>""[^""]+""|\S+)")
  if (-not $target.Success) {
    throw "The resvg-capi command has no --target: $($Entry.Command)"
  }
  # The compdb command is already expanded: the value is a real path, not a Ninja-escaped one.
  $targetDirValue = $targetDir.Groups["value"].Value.Trim('"')
  $targetValue = $target.Groups["value"].Value.Trim('"')
  if ($targetValue -cne $RustTarget) {
    throw "The resvg-capi command targets '$targetValue', expected '$RustTarget'"
  }
  if (-not [IO.Path]::IsPathRooted($targetDirValue)) {
    $targetDirValue = Join-Path $Entry.Directory $targetDirValue
  }
  return [IO.Path]::GetFullPath((Join-Path (Join-Path $targetDirValue $targetValue) "production\resvg.lib"))
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
    $entry = Get-ResvgCargoEntry @(Get-NinjaCompDb $BuildDir)
    $selection = Get-RustSelection $entry.Command
    ($selection.Kind -eq "ambient") | Should Be $false
    Assert-SelectionIdentifier $selection
    if ($SelectedToolchain) {
      $selection.Toolchain | Should Be $SelectedToolchain
    }
  }

  It "accepts actual Rust 1.99.1 reports for both named and absolute selections and rejects 1.98" {
    $entry = Get-ResvgCargoEntry @(Get-NinjaCompDb $BuildDir)
    $selection = Get-RustSelection $entry.Command
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

  It "keeps the locked production staticlib build with the upstream default features" {
    $entry = Get-ResvgCargoEntry @(Get-NinjaCompDb $BuildDir)
    $command = $entry.Command

    $command | Should Match "--locked"
    # Upstream resvg 0.48.1 declares [profile.production] as a release-derived LTO profile;
    # Cargo selects it by name, and the archive lands under the profile's directory.
    $command | Should Match "--profile\s+production(\s|$)"
    ($command -match "\s--release(\s|$)") | Should Be $false
    $command | Should Match ("--target\s+" + [regex]::Escape($RustTarget))
    $command | Should Match "--crate-type\s+staticlib"
    $command | Should Match ([regex]::Escape("RUSTFLAGS=-C target-feature=+crt-static"))
    ($command -match "--no-default-features") | Should Be $false
  }

  It "selects the named production profile without a release or size fallback" {
    $entry = Get-ResvgCargoEntry @(Get-NinjaCompDb $BuildDir)

    $entry.Command | Should Match "--profile\s+production(\s|$)"
    ($entry.Command -match "(?i)--profile\s+(release|dev|test|bench)(\s|$)") | Should Be $false
    ($entry.Command -match "(?i)\s--release(\s|$)") | Should Be $false
  }

  It "routes the production staticlib into the actual link graph without a release archive" {
    $compdb = @(Get-NinjaCompDb $BuildDir)
    $cargo = Get-ResvgCargoEntry $compdb
    $expected = Get-ExpectedProductionArchive $cargo $RustTarget

    ([string]::Equals($cargo.Output, $expected, [StringComparison]::OrdinalIgnoreCase)) | Should Be $true
    ($cargo.Output -match "(?i)[\\/]release[\\/]resvg\.lib$") | Should Be $false
    # compdb reports the explicit output only; a CMake implicit alias separated by '|' or written
    # with ${cmake_ninja_workdir} is never part of it.
    ($cargo.RawOutput -match '\||\$\{') | Should Be $false

    $link = Get-SvgLinkEntry $compdb $BuildDir
    # Inside the build tree CMake's command refers to the archive relative to the build
    # directory; an external target directory is referred to absolutely.
    $relative = Get-PathRelativeTo $cargo.Directory $expected
    ((Test-CommandContainsPath $link.Command $expected) -or
      (Test-CommandContainsPath $link.Command $relative)) | Should Be $true
    ($link.Command -match "(?i)[\\/]release[\\/]resvg\.lib") | Should Be $false
  }

  It "resolves a relocated metadata-only Ninja graph from its recorded build root" {
    $metadataCopy = Join-Path $TestDrive "metadata copy `$ graph"
    $metadataFiles = Join-Path $metadataCopy "CMakeFiles"
    $null = New-Item -ItemType Directory -Path $metadataFiles -Force
    Copy-Item -LiteralPath (Join-Path $BuildDir "build.ninja") `
      -Destination (Join-Path $metadataCopy "build.ninja")
    Copy-Item -LiteralPath (Join-Path $BuildDir "CMakeCache.txt") `
      -Destination (Join-Path $metadataCopy "CMakeCache.txt")
    Copy-Item -LiteralPath (Join-Path $BuildDir "CMakeFiles/rules.ninja") `
      -Destination (Join-Path $metadataFiles "rules.ninja")

    # Ninja records the directory named by -C, not the build root retained in CMakeCache.txt.
    # Paths in the archived graph must therefore resolve from the cache's recorded origin.
    $compdb = @(Get-NinjaCompDb $metadataCopy)
    $cargo = Get-ResvgCargoEntry $compdb
    $expected = Get-ExpectedProductionArchive $cargo $RustTarget

    ([string]::Equals($cargo.Output, $expected, [StringComparison]::OrdinalIgnoreCase)) | Should Be $true
    ([string]::Equals(
      $cargo.Directory,
      [IO.Path]::GetFullPath($metadataCopy),
      [StringComparison]::OrdinalIgnoreCase)) | Should Be $false

    $link = Get-SvgLinkEntry $compdb $metadataCopy
    $relative = Get-PathRelativeTo $cargo.Directory $expected
    ((Test-CommandContainsPath $link.Command $expected) -or
      (Test-CommandContainsPath $link.Command $relative)) | Should Be $true
    ($link.Command -match "(?i)[\\/]release[\\/]resvg\.lib") | Should Be $false
  }
}
