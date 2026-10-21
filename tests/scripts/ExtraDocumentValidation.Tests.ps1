[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$SourceRoot
)

$ErrorActionPreference = "Stop"

function Invoke-ExtraLeafValidation {
  param(
    [string]$Leaf,
    [string]$Reserved = "",
    [string]$Seen = ""
  )

  $helper = (Join-Path $SourceRoot "cmake/pvdkit-plugin.cmake").Replace("\", "/")
  $script = Join-Path $TestDrive ("extra-leaf-" + [Guid]::NewGuid().ToString("N") + ".cmake")
  $content = @"
include("$helper")
pvdkit_validate_extra_document_leaf("$Leaf" "$Reserved" "$Seen" key)
message(STATUS "key=`${key}")
"@
  [IO.File]::WriteAllText($script, $content, [Text.UTF8Encoding]::new($false))
  $previous = $ErrorActionPreference
  try {
    $ErrorActionPreference = "Continue"
    $output = & cmake -P $script 2>&1
  } finally {
    $ErrorActionPreference = $previous
  }
  return [pscustomobject]@{
    ExitCode = $LASTEXITCODE
    Output = ($output -join "`n")
  }
}

Describe "CMake extra-document leaf policy" {
  It "accepts the reviewed ASCII Rust notice leaf" {
    $result = Invoke-ExtraLeafValidation -Leaf "RUST-COPYRIGHT.html"
    $result.ExitCode | Should Be 0
    $result.Output | Should Match "key=rust-copyright\.html"
  }

  It "rejects invalid configure-time extra leaf <Name>" -TestCases @(
    @{ Name = "case-variant-mandatory"; Leaf = "readme_EN.txt"; Reserved = "readme_en.txt"; Seen = "" }
    @{ Name = "trailing-dot"; Leaf = "LICENSES.txt."; Reserved = "licenses.txt"; Seen = "" }
    @{ Name = "trailing-space"; Leaf = "LICENSES.txt "; Reserved = "licenses.txt"; Seen = "" }
    @{ Name = "duplicate-case-variant"; Leaf = "RUST-COPYRIGHT.html"; Reserved = ""; Seen = "rust-copyright.html" }
    @{ Name = "device-alias"; Leaf = "CON"; Reserved = ""; Seen = "" }
    @{ Name = "non-ascii"; Leaf = "notice-$([char]0x00FC).html"; Reserved = ""; Seen = "" }
  ) {
    param($Name, $Leaf, $Reserved, $Seen)

    $result = Invoke-ExtraLeafValidation -Leaf $Leaf -Reserved $Reserved -Seen $Seen
    $result.ExitCode | Should Not Be 0
    $result.Output | Should Match "extra document|EXTRA_DOCUMENTS"
  }
}
