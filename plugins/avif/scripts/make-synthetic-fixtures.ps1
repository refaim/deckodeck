[CmdletBinding()]
param(
  [string]$Destination
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Destination)) {
  $Destination = Join-Path $PSScriptRoot "..\fixtures"
}

function Invoke-FFmpeg {
  param([string[]]$Arguments)

  & ffmpeg -hide_banner -loglevel error -y @Arguments
  if ($LASTEXITCODE -ne 0) {
    throw "ffmpeg failed with exit code $LASTEXITCODE"
  }
}

if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) {
  throw "ffmpeg must be available on PATH"
}

New-Item -ItemType Directory -Force -Path $Destination | Out-Null

$quad = "nullsrc=size=64x64,format=gbrp,geq=r='if(lt(Y,H/2),if(lt(X,W/2),255,0),if(lt(X,W/2),0,255))':g='if(lt(Y,H/2),if(lt(X,W/2),0,255),if(lt(X,W/2),0,255))':b='if(lt(Y,H/2),0,255)'"
$rgbLossless = @(
  "-f", "lavfi", "-i", $quad, "-frames:v", "1", "-c:v", "libaom-av1", "-still-picture", "1",
  "-pix_fmt", "gbrp", "-aom-params", "lossless=1", "-color_range", "pc", "-colorspace", "rgb"
)

Invoke-FFmpeg ($rgbLossless + (Join-Path $Destination "quad_rgb_lossless.avif"))
Invoke-FFmpeg @(
  "-f", "lavfi", "-i", $quad, "-frames:v", "1", "-c:v", "libaom-av1", "-still-picture", "1",
  "-pix_fmt", "yuv420p", "-color_range", "tv", "-colorspace", "bt709",
  (Join-Path $Destination "quad_yuv420.avif")
)

$alpha = "nullsrc=size=96x32,format=yuva444p,geq=lum='255':cb='128':cr='128':a='if(lt(X,W/3),0,if(lt(X,2*W/3),128,255))'"
Invoke-FFmpeg @(
  "-f", "lavfi", "-i", $alpha,
  "-filter_complex", "[0:v]split[color][rgba];[color]format=yuv444p[colorout];[rgba]alphaextract,setparams=colorspace=unknown:range=full[alphaout]",
  "-map", "[colorout]", "-map", "[alphaout]", "-frames:v", "1", "-c:v", "libaom-av1",
  "-still-picture", "1", "-pix_fmt:v:0", "yuv444p", "-pix_fmt:v:1", "gray",
  "-aom-params:v:0", "lossless=1", "-aom-params:v:1", "lossless=1",
  "-color_range:v:0", "pc", "-colorspace:v:0", "bt709", "-color_range:v:1", "pc",
  (Join-Path $Destination "alpha_steps.avif")
)

$animationDirectory = Join-Path ([IO.Path]::GetTempPath()) "pvdkit-animation-$([Guid]::NewGuid())"
New-Item -ItemType Directory -Path $animationDirectory | Out-Null
try {
  $redFrame = Join-Path $animationDirectory "red.png"
  $greenFrame = Join-Path $animationDirectory "green.png"
  $blueFrame = Join-Path $animationDirectory "blue.png"
  Invoke-FFmpeg @("-f", "lavfi", "-i", "nullsrc=size=64x64,format=rgb24,geq=r=255:g=0:b=0",
    "-frames:v", "1", $redFrame)
  Invoke-FFmpeg @("-f", "lavfi", "-i", "nullsrc=size=64x64,format=rgb24,geq=r=0:g=255:b=0",
    "-frames:v", "1", $greenFrame)
  Invoke-FFmpeg @("-f", "lavfi", "-i", "nullsrc=size=64x64,format=rgb24,geq=r=0:g=0:b=255",
    "-frames:v", "1", $blueFrame)

  $concatFile = Join-Path $animationDirectory "frames.txt"
  $concatPath = { param([string]$Path) $Path.Replace("\", "/").Replace("'", "'\''") }
  [IO.File]::WriteAllLines($concatFile, @(
      "file '$(& $concatPath $redFrame)'",
      "option framerate 1000",
      "duration 0.1",
      "file '$(& $concatPath $greenFrame)'",
      "option framerate 1000",
      "duration 0.2",
      "file '$(& $concatPath $blueFrame)'",
      "option framerate 1000",
      "duration 0.3",
      # Repeating the final file lets the concat demuxer preserve its preceding duration.
      "file '$(& $concatPath $blueFrame)'",
      "option framerate 1000"
    ))
  $animationWithEndMarker = Join-Path $animationDirectory "animation-with-end-marker.avif"
  Invoke-FFmpeg @(
    "-f", "concat", "-safe", "0", "-i", $concatFile, "-fps_mode", "passthrough",
    "-c:v", "libaom-av1", "-pix_fmt", "gbrp", "-aom-params", "lossless=1",
    "-color_range", "pc", "-colorspace", "rgb", $animationWithEndMarker
  )
  Invoke-FFmpeg @(
    "-i", $animationWithEndMarker, "-map", "0:v:1", "-frames:v", "3", "-c:v", "copy",
    (Join-Path $Destination "anim_3frames.avif")
  )
} finally {
  Remove-Item -LiteralPath $animationDirectory -Recurse -Force
}

$gray = "nullsrc=size=64x64,format=gray,geq=lum='4*X'"
Invoke-FFmpeg @(
  "-f", "lavfi", "-i", $gray, "-frames:v", "1", "-c:v", "libaom-av1", "-still-picture", "1",
  "-pix_fmt", "gray", "-aom-params", "lossless=1", (Join-Path $Destination "gray_400.avif")
)

$tenBit = "nullsrc=size=64x64,format=yuv444p10le,geq=lum='16*X':cb='512':cr='512'"
Invoke-FFmpeg @(
  "-f", "lavfi", "-i", $tenBit, "-frames:v", "1", "-c:v", "libaom-av1", "-still-picture", "1",
  "-pix_fmt", "yuv444p10le", "-aom-params", "lossless=1", (Join-Path $Destination "tenbit_444.avif")
)

Invoke-FFmpeg @("-f", "lavfi", "-i", $quad, "-frames:v", "1", (Join-Path $Destination "not_avif.png"))
Invoke-FFmpeg @("-f", "lavfi", "-i", $quad, "-frames:v", "1", (Join-Path $Destination "not_avif.bmp"))

$garbage = [byte[]]::new(4096)
$generator = [Random]::new(0x41564946)
$generator.NextBytes($garbage)
[IO.File]::WriteAllBytes((Join-Path $Destination "garbage.bin"), $garbage)

$quadBytes = [IO.File]::ReadAllBytes((Join-Path $Destination "quad_yuv420.avif"))
$truncatedLength = [Math]::Floor($quadBytes.Length * 0.60)
[byte[]]$truncated = $quadBytes[0..($truncatedLength - 1)]
[IO.File]::WriteAllBytes((Join-Path $Destination "truncated.avif"), $truncated)

Write-Output "Generated 10 synthetic fixtures in $Destination."
