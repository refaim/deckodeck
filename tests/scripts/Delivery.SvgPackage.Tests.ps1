[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$SourceRoot,

  [Parameter(Mandatory = $true)]
  [string]$BuildDir
)

$ErrorActionPreference = "Stop"

$roots = [pscustomobject]@{
  Source = $SourceRoot
  Build = $BuildDir
}

$expectedHashes = @{
  "LICENSES-resvg.txt" = "B8C9A959A777D76F37CCAE02B9FBA9C332F5DEB215993408D1700828B94FDDC4"
}
$expectedExtras = @()
$expectedPackageEntries = @("ChangeLog", "LICENSES.txt", "readme_en.txt", "readme_ru.txt", "SVG.pvd")

function Get-Sha256 {
  param([string]$Path)
  if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
    throw "Missing package artifact: $Path"
  }
  return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Assert-SvgStaging {
  param(
    [string]$PackageDir,
    [string]$Context = "SVG package"
  )

  $manifestPath = Join-Path $PackageDir "manifest.json"
  if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "$Context has no manifest.json"
  }
  $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
  $declared = @($manifest.extra_documents | Where-Object { $null -ne $_ })
  if (($declared | Sort-Object) -join "," -cne (($expectedExtras | Sort-Object) -join ",")) {
    throw "$Context manifest.extra_documents is '$($declared -join ', ')'; expected '$($expectedExtras -join ', ')'"
  }

  $license = Join-Path $PackageDir "LICENSES.txt"
  if ((Get-Sha256 $license) -cne $expectedHashes["LICENSES-resvg.txt"]) {
    throw "$Context LICENSES.txt is not the reviewed aggregate"
  }
  $unexpected = @(Get-ChildItem -LiteralPath $PackageDir -File |
    Where-Object { $_.Name -cne "manifest.json" -and $_.Name -notin @("readme_en.txt", "readme_ru.txt", "ChangeLog", "LICENSES.txt") })
  if ($unexpected.Count -ne 0) {
    throw "$Context stages unexpected documents: $($unexpected.Name -join ', ')"
  }
}

Describe "SVG license staging" {
  It "keeps the reviewed aggregate byte-identical" {
    $noticeDir = Join-Path $roots.Source "third_party/resvg"
    foreach ($name in $expectedHashes.Keys) {
      (Get-Sha256 (Join-Path $noticeDir $name)) | Should Be $expectedHashes[$name]
    }
  }

  It "stages the reviewed aggregate without separate Rust HTML notices" {
    Assert-SvgStaging (Join-Path $roots.Build "plugins/svg/package")
  }

  It "rejects a staged package with an unexpected extra notice" {
    $copy = Join-Path $TestDrive "extra-notice-package"
    New-Item -ItemType Directory -Path $copy -Force | Out-Null
    $noticeDir = Join-Path $roots.Source "third_party/resvg"
    Copy-Item -LiteralPath (Join-Path $noticeDir "LICENSES-resvg.txt") -Destination (Join-Path $copy "LICENSES.txt")
    Copy-Item -LiteralPath (Join-Path $roots.Source "plugins/svg/package/readme_en.txt") -Destination $copy
    Copy-Item -LiteralPath (Join-Path $roots.Source "plugins/svg/package/readme_ru.txt") -Destination $copy
    Copy-Item -LiteralPath (Join-Path $roots.Source "plugins/svg/package/ChangeLog") -Destination $copy
    Copy-Item -LiteralPath (Join-Path $noticeDir "RUST-COPYRIGHT-library.html") -Destination $copy
    $manifest = [ordered]@{
      name = "SVG"
      version = "1.0.0"
      architecture = "x64"
      file = "SVG.pvd"
      licenses = @("resvg")
      imports = @("KERNEL32.dll", "ntdll.dll", "ole32.dll")
      extra_documents = @()
    }
    [IO.File]::WriteAllText(
      (Join-Path $copy "manifest.json"),
      ($manifest | ConvertTo-Json -Depth 4),
      [Text.UTF8Encoding]::new($false))

    $thrown = $false
    try {
      Assert-SvgStaging $copy "negative control"
    } catch {
      $thrown = $true
    }
    $thrown | Should Be $true
  }

  It "packs exactly the plugin and the four standard documents on both architectures" {
    $buildLeaf = Split-Path -Leaf $roots.Build
    if ($buildLeaf -cnotmatch '^release(?<Suffix>.*)$') {
      throw "Cannot derive the pack suffix from build directory '$($roots.Build)'"
    }
    $suffix = $Matches["Suffix"]
    $buildRoot = Split-Path -Parent $roots.Build
    $x86Build = Join-Path $buildRoot "release-x86$suffix"
    if (-not (Test-Path -LiteralPath $x86Build -PathType Container)) {
      throw "The x86 release build for packaging is missing: $x86Build"
    }

    $dist = Join-Path $TestDrive "svg-dist"
    & (Join-Path $roots.Source "scripts/pack.ps1") `
      -Plugins svg -Suffix $suffix -DistDir $dist *>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
      throw "pack.ps1 failed with exit code $LASTEXITCODE"
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    foreach ($architecture in @("x64", "x86")) {
      $zip = Join-Path $dist "SVG-1.0.0-$architecture.zip"
      $archive = [IO.Compression.ZipFile]::OpenRead($zip)
      try {
        $entries = @($archive.Entries | ForEach-Object { $_.FullName } | Sort-Object)
      } finally {
        $archive.Dispose()
      }
      ($entries -join ",") | Should Be (($expectedPackageEntries | Sort-Object) -join ",")
    }
  }
}
