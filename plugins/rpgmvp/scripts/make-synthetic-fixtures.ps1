[CmdletBinding()]
param(
  [string]$Ffmpeg = "ffmpeg.exe"
)

$ErrorActionPreference = "Stop"

function Invoke-Checked {
  param(
    [string]$Command,
    [string[]]$Arguments
  )

  & $Command @Arguments
  if ($LASTEXITCODE -ne 0) {
    throw "$Command failed with exit code $LASTEXITCODE"
  }
}

$pluginDirectory = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$fixtures = Join-Path $pluginDirectory "fixtures"
$decodeFailures = Join-Path $fixtures "decode-failures"
[void][IO.Directory]::CreateDirectory($decodeFailures)
$decrypt = Join-Path $PSScriptRoot "rpgmvp-decrypt.ps1"
$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$temporary = [IO.Path]::GetFullPath((Join-Path $temporaryRoot "pvdkit-rpgmvp-fixtures-$PID"))
if (-not $temporary.StartsWith($temporaryRoot, [StringComparison]::OrdinalIgnoreCase)) {
  throw "Refusing to use a temporary directory outside $temporaryRoot"
}
[void][IO.Directory]::CreateDirectory($temporary)

try {
  $gray = Join-Path $temporary "gray8.png"
  Invoke-Checked $Ffmpeg @("-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                            "nullsrc=size=16x8,format=gray,geq=lum='16*X'", "-frames:v", "1", "-c:v", "png",
                            "-pix_fmt", "gray", $gray)
  & $decrypt -InputPath $gray -OutputPath (Join-Path $fixtures "gray8_16x8.rpgmvp") -Wrap

  $grayAlpha = Join-Path $temporary "graya8.png"
  Invoke-Checked $Ffmpeg @("-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                            "nullsrc=size=16x8,format=ya8,geq=lum='16*X':a='32*Y'", "-frames:v", "1", "-c:v",
                            "png", "-pix_fmt", "ya8", $grayAlpha)
  & $decrypt -InputPath $grayAlpha -OutputPath (Join-Path $fixtures "graya8_16x8.rpgmvp") -Wrap

  $oneBit = Join-Path $temporary "gray1.png"
  Invoke-Checked $Ffmpeg @("-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                            "nullsrc=size=16x8,format=gray,geq=lum='gte(X,8)*255'", "-frames:v", "1", "-c:v",
                            "png", "-pix_fmt", "monob", $oneBit)
  & $decrypt -InputPath $oneBit -OutputPath (Join-Path $fixtures "gray1_16x8.rpgmvp") -Wrap

  $interlaced = Join-Path $temporary "kamen-interlaced.png"
  & $decrypt -InputPath (Join-Path $fixtures "rgba8_adam7_700x700_kamen.png_") -OutputPath $interlaced
  $nonInterlaced = Join-Path $temporary "kamen-noninterlaced.png"
  Invoke-Checked $Ffmpeg @("-y", "-hide_banner", "-loglevel", "error", "-i", $interlaced, "-frames:v", "1",
                            "-c:v", "png", "-pix_fmt", "rgba", $nonInterlaced)
  & $decrypt -InputPath $nonInterlaced `
    -OutputPath (Join-Path $fixtures "rgba8_noninterlaced_700x700_kamen.rpgmvp") -Wrap

  $apng = Join-Path $temporary "two-frame.apng"
  Invoke-Checked $Ffmpeg @("-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                            "testsrc2=duration=0.2:size=4x4:rate=10", "-plays", "0", "-f", "apng", $apng)
  & $decrypt -InputPath $apng -OutputPath (Join-Path $fixtures "apng_4x4.rpgmvp") -Wrap

  [byte[]]$base = [IO.File]::ReadAllBytes((Join-Path $fixtures "rgba8_48x48.rpgmvp"))
  if ($base.Length -lt 201) {
    throw "rgba8_48x48.rpgmvp is too short to derive the negative fixtures"
  }

  # A canonical one-byte sRGB chunk (rendering intent 0, CRC AECE1CE9), inserted immediately
  # after IHDR. In the encrypted layout reconstructed PNG offset 33 is file offset 49.
  [byte[]]$srgbChunk = 0x00, 0x00, 0x00, 0x01, 0x73, 0x52, 0x47, 0x42, 0x00, 0xae, 0xce, 0x1c, 0xe9
  [byte[]]$srgb = [byte[]]::new($base.Length + $srgbChunk.Length)
  [Array]::Copy($base, 0, $srgb, 0, 49)
  [Array]::Copy($srgbChunk, 0, $srgb, 49, $srgbChunk.Length)
  [Array]::Copy($base, 49, $srgb, 49 + $srgbChunk.Length, $base.Length - 49)
  [IO.File]::WriteAllBytes((Join-Path $fixtures "rgba8_srgb_48x48.rpgmvp"), $srgb)
  [byte[]]$badSrgb = $srgb.Clone()
  $badSrgb[58] = $badSrgb[58] -bxor 0xff
  [IO.File]::WriteAllBytes((Join-Path $decodeFailures "bad_srgb_crc.rpgmvp"), $badSrgb)

  [IO.File]::WriteAllBytes((Join-Path $fixtures "stub_31.bin"), [byte[]]$base[0..30])
  [IO.File]::WriteAllBytes((Join-Path $fixtures "stub_48.bin"), [byte[]]$base[0..47])
  # Byte 200 is inside the source fixture's IDAT payload; the complete IHDR remains valid, so open
  # succeeds and decode reports the truncation.
  [IO.File]::WriteAllBytes((Join-Path $decodeFailures "truncated_idat.rpgmvp"), [byte[]]$base[0..199])

  [byte[]]$badCrc = $base.Clone()
  $badCrc[45] = $badCrc[45] -bxor 0xff # reconstructed PNG byte 29: first IHDR CRC byte
  [IO.File]::WriteAllBytes((Join-Path $fixtures "bad_ihdr_crc.rpgmvp"), $badCrc)

  [byte[]]$huge = $base.Clone()
  # Reconstructed PNG width/height = 100000 (big endian), RGBA8 unchanged. CRC32 over
  # "IHDR" + 00 01 86 A0 + 00 01 86 A0 + 08 06 00 00 00 is A8520BC8.
  [byte[]]$dimensions = 0x00, 0x01, 0x86, 0xa0, 0x00, 0x01, 0x86, 0xa0
  [Array]::Copy($dimensions, 0, $huge, 32, $dimensions.Length)
  [byte[]]$crc = 0xa8, 0x52, 0x0b, 0xc8
  [Array]::Copy($crc, 0, $huge, 45, $crc.Length)
  [IO.File]::WriteAllBytes((Join-Path $fixtures "too_large_100000x100000.rpgmvp"), $huge)
} finally {
  if (Test-Path -LiteralPath $temporary) {
    Remove-Item -LiteralPath $temporary -Recurse -Force
  }
}
