[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$RepositoryRoot,

  [Parameter(Mandatory = $true)]
  [string]$LegacyPackageInputDir,

  [string]$EvidenceDir = ""
)

$ErrorActionPreference = "Stop"

$expectedHashes = @{
  "x64" = "4E3A9860C3D9F54B6563DC38F35C1186487AA75F617E805A2DC78F631E0E80E3"
  "x86" = "6346407CF82A281CA916ACDAE880A13D18FA5AD9E4386A8FD42556186E23BB11"
}

function Get-CMakeCommandSnapshot {
  $command = Get-Command cmake -ErrorAction SilentlyContinue
  if (-not $command) {
    return [pscustomobject]@{
      Present = $false
      CommandType = ""
      Source = ""
      Path = ""
      Definition = ""
    }
  }
  return [pscustomobject]@{
    Present = $true
    CommandType = [string]$command.CommandType
    Source = [string]$command.Source
    Path = [string]$command.Path
    Definition = [string]$command.Definition
  }
}

function Assert-CMakeCommandSnapshot {
  param([object]$Before)

  $after = Get-CMakeCommandSnapshot
  foreach ($property in @("Present", "CommandType", "Source", "Path", "Definition")) {
    if ($after.$property -cne $Before.$property) {
      throw "Get-Command cmake changed after the mocked call: $property was '$($Before.$property)', now '$($after.$property)'"
    }
  }
}

function Remove-OwnedFixture {
  param([string]$Path)

  if (-not $Path) {
    return
  }
  $fullPath = [IO.Path]::GetFullPath($Path)
  $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd(
    [IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
  $prefix = $tempRoot + [IO.Path]::DirectorySeparatorChar
  if (-not $fullPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to remove fixture outside the temp root: $fullPath"
  }
  $leaf = Split-Path -Leaf $fullPath
  if ($leaf -notmatch "^pvdkit-build-all-(missing|explicit|empty)-\d+-[0-9a-f]{32}$") {
    throw "Refusing to remove an unrecognised fixture directory: $fullPath"
  }
  if (Test-Path -LiteralPath $fullPath) {
    Remove-Item -LiteralPath $fullPath -Recurse -Force
  }
}

function New-LegacyBuildFixture {
  param(
    [string]$RepositoryRoot,
    [string]$LegacyPackageInputDir,
    [string]$ManifestCase
  )

  $fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) (
    "pvdkit-build-all-$ManifestCase-$PID-$( [Guid]::NewGuid().ToString('N') )")
  try {
    New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null

    $scriptsDir = Join-Path $fixtureRoot "scripts"
    New-Item -ItemType Directory -Path $scriptsDir -Force | Out-Null
    foreach ($script in @(
        "build-all.ps1", "check-imports.ps1", "check-exports.ps1", "import-policy.ps1", "llvm-dir.ps1")) {
      Copy-Item -LiteralPath (Join-Path $RepositoryRoot "scripts\$script") -Destination $scriptsDir
    }

    $inputs = @(
      @{ Architecture = "x64"; Preset = "release" },
      @{ Architecture = "x86"; Preset = "release-x86" }
    )
    foreach ($input in $inputs) {
      $archive = Join-Path $LegacyPackageInputDir "RPGMVP-1.2.0-$($input.Architecture).zip"
      if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) {
        throw "Legacy release archive not found: $archive"
      }
      $actualHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
      if ($actualHash -ne $expectedHashes[$input.Architecture]) {
        throw "Legacy archive hash mismatch for $archive`: $actualHash"
      }

      $extracted = Join-Path $fixtureRoot "extracted\$($input.Architecture)"
      Expand-Archive -LiteralPath $archive -DestinationPath $extracted
      $packageDir = Join-Path $fixtureRoot "build\$($input.Preset)-legacy\plugins\rpgmvp\package"
      $pluginDir = Split-Path -Parent $packageDir
      New-Item -ItemType Directory -Path $packageDir -Force | Out-Null
      foreach ($document in @("readme_en.txt", "readme_ru.txt", "ChangeLog", "LICENSES.txt")) {
        Copy-Item -LiteralPath (Join-Path $extracted $document) -Destination $packageDir
      }
      Copy-Item -LiteralPath (Join-Path $extracted "RPGMVP.pvd") -Destination $pluginDir

      $manifest = [ordered]@{
        name = "RPGMVP"
        version = "1.2.0"
        architecture = $input.Architecture
        file = "RPGMVP.pvd"
        licenses = @("libspng", "zlib")
      }
      switch ($ManifestCase) {
        "missing" { }
        "explicit" { $manifest.imports = @("KERNEL32.dll") }
        "empty" { $manifest.imports = @() }
        default { throw "Unknown manifest case '$ManifestCase'" }
      }
      $manifestPath = Join-Path $packageDir "manifest.json"
      [IO.File]::WriteAllText(
        $manifestPath,
        ($manifest | ConvertTo-Json -Depth 4),
        [Text.UTF8Encoding]::new($false))
    }

    return $fixtureRoot
  } catch {
    Remove-OwnedFixture $fixtureRoot
    throw
  }
}

function Invoke-LegacyBuildAll {
  param([string]$FixtureRoot)

  $exitCode = 0
  $log = ""
  try {
    # Dot-source so the Pester cmake mock is visible to the script's command lookup.
    $log = . (Join-Path $FixtureRoot "scripts\build-all.ps1") -Suffix "-legacy" 2>&1 | Out-String
  } catch {
    $exitCode = 1
    $log = ((($_ | Out-String) + "`n" + ($log | Out-String))).Trim()
  }
  return [pscustomobject]@{
    ExitCode = $exitCode
    Log = $log
    FixtureRoot = $FixtureRoot
  }
}

$script:cmakeBeforeAllTests = Get-CMakeCommandSnapshot
$script:cmakePreviousFunction = Get-Item Function:\cmake -ErrorAction SilentlyContinue
$script:cmakePreviousScriptBlock = if ($script:cmakePreviousFunction) {
  $script:cmakePreviousFunction.ScriptBlock
} else {
  $null
}
$script:cmakePreviousOptions = if ($script:cmakePreviousFunction) {
  $script:cmakePreviousFunction.Options
} else {
  $null
}

Describe "build-all.ps1 legacy manifest import policy" {
  Context "mocked legacy manifest cases" {
    BeforeAll {
      # Pester 3.4 mocks commands, not native applications. A script-scoped placeholder lets
      # Pester replace and then remove cmake without leaving a global function behind.
      function script:cmake { }
      Mock cmake {
        $global:LASTEXITCODE = 0
      } -Scope Context
    }

    It "builds and checks a manifest with no imports property using the KERNEL32.dll default" {
      $fixtureRoot = $null
      try {
        $fixtureRoot = New-LegacyBuildFixture $RepositoryRoot $LegacyPackageInputDir "missing"
        $result = Invoke-LegacyBuildAll $fixtureRoot
        if ($EvidenceDir) {
          New-Item -ItemType Directory -Path $EvidenceDir -Force | Out-Null
          $result.Log | Set-Content -LiteralPath (Join-Path $EvidenceDir "build-all-missing-imports.log") -Encoding utf8
        }
        $result.ExitCode | Should Be 0
        Test-Path -LiteralPath (Join-Path $fixtureRoot "dist\x64\RPGMVP.pvd") | Should Be $true
        Test-Path -LiteralPath (Join-Path $fixtureRoot "dist\x86\RPGMVP.pvd") | Should Be $true
      } finally {
        if ($fixtureRoot) {
          Remove-OwnedFixture $fixtureRoot
        }
      }
    }

    It "keeps an explicit KERNEL32.dll imports array" {
      $fixtureRoot = $null
      try {
        $fixtureRoot = New-LegacyBuildFixture $RepositoryRoot $LegacyPackageInputDir "explicit"
        $result = Invoke-LegacyBuildAll $fixtureRoot
        if ($EvidenceDir) {
          New-Item -ItemType Directory -Path $EvidenceDir -Force | Out-Null
          $result.Log | Set-Content -LiteralPath (Join-Path $EvidenceDir "build-all-explicit-imports.log") -Encoding utf8
        }
        $result.ExitCode | Should Be 0
        Test-Path -LiteralPath (Join-Path $fixtureRoot "dist\x64\RPGMVP.pvd") | Should Be $true
        Test-Path -LiteralPath (Join-Path $fixtureRoot "dist\x86\RPGMVP.pvd") | Should Be $true
      } finally {
        if ($fixtureRoot) {
          Remove-OwnedFixture $fixtureRoot
        }
      }
    }

    It "builds an explicitly empty imports array using the KERNEL32.dll default" {
      $fixtureRoot = $null
      try {
        $fixtureRoot = New-LegacyBuildFixture $RepositoryRoot $LegacyPackageInputDir "empty"
        $result = Invoke-LegacyBuildAll $fixtureRoot
        if ($EvidenceDir) {
          New-Item -ItemType Directory -Path $EvidenceDir -Force | Out-Null
          $result.Log | Set-Content -LiteralPath (Join-Path $EvidenceDir "build-all-empty-imports.log") -Encoding utf8
        }
        $result.ExitCode | Should Be 0
        Test-Path -LiteralPath (Join-Path $fixtureRoot "dist\x64\RPGMVP.pvd") | Should Be $true
        Test-Path -LiteralPath (Join-Path $fixtureRoot "dist\x86\RPGMVP.pvd") | Should Be $true
      } finally {
        if ($fixtureRoot) {
          Remove-OwnedFixture $fixtureRoot
        }
      }
    }

    It "keeps the mock usable after a failed fixture body" {
      $failed = $false
      try {
        throw "intentional fixture failure"
      } catch {
        $_.Exception.Message | Should Be "intentional fixture failure"
        $failed = $true
      }
      $failed | Should Be $true
    (Get-Command cmake).CommandType | Should Be "Function"
    }
  }

}

# Describe returns after its tests and mock teardown. Remove the restored placeholder (or put
# back the pre-existing function), then register a fresh isolation test against that state.
Remove-Item Function:\cmake -Force -ErrorAction SilentlyContinue
if ($script:cmakePreviousFunction) {
  Set-Item Function:\cmake `
    -Value $script:cmakePreviousScriptBlock -Options $script:cmakePreviousOptions
}

Describe "build-all cmake mock isolation" {
  It "restores Get-Command cmake type, path and definition" {
    Assert-CMakeCommandSnapshot $script:cmakeBeforeAllTests
  }
}
