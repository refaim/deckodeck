[CmdletBinding()]
param(
  [string]$Ffmpeg = "ffmpeg.exe",
  [string]$ExifTool = "exiftool.exe"
)

$ErrorActionPreference = "Stop"

if (-not (Get-Command $Ffmpeg -ErrorAction SilentlyContinue)) {
  throw "$Ffmpeg must be available on PATH or supplied through -Ffmpeg"
}
if (-not (Get-Command $ExifTool -ErrorAction SilentlyContinue)) {
  throw "$ExifTool must be available on PATH or supplied through -ExifTool"
}

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

function Get-BigEndianUInt32 {
  param(
    [byte[]]$Bytes,
    [int]$Offset
  )

  return [uint32](([uint32]$Bytes[$Offset] -shl 24) -bor
                    ([uint32]$Bytes[$Offset + 1] -shl 16) -bor
                    ([uint32]$Bytes[$Offset + 2] -shl 8) -bor
                    [uint32]$Bytes[$Offset + 3])
}

function Write-BigEndianUInt16 {
  param(
    [byte[]]$Bytes,
    [int]$Offset,
    [uint16]$Value
  )

  $Bytes[$Offset] = [byte](($Value -shr 8) -band 0xff)
  $Bytes[$Offset + 1] = [byte]($Value -band 0xff)
}

function Write-BigEndianUInt32 {
  param(
    [byte[]]$Bytes,
    [int]$Offset,
    [uint32]$Value
  )

  $Bytes[$Offset] = [byte](($Value -shr 24) -band 0xff)
  $Bytes[$Offset + 1] = [byte](($Value -shr 16) -band 0xff)
  $Bytes[$Offset + 2] = [byte](($Value -shr 8) -band 0xff)
  $Bytes[$Offset + 3] = [byte]($Value -band 0xff)
}

function Write-Ascii {
  param(
    [byte[]]$Bytes,
    [int]$Offset,
    [string]$Text
  )

  [byte[]]$encoded = [Text.Encoding]::ASCII.GetBytes($Text)
  [Array]::Copy($encoded, 0, $Bytes, $Offset, $encoded.Length)
}

function Get-AlignedLength {
  param([int]$Length)

  return (($Length + 3) -shr 2) -shl 2
}

function ConvertTo-S15Fixed16 {
  param([double]$Value)

  return [uint32][Math]::Round($Value * 65536.0, [MidpointRounding]::AwayFromZero)
}

function Get-IccXyzTag {
  param([double[]]$Values)

  if ($Values.Length -ne 3) {
    throw "An ICC XYZ tag needs exactly three values"
  }
  [byte[]]$bytes = [byte[]]::new(20)
  Write-Ascii $bytes 0 "XYZ "
  for ($index = 0; $index -lt $Values.Length; ++$index) {
    Write-BigEndianUInt32 $bytes (8 + $index * 4) (ConvertTo-S15Fixed16 $Values[$index])
  }
  return ,$bytes
}

function Get-IccDescriptionTag {
  param([string]$Text)

  [byte[]]$ascii = [Text.Encoding]::ASCII.GetBytes($Text + [char]0)
  # ICC v2 textDescription: ASCII text, empty Unicode text, and an empty 67-byte Macintosh field.
  [byte[]]$bytes = [byte[]]::new(12 + $ascii.Length + 4 + 4 + 2 + 1 + 67)
  Write-Ascii $bytes 0 "desc"
  Write-BigEndianUInt32 $bytes 8 ([uint32]$ascii.Length)
  [Array]::Copy($ascii, 0, $bytes, 12, $ascii.Length)
  return ,$bytes
}

function Get-IccTextTag {
  param([string]$Text)

  [byte[]]$ascii = [Text.Encoding]::ASCII.GetBytes($Text + [char]0)
  [byte[]]$bytes = [byte[]]::new(8 + $ascii.Length)
  Write-Ascii $bytes 0 "text"
  [Array]::Copy($ascii, 0, $bytes, 8, $ascii.Length)
  return ,$bytes
}

function Get-IccSrgbCurveTag {
  # A sampled 1024-entry curve is used instead of a gamma approximation. It maps encoded sRGB
  # values to linear-light values with IEC 61966-2-1's piecewise transfer function.
  $entryCount = 1024
  [byte[]]$bytes = [byte[]]::new(12 + $entryCount * 2)
  Write-Ascii $bytes 0 "curv"
  Write-BigEndianUInt32 $bytes 8 $entryCount
  for ($index = 0; $index -lt $entryCount; ++$index) {
    $encoded = $index / ($entryCount - 1.0)
    $linear = if ($encoded -le 0.04045) {
      $encoded / 12.92
    } else {
      [Math]::Pow(($encoded + 0.055) / 1.055, 2.4)
    }
    $sample = [uint16][Math]::Round($linear * 65535.0, [MidpointRounding]::AwayFromZero)
    Write-BigEndianUInt16 $bytes (12 + $index * 2) $sample
  }
  return ,$bytes
}

function Get-PvdkitIccProfile {
  [byte[]]$description = Get-IccDescriptionTag "pvdkit sRGB-equivalent test profile"
  [byte[]]$copyright = Get-IccTextTag "Copyright (c) 2026 Roman Kharitonov, MIT"
  [byte[]]$whitePoint = Get-IccXyzTag @(0.9642, 1.0, 0.8249)
  [byte[]]$red = Get-IccXyzTag @(0.4361, 0.2225, 0.0139)
  [byte[]]$green = Get-IccXyzTag @(0.3851, 0.7169, 0.0971)
  [byte[]]$blue = Get-IccXyzTag @(0.1431, 0.0606, 0.7141)
  [byte[]]$curve = Get-IccSrgbCurveTag

  $tagCount = 9
  $descriptionOffset = 128 + 4 + $tagCount * 12
  $copyrightOffset = $descriptionOffset + (Get-AlignedLength $description.Length)
  $whitePointOffset = $copyrightOffset + (Get-AlignedLength $copyright.Length)
  $redOffset = $whitePointOffset + (Get-AlignedLength $whitePoint.Length)
  $greenOffset = $redOffset + (Get-AlignedLength $red.Length)
  $blueOffset = $greenOffset + (Get-AlignedLength $green.Length)
  $curveOffset = $blueOffset + (Get-AlignedLength $blue.Length)
  $profileSize = $curveOffset + (Get-AlignedLength $curve.Length)
  [byte[]]$profile = [byte[]]::new($profileSize)

  # Deterministic ICC v2.1 display-profile header. All unspecified fields, including CMM, platform,
  # flags, device identifiers, profile creator, profile ID and reserved bytes, remain zero.
  Write-BigEndianUInt32 $profile 0 $profileSize
  Write-BigEndianUInt32 $profile 8 0x02100000
  Write-Ascii $profile 12 "mntr"
  Write-Ascii $profile 16 "RGB "
  Write-Ascii $profile 20 "XYZ "
  foreach ($datePart in @(@(24, 2026), @(26, 9), @(28, 13), @(30, 0), @(32, 0), @(34, 0))) {
    Write-BigEndianUInt16 $profile $datePart[0] $datePart[1]
  }
  Write-Ascii $profile 36 "acsp"
  Write-BigEndianUInt32 $profile 64 0
  Write-BigEndianUInt32 $profile 68 (ConvertTo-S15Fixed16 0.9642)
  Write-BigEndianUInt32 $profile 72 (ConvertTo-S15Fixed16 1.0)
  Write-BigEndianUInt32 $profile 76 (ConvertTo-S15Fixed16 0.8249)

  Write-BigEndianUInt32 $profile 128 $tagCount
  $entries = @(
    @("desc", $descriptionOffset, $description.Length),
    @("cprt", $copyrightOffset, $copyright.Length),
    @("wtpt", $whitePointOffset, $whitePoint.Length),
    @("rXYZ", $redOffset, $red.Length),
    @("gXYZ", $greenOffset, $green.Length),
    @("bXYZ", $blueOffset, $blue.Length),
    @("rTRC", $curveOffset, $curve.Length),
    @("gTRC", $curveOffset, $curve.Length),
    @("bTRC", $curveOffset, $curve.Length)
  )
  for ($index = 0; $index -lt $entries.Length; ++$index) {
    $entryOffset = 132 + $index * 12
    Write-Ascii $profile $entryOffset $entries[$index][0]
    Write-BigEndianUInt32 $profile ($entryOffset + 4) $entries[$index][1]
    Write-BigEndianUInt32 $profile ($entryOffset + 8) $entries[$index][2]
  }

  foreach ($payload in @(
      @($descriptionOffset, $description),
      @($copyrightOffset, $copyright),
      @($whitePointOffset, $whitePoint),
      @($redOffset, $red),
      @($greenOffset, $green),
      @($blueOffset, $blue),
      @($curveOffset, $curve)
    )) {
    [Array]::Copy($payload[1], 0, $profile, $payload[0], $payload[1].Length)
  }
  return ,$profile
}

function Get-SwappedIccProfile {
  param(
    [byte[]]$SourceBytes
  )

  [byte[]]$iccBytes = $SourceBytes.Clone()
  if ($iccBytes.Length -lt 132) {
    throw "ICC profile is too short"
  }
  if ($iccBytes[84..99] | Where-Object { $_ -ne 0 }) {
    throw "ICC profile ID is not zero; the patched profile would need a new MD5"
  }

  $tagCount = Get-BigEndianUInt32 $iccBytes 128
  $redOffset = $null
  $redSize = $null
  $blueOffset = $null
  $blueSize = $null
  for ($index = 0; $index -lt $tagCount; ++$index) {
    $entry = 132 + $index * 12
    if ($entry + 12 -gt $iccBytes.Length) {
      throw "ICC tag table extends beyond the profile"
    }
    $signature = [Text.Encoding]::ASCII.GetString($iccBytes, $entry, 4)
    if ($signature -eq "rXYZ") {
      $redOffset = [int](Get-BigEndianUInt32 $iccBytes ($entry + 4))
      $redSize = [int](Get-BigEndianUInt32 $iccBytes ($entry + 8))
    } elseif ($signature -eq "bXYZ") {
      $blueOffset = [int](Get-BigEndianUInt32 $iccBytes ($entry + 4))
      $blueSize = [int](Get-BigEndianUInt32 $iccBytes ($entry + 8))
    }
  }
  if ($null -eq $redOffset -or $null -eq $blueOffset -or $redSize -ne $blueSize) {
    throw "ICC rXYZ and bXYZ payloads were not found with equal sizes"
  }
  if ($redOffset + $redSize -gt $iccBytes.Length -or $blueOffset + $blueSize -gt $iccBytes.Length) {
    throw "ICC XYZ payload extends beyond the profile"
  }

  [byte[]]$redPayload = [byte[]]::new($redSize)
  [byte[]]$bluePayload = [byte[]]::new($blueSize)
  [Array]::Copy($iccBytes, $redOffset, $redPayload, 0, $redSize)
  [Array]::Copy($iccBytes, $blueOffset, $bluePayload, 0, $blueSize)
  [Array]::Copy($bluePayload, 0, $iccBytes, $redOffset, $redSize)
  [Array]::Copy($redPayload, 0, $iccBytes, $blueOffset, $blueSize)
  return ,$iccBytes
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
  $quadrants = Join-Path $temporary "icc-quadrants.png"
  Invoke-Checked $Ffmpeg @("-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                            "nullsrc=size=64x64,format=rgb24,geq=r='if(lt(Y,32),if(lt(X,32),255,0),if(lt(X,32),0,128))':g='if(lt(Y,32),if(lt(X,32),0,255),if(lt(X,32),0,128))':b='if(lt(Y,32),0,if(lt(X,32),255,128))'",
                            "-frames:v", "1", "-c:v", "png", "-pix_fmt", "rgb24", $quadrants)

  $srgbProfile = Join-Path $temporary "srgb.icc"
  $swappedProfile = Join-Path $temporary "swapped.icc"
  [byte[]]$originalProfile = Get-PvdkitIccProfile
  [IO.File]::WriteAllBytes($srgbProfile, $originalProfile)
  [IO.File]::WriteAllBytes($swappedProfile, (Get-SwappedIccProfile $originalProfile))

  $swappedPng = Join-Path $fixtures "icc_swapped_rb_64x64.png"
  Copy-Item -LiteralPath $quadrants -Destination $swappedPng -Force
  Invoke-Checked $ExifTool @("-overwrite_original", "-ICC_Profile<=$swappedProfile", $swappedPng)
  Invoke-Checked $ExifTool @("-ICC_Profile:all", $swappedPng)
  & $decrypt -InputPath $swappedPng -OutputPath (Join-Path $fixtures "icc_swapped_rb_64x64.rpgmvp") -Wrap

  $srgbPng = Join-Path $temporary "icc-srgb.png"
  Copy-Item -LiteralPath $quadrants -Destination $srgbPng
  Invoke-Checked $ExifTool @("-overwrite_original", "-ICC_Profile<=$srgbProfile", $srgbPng)
  Invoke-Checked $ExifTool @("-ICC_Profile:all", $srgbPng)
  & $decrypt -InputPath $srgbPng -OutputPath (Join-Path $fixtures "icc_srgb_64x64.rpgmvp") -Wrap

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
