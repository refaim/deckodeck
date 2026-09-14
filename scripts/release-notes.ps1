[CmdletBinding()]
param(
  # Plugin id (directory name under plugins/) whose ChangeLog opens the notes.
  [Parameter(Mandatory = $true)]
  [string]$Id,
  # The release zips; each is listed with its SHA-256. As separate arguments or comma-separated
  # (`-Zip a.zip,b.zip` reaches a script started with `powershell -File` as one string).
  [Parameter(Mandatory = $true)]
  [string[]]$Zip,
  # Where to write the notes (UTF-8 without BOM, LF); standard output when empty.
  [string]$OutFile
)

# Composes the GitHub Release notes: the first entry of plugins/<id>/package/ChangeLog (a UTF-8
# BOM, CRLF file for Far users; the entry is everything before the next "<NAME> X.Y.Z DD.MM.YYYY"
# header, rendered as Markdown: the dashed underline makes the header a heading, and the " * " /
# " + " bullets Far users read become "- " so GitHub renders one list rather than a new list at
# every change of bullet character) followed by the SHA-256 of every zip.
$ErrorActionPreference = "Stop"

$repository = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$changeLog = Join-Path $repository "plugins\$Id\package\ChangeLog"
$bytes = [IO.File]::ReadAllBytes($changeLog)
if ($bytes.Length -lt 3 -or $bytes[0] -ne 0xEF -or $bytes[1] -ne 0xBB -or $bytes[2] -ne 0xBF) {
  throw "$changeLog must start with the UTF-8 BOM"
}
$lines = [Text.Encoding]::UTF8.GetString($bytes, 3, $bytes.Length - 3) -split "`r`n"
$header = '^[A-Za-z0-9_]+ \d+\.\d+\.\d+ \d\d\.\d\d\.\d{4}$'
if ($lines[0] -notmatch $header) {
  throw "$changeLog must start with '<NAME> X.Y.Z DD.MM.YYYY'; its first line is '$($lines[0])'"
}
$entry = @($lines[0])
for ($index = 1; $index -lt $lines.Count -and $lines[$index] -notmatch $header; $index++) {
  $entry += $lines[$index] -replace '^ [*+] ', '- '
}
while ($entry.Count -gt 1 -and $entry[-1].Trim() -eq "") {
  $entry = $entry[0..($entry.Count - 2)]
}

$notes = @($entry) + @("", "SHA-256:", "")
$Zip = @($Zip | ForEach-Object { $_ -split "," } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
foreach ($path in $Zip) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "$path does not exist"
  }
  $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
  $notes += "    $([IO.Path]::GetFileName($path))  $hash"
}
$text = ($notes -join "`n") + "`n"
if ($OutFile) {
  [IO.File]::WriteAllText([IO.Path]::GetFullPath($OutFile), $text, [Text.UTF8Encoding]::new($false))
} else {
  Write-Output $text
}
