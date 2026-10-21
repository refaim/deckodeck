[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$SourceRoot,

  [Parameter(Mandatory = $true)]
  [string]$BuildDir
)

$ErrorActionPreference = "Stop"

$expectedHashes = @{
  "LICENSES-resvg.txt" = "195E6D92BC40D6276DA2722A160D5973967CCA14F06B904EF66022E68C536518"
  "RUST-COPYRIGHT-library.html" = "5647BE074C8EDF7339FD863055923A8FC80BC5610A8D4661EC3B767B9D392C27"
  "RUST-COPYRIGHT.html" = "D518AE2D76BC7A1F0CBF0069B8DEAFB56EFC39C2614CAB74A859EF0B467F8373"
}
$expectedExtras = @("RUST-COPYRIGHT-library.html", "RUST-COPYRIGHT.html")

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
  foreach ($name in $expectedExtras) {
    $path = Join-Path $PackageDir $name
    if ((Get-Sha256 $path) -cne $expectedHashes[$name]) {
      throw "$Context $name is not byte-identical to the reviewed notice"
    }
  }
}

Describe "SVG license staging" {
  It "keeps the reviewed aggregate and Rust notice artifacts byte-identical" {
    $noticeDir = Join-Path $SourceRoot "third_party/resvg"
    foreach ($name in $expectedHashes.Keys) {
      (Get-Sha256 (Join-Path $noticeDir $name)) | Should Be $expectedHashes[$name]
    }
  }

  It "stages the reviewed aggregate and declares and stages both Rust notices" {
    Assert-SvgStaging (Join-Path $BuildDir "plugins/svg/package")
  }

  It "rejects a manifest that declares a notice missing from the staged package" {
    $copy = Join-Path $TestDrive "missing-notice-package"
    New-Item -ItemType Directory -Path $copy -Force | Out-Null
    $noticeDir = Join-Path $SourceRoot "third_party/resvg"
    Copy-Item -LiteralPath (Join-Path $noticeDir "LICENSES-resvg.txt") -Destination (Join-Path $copy "LICENSES.txt")
    foreach ($name in $expectedExtras) {
      Copy-Item -LiteralPath (Join-Path $noticeDir $name) -Destination (Join-Path $copy $name)
    }
    $manifest = [ordered]@{
      name = "SVG"
      version = "1.0.0"
      architecture = "x64"
      file = "SVG.pvd"
      licenses = @("resvg")
      imports = @("KERNEL32.dll", "ntdll.dll", "ole32.dll")
      extra_documents = $expectedExtras
    }
    [IO.File]::WriteAllText(
      (Join-Path $copy "manifest.json"),
      ($manifest | ConvertTo-Json -Depth 4),
      [Text.UTF8Encoding]::new($false))
    Remove-Item -LiteralPath (Join-Path $copy "RUST-COPYRIGHT.html") -Force

    $thrown = $false
    try {
      Assert-SvgStaging $copy "negative control"
    } catch {
      $thrown = $true
    }
    $thrown | Should Be $true
  }
}
