[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$RepositoryRoot,

  # Directory containing RPGMVP-1.2.0-x64.zip and RPGMVP-1.2.0-x86.zip from the public release.
  [Parameter(Mandatory = $true)]
  [string]$LegacyPackageInputDir,

  [string]$EvidenceDir = ""
)

$ErrorActionPreference = "Stop"

$expectedHashes = @{
  "x64" = "4E3A9860C3D9F54B6563DC38F35C1186487AA75F617E805A2DC78F631E0E80E3"
  "x86" = "6346407CF82A281CA916ACDAE880A13D18FA5AD9E4386A8FD42556186E23BB11"
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
  if ($leaf -notmatch "^pvdkit-pack-legacy-(missing|explicit|empty)-\d+-[0-9a-f]{32}$") {
    throw "Refusing to remove an unrecognised fixture directory: $fullPath"
  }
  if (Test-Path -LiteralPath $fullPath) {
    Remove-Item -LiteralPath $fullPath -Recurse -Force
  }
}

function New-LegacyPackFixture {
  param(
    [string]$RepositoryRoot,
    [string]$LegacyPackageInputDir,
    [string]$ManifestCase
  )

  $fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) (
    "pvdkit-pack-legacy-$ManifestCase-$PID-$( [Guid]::NewGuid().ToString('N') )")
  try {
    New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null

    $scriptsDir = Join-Path $fixtureRoot "scripts"
    New-Item -ItemType Directory -Path $scriptsDir -Force | Out-Null
    foreach ($script in @(
        "pack.ps1", "check-imports.ps1", "check-exports.ps1", "import-policy.ps1", "llvm-dir.ps1")) {
      Copy-Item -LiteralPath (Join-Path $RepositoryRoot "scripts\$script") -Destination $scriptsDir
    }

    $inputs = @(
      @{ Architecture = "x64"; Suffix = "release-legacy" },
      @{ Architecture = "x86"; Suffix = "release-x86-legacy" }
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
      $packageDir = Join-Path $fixtureRoot "build\$($input.Suffix)\plugins\rpgmvp\package"
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

function Invoke-LegacyPack {
  param([string]$FixtureRoot)

  $distDir = Join-Path $FixtureRoot "dist"
  $exitCode = 0
  $log = ""
  try {
    $log = & (Join-Path $FixtureRoot "scripts\pack.ps1") `
      -Suffix "-legacy" -Plugins rpgmvp -DistDir $distDir 2>&1 | Out-String
  } catch {
    $exitCode = 1
    $log = ((($_ | Out-String) + "`n" + ($log | Out-String))).Trim()
  }
  return [pscustomobject]@{
    ExitCode = $exitCode
    Log = $log
    DistDir = $distDir
  }
}

function Get-ZipEntryNames {
  param([string]$Path)
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $archive = [IO.Compression.ZipFile]::OpenRead($Path)
  try {
    return @($archive.Entries | ForEach-Object FullName | Sort-Object)
  } finally {
    $archive.Dispose()
  }
}

Describe "pack.ps1 legacy manifest import policy" {
  It "packages a manifest with no imports property using the KERNEL32.dll default" {
    $fixtureRoot = New-LegacyPackFixture $RepositoryRoot $LegacyPackageInputDir "missing"
    try {
      $result = Invoke-LegacyPack $fixtureRoot
      if ($EvidenceDir) {
        New-Item -ItemType Directory -Path $EvidenceDir -Force | Out-Null
        $result.Log | Set-Content -LiteralPath (Join-Path $EvidenceDir "pack-missing-imports.log") -Encoding utf8
      }
      $result.ExitCode | Should Be 0
      Test-Path -LiteralPath (Join-Path $result.DistDir "RPGMVP-1.2.0-x64.zip") |
        Should Be $true
      Test-Path -LiteralPath (Join-Path $result.DistDir "RPGMVP-1.2.0-x86.zip") |
        Should Be $true
    } finally {
      Remove-OwnedFixture $fixtureRoot
    }
  }

  It "keeps an explicit KERNEL32.dll imports array" {
    $fixtureRoot = New-LegacyPackFixture $RepositoryRoot $LegacyPackageInputDir "explicit"
    try {
      $result = Invoke-LegacyPack $fixtureRoot
      if ($EvidenceDir) {
        New-Item -ItemType Directory -Path $EvidenceDir -Force | Out-Null
        $result.Log | Set-Content -LiteralPath (Join-Path $EvidenceDir "pack-explicit-imports.log") -Encoding utf8
      }
      $result.ExitCode | Should Be 0
      @(Get-ChildItem -LiteralPath $result.DistDir -Filter "RPGMVP-1.2.0-*.zip").Count |
        Should Be 2
      $zip = Join-Path $result.DistDir "RPGMVP-1.2.0-x64.zip"
      (Get-ZipEntryNames $zip) -join "," | Should Be "ChangeLog,LICENSES.txt,readme_en.txt,readme_ru.txt,RPGMVP.pvd"
    } finally {
      Remove-OwnedFixture $fixtureRoot
    }
  }

  It "packages an explicitly empty imports array using the KERNEL32.dll default" {
    $fixtureRoot = New-LegacyPackFixture $RepositoryRoot $LegacyPackageInputDir "empty"
    try {
      $result = Invoke-LegacyPack $fixtureRoot
      if ($EvidenceDir) {
        New-Item -ItemType Directory -Path $EvidenceDir -Force | Out-Null
        $result.Log | Set-Content -LiteralPath (Join-Path $EvidenceDir "pack-empty-imports.log") -Encoding utf8
      }
      $result.ExitCode | Should Be 0
      @(Get-ChildItem -LiteralPath $result.DistDir -Filter "RPGMVP-1.2.0-*.zip").Count |
        Should Be 2
    } finally {
      Remove-OwnedFixture $fixtureRoot
    }
  }
}
