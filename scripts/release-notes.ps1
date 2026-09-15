[CmdletBinding()]
param(
  # Plugin id (directory name under plugins/) whose ChangeLog opens the notes.
  [Parameter(Mandatory = $true)]
  [string]$Id,
  # Where to write the notes (UTF-8 without BOM, LF); standard output when empty.
  [string]$OutFile
)

# Composes the GitHub Release notes from the first entry of plugins/<id>/package/ChangeLog (a
# UTF-8 BOM, CRLF file for Far users; the entry is everything before the next
# "<NAME> X.Y.Z DD.MM.YYYY" header), minus that header line and its dashed underline (the release
# title already gives the name and version, and GitHub shows the date), with the " * " / " + "
# bullets Far users read rendered as "- " so GitHub renders one list rather than a new list at
# every change of bullet character.
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
$entry = [Collections.Generic.List[string]]::new()
$entry.Add($lines[0])
for ($index = 1; $index -lt $lines.Count -and $lines[$index] -notmatch $header; $index++) {
  $entry.Add(($lines[$index] -replace '^ [*+] ', '- '))
}
# Drop the header line and its dashed underline; only the bullets remain.
if ($entry.Count -ge 2) { $entry.RemoveRange(0, 2) } else { $entry.Clear() }
while ($entry.Count -gt 0 -and $entry[0].Trim() -eq "") {
  $entry.RemoveAt(0)
}
while ($entry.Count -gt 0 -and $entry[$entry.Count - 1].Trim() -eq "") {
  $entry.RemoveAt($entry.Count - 1)
}

$text = ($entry -join "`n") + "`n"
if ($OutFile) {
  [IO.File]::WriteAllText([IO.Path]::GetFullPath($OutFile), $text, [Text.UTF8Encoding]::new($false))
} else {
  Write-Output $text
}
