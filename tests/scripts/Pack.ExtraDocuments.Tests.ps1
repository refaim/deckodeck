[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$RepositoryRoot,

  [Parameter(Mandatory = $true)]
  [string]$SourceRoot,

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
$expectedNotices = @{
  "RUST-COPYRIGHT-library.html" = "5647BE074C8EDF7339FD863055923A8FC80BC5610A8D4661EC3B767B9D392C27"
  "RUST-COPYRIGHT.html" = "D518AE2D76BC7A1F0CBF0069B8DEAFB56EFC39C2614CAB74A859EF0B467F8373"
}

function Remove-OwnedExtraFixture {
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
  if ((Split-Path -Leaf $fullPath) -notmatch "^pvdkit-pack-extra-\d+-[0-9a-f]{32}$") {
    throw "Refusing to remove an unrecognised fixture directory: $fullPath"
  }
  if (Test-Path -LiteralPath $fullPath) {
    Remove-Item -LiteralPath $fullPath -Recurse -Force
  }
}

function New-ExtraDocumentsFixture {
  param(
    [string]$RepositoryRoot,
    [string]$SourceRoot,
    [string]$LegacyPackageInputDir,
    [string[]]$ExtraDocumentNames = @("RUST-COPYRIGHT-library.html", "RUST-COPYRIGHT.html")
  )

  $fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) (
    "pvdkit-pack-extra-$PID-$([Guid]::NewGuid().ToString('N'))")
  try {
    New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null
    $scriptsDir = Join-Path $fixtureRoot "scripts"
    New-Item -ItemType Directory -Path $scriptsDir -Force | Out-Null
    foreach ($script in @(
        "pack.ps1", "check-imports.ps1", "check-exports.ps1", "import-policy.ps1", "llvm-dir.ps1")) {
      Copy-Item -LiteralPath (Join-Path $RepositoryRoot "scripts/$script") -Destination $scriptsDir
    }

    $inputs = @(
      @{ Architecture = "x64"; Suffix = "release-extra" },
      @{ Architecture = "x86"; Suffix = "release-x86-extra" }
    )
    foreach ($input in $inputs) {
      $archive = Join-Path $LegacyPackageInputDir "RPGMVP-1.2.0-$($input.Architecture).zip"
      if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) {
        throw "Legacy release archive not found: $archive"
      }
      $archiveHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
      if ($archiveHash -ne $expectedHashes[$input.Architecture]) {
        throw "Legacy archive hash mismatch for $archive`: $archiveHash"
      }

      $extracted = Join-Path $fixtureRoot "extracted/$($input.Architecture)"
      Expand-Archive -LiteralPath $archive -DestinationPath $extracted
      $packageDir = Join-Path $fixtureRoot "build/$($input.Suffix)/plugins/rpgmvp/package"
      $pluginDir = Split-Path -Parent $packageDir
      New-Item -ItemType Directory -Path $packageDir -Force | Out-Null
      foreach ($document in @("readme_en.txt", "readme_ru.txt", "ChangeLog", "LICENSES.txt")) {
        Copy-Item -LiteralPath (Join-Path $extracted $document) -Destination $packageDir
      }
      Copy-Item -LiteralPath (Join-Path $extracted "RPGMVP.pvd") -Destination $pluginDir
      foreach ($name in $expectedNotices.Keys) {
        $sourceNotice = Join-Path $SourceRoot "third_party/resvg/$name"
        $noticeHash = (Get-FileHash -LiteralPath $sourceNotice -Algorithm SHA256).Hash
        if ($noticeHash -ne $expectedNotices[$name]) {
          throw "Reviewed notice hash mismatch for $sourceNotice`: $noticeHash"
        }
        Copy-Item -LiteralPath $sourceNotice -Destination (Join-Path $packageDir $name)
      }

      $manifest = [ordered]@{
        name = "RPGMVP"
        version = "1.2.0"
        architecture = $input.Architecture
        file = "RPGMVP.pvd"
        licenses = @("libspng", "zlib")
        imports = @("KERNEL32.dll")
        extra_documents = $ExtraDocumentNames
      }
      [IO.File]::WriteAllText(
        (Join-Path $packageDir "manifest.json"),
        ($manifest | ConvertTo-Json -Depth 4),
        [Text.UTF8Encoding]::new($false))
    }
    return $fixtureRoot
  } catch {
    Remove-OwnedExtraFixture $fixtureRoot
    throw
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

function Get-ZipEntryHash {
  param(
    [string]$Path,
    [string]$EntryName
  )

  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $archive = [IO.Compression.ZipFile]::OpenRead($Path)
  try {
    $entry = $archive.GetEntry($EntryName)
    if (-not $entry) {
      throw "$EntryName is absent from $Path"
    }
    $stream = $entry.Open()
    try {
      $sha = [Security.Cryptography.SHA256]::Create()
      try {
        return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace("-", "")
      } finally {
        $sha.Dispose()
      }
    } finally {
      $stream.Dispose()
    }
  } finally {
    $archive.Dispose()
  }
}

Describe "pack.ps1 extra manifest documents" {
  It "packages two reviewed Rust notices byte-exactly alongside the usual five entries" {
    $fixtureRoot = New-ExtraDocumentsFixture $RepositoryRoot $SourceRoot $LegacyPackageInputDir
    try {
      $distDir = Join-Path $fixtureRoot "dist"
      $exitCode = 0
      try {
        $log = & (Join-Path $fixtureRoot "scripts/pack.ps1") `
          -Suffix "-extra" -Plugins rpgmvp -DistDir $distDir 2>&1 | Out-String
      } catch {
        $exitCode = 1
        $log = (($_ | Out-String) + "`n" + ($log | Out-String)).Trim()
      }
      if ($EvidenceDir) {
        New-Item -ItemType Directory -Path $EvidenceDir -Force | Out-Null
        $log | Set-Content -LiteralPath (Join-Path $EvidenceDir "pack-extra-documents.log") -Encoding utf8
      }
      $exitCode | Should Be 0

      foreach ($architecture in @("x64", "x86")) {
        $zip = Join-Path $distDir "RPGMVP-1.2.0-$architecture.zip"
        $entries = Get-ZipEntryNames $zip
        $expectedEntries = @(
          "ChangeLog", "LICENSES.txt", "RPGMVP.pvd", "RUST-COPYRIGHT-library.html",
          "RUST-COPYRIGHT.html", "readme_en.txt", "readme_ru.txt"
        )
        ($entries -join ",") | Should Be (($expectedEntries | Sort-Object) -join ",")
        foreach ($name in $expectedNotices.Keys) {
          (Get-ZipEntryHash $zip $name) | Should Be $expectedNotices[$name]
        }
      }
    } finally {
      Remove-OwnedExtraFixture $fixtureRoot
    }
  }

  It "rejects invalid extra_documents entry <Name>" -TestCases @(
    @{ Name = "traversal"; Entries = @("../RUST-COPYRIGHT.html"); Error = "not a plain leaf name" }
    @{ Name = "absolute"; Entries = @("C:\outside-notice.html"); Error = "not a plain leaf name" }
    @{ Name = "invalid-leaf"; Entries = @("bad:name.html"); Error = "not a plain leaf name" }
    @{ Name = "plugin-collision"; Entries = @("RPGMVP.pvd"); Error = "collides" }
    @{ Name = "mandatory-collision"; Entries = @("readme_en.txt"); Error = "collides" }
    @{ Name = "manifest-collision"; Entries = @("manifest.json"); Error = "collides" }
    @{ Name = "case-insensitive-duplicate"; Entries = @("RUST-COPYRIGHT.html", "rust-copyright.html"); Error = "more than once" }
    @{ Name = "case-variant-mandatory-collision"; Entries = @("readme_EN.txt"); Error = "collides" }
    @{ Name = "trailing-dot-collision"; Entries = @("LICENSES.txt."); Error = "not a plain leaf name" }
    @{ Name = "trailing-space-collision"; Entries = @("LICENSES.txt "); Error = "not a plain leaf name" }
    @{ Name = "device-alias"; Entries = @("CON"); Error = "not a plain leaf name" }
    @{ Name = "non-ascii"; Entries = @("notice-$([char]0x00FC).html"); Error = "not a plain leaf name" }
    @{ Name = "missing-file"; Entries = @("MISSING-NOTICE.html"); Error = "was not staged" }
  ) {
    param($Name, [string[]]$Entries, [string]$Error)

    $fixtureRoot = New-ExtraDocumentsFixture `
      -RepositoryRoot $RepositoryRoot `
      -SourceRoot $SourceRoot `
      -LegacyPackageInputDir $LegacyPackageInputDir `
      -ExtraDocumentNames $Entries
    try {
      $distDir = Join-Path $fixtureRoot "dist"
      $exitCode = 0
      $log = ""
      try {
        $log = & (Join-Path $fixtureRoot "scripts/pack.ps1") `
          -Suffix "-extra" -Plugins rpgmvp -DistDir $distDir 2>&1 | Out-String
      } catch {
        $exitCode = 1
        $log = ((($_ | Out-String) + "`n" + ($log | Out-String))).Trim()
      }
      $exitCode | Should Be 1
      $log | Should Match ([regex]::Escape($Error))
      @(Get-ChildItem -LiteralPath $distDir -Filter "*.zip" -ErrorAction SilentlyContinue).Count |
        Should Be 0
    } finally {
      Remove-OwnedExtraFixture $fixtureRoot
    }
  }
}
