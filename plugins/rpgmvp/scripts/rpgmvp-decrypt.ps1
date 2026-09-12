[CmdletBinding()]
param(
  [Parameter(Mandatory = $true, Position = 0)]
  [ValidateNotNullOrEmpty()]
  [string]$InputPath,

  [Parameter(Mandatory = $true, Position = 1)]
  [ValidateNotNullOrEmpty()]
  [string]$OutputPath,

  # Apply the inverse transformation for synthetic fixtures: prepend the RPG Maker header and a
  # disposable 16-byte encrypted-header slot, then retain the PNG from byte 16 onward.
  [switch]$Wrap
)

$ErrorActionPreference = "Stop"

[byte[]]$pngHeader = 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
                      0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52
[byte[]]$rpgmvpHeader = 0x52, 0x50, 0x47, 0x4d, 0x56, 0x00, 0x00, 0x00,
                         0x00, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00

$inputBytes = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $InputPath).Path)

if ($Wrap) {
  if ($inputBytes.Length -lt $pngHeader.Length) {
    throw "Input is shorter than the fixed 16-byte PNG header: $InputPath"
  }
  for ($index = 0; $index -lt $pngHeader.Length; $index++) {
    if ($inputBytes[$index] -ne $pngHeader[$index]) {
      throw "Input does not start with the fixed 16-byte PNG header: $InputPath"
    }
  }

  $outputBytes = [byte[]]::new($inputBytes.Length + $rpgmvpHeader.Length)
  [Array]::Copy($rpgmvpHeader, 0, $outputBytes, 0, $rpgmvpHeader.Length)
  # Bytes 16..31 are the XOR-obscured original header in a game file. They are deliberately zero
  # for synthetic fixtures because restoration never reads them and no game key is involved.
  [Array]::Copy($inputBytes, $pngHeader.Length, $outputBytes, $rpgmvpHeader.Length + $pngHeader.Length,
                $inputBytes.Length - $pngHeader.Length)
} else {
  if ($inputBytes.Length -lt ($rpgmvpHeader.Length + $pngHeader.Length)) {
    throw "Input is shorter than the 32-byte RPGMVP prefix: $InputPath"
  }
  for ($index = 0; $index -lt 8; $index++) {
    if ($inputBytes[$index] -ne $rpgmvpHeader[$index]) {
      throw "Input does not start with the RPGMV signature: $InputPath"
    }
  }

  $outputBytes = [byte[]]::new($inputBytes.Length - $rpgmvpHeader.Length)
  [Array]::Copy($pngHeader, 0, $outputBytes, 0, $pngHeader.Length)
  [Array]::Copy($inputBytes, $rpgmvpHeader.Length + $pngHeader.Length, $outputBytes, $pngHeader.Length,
                $inputBytes.Length - $rpgmvpHeader.Length - $pngHeader.Length)
}

$outputDirectory = [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($OutputPath))
[void][IO.Directory]::CreateDirectory($outputDirectory)
[IO.File]::WriteAllBytes([IO.Path]::GetFullPath($OutputPath), $outputBytes)
