[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$fixtures = Join-Path $PSScriptRoot "..\fixtures"

function Write-GzipFixture {
    param([string]$Source, [string]$Destination)

    $inputFile = [IO.File]::ReadAllBytes($Source)
    $output = [IO.File]::Create($Destination)
    try {
        $gzip = [IO.Compression.GZipStream]::new($output, [IO.Compression.CompressionLevel]::Optimal, $false)
        try {
            $gzip.Write($inputFile, 0, $inputFile.Length)
        }
        finally {
            $gzip.Dispose()
        }
    }
    finally {
        $output.Dispose()
    }
}

Write-GzipFixture (Join-Path $fixtures "basic_2x2.svg") (Join-Path $fixtures "basic_2x2.svgz")
$garbage = [Text.Encoding]::UTF8.GetBytes("this is gzip data, but not an SVG document")
$temporary = Join-Path ([IO.Path]::GetTempPath()) "pvdkit-svg-gzip-garbage-$PID.svg"
try {
    [IO.File]::WriteAllBytes($temporary, $garbage)
    Write-GzipFixture $temporary (Join-Path $fixtures "gzip_garbage.svgz")
}
finally {
    Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
}
[IO.File]::WriteAllBytes((Join-Path $fixtures "truncated.svgz"), [byte[]](0x1F, 0x8B))
