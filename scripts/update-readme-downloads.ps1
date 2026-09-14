[CmdletBinding()]
param(
  # Plugin NAME as released (AVIF, RPGMVP): the first cell of its row.
  [Parameter(Mandatory = $true)]
  [string]$Name,
  # The version just released (X.Y.Z).
  [Parameter(Mandatory = $true)]
  [string]$Version,
  # The release tag (<id>/vX.Y.Z); the row links to the GitHub Release page of that tag.
  [Parameter(Mandatory = $true)]
  [string]$Tag,
  # The repository the releases live in.
  [string]$RepositoryUrl = "https://github.com/refaim/deckodeck",
  # The README to edit; the repository's own by default.
  [string]$Readme
)

# Keeps the "Downloads" table of README.md current: GitHub lists every plugin's releases in one
# flat list, so the table carries one row per plugin with its latest version and a link to that
# release. The rows live between the <!-- downloads:begin --> and <!-- downloads:end --> anchors;
# this script replaces the row whose first cell is <NAME>.pvd (or appends one) and rewrites the
# file only when the row changed, so a re-run for a version already listed is a no-op, and a row
# that already names a newer version (an older line released later) is left alone as well. Prints
# exactly one word on the pipeline, "updated" or "unchanged" (the reason for "unchanged" goes to
# the verbose stream). The release workflow runs it after `gh release create` and commits the
# result when it says "updated". Line endings and the UTF-8 (no BOM) encoding are preserved.
$ErrorActionPreference = "Stop"

if (-not $Readme) {
  $Readme = Join-Path $PSScriptRoot "..\README.md"
}
$Readme = [IO.Path]::GetFullPath($Readme)
$released = [version]$Version
$text = [IO.File]::ReadAllText($Readme)
$newline = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }
$begin = "<!-- downloads:begin -->"
$end = "<!-- downloads:end -->"
$beginIndex = $text.IndexOf($begin, [StringComparison]::Ordinal)
$endIndex = $text.IndexOf($end, [StringComparison]::Ordinal)
if ($beginIndex -lt 0 -or $endIndex -lt 0 -or $endIndex -lt $beginIndex) {
  throw "$Readme has no $begin ... $end block"
}
$blockStart = $beginIndex + $begin.Length
$block = $text.Substring($blockStart, $endIndex - $blockStart)

$row = "| $Name.pvd | $Version | [$Tag]($RepositoryUrl/releases/tag/$Tag) |"
# The whole row up to (not including) its line ending, LF or CRLF alike (in .NET, `$` under
# (?m) sits before "\n" only, so a trailing "\r" is excluded explicitly instead); the version
# cell is captured to compare releases.
$rowPattern = "(?m)^\| $([regex]::Escape($Name))\.pvd \| (?<version>[0-9]+\.[0-9]+\.[0-9]+) \|[^\r\n]*"
$existing = [regex]::Match($block, $rowPattern)
if ($existing.Success) {
  $listed = [version]$existing.Groups["version"].Value
  if ($listed -gt $released) {
    Write-Verbose "the row already names $listed, newer than $Version; left alone"
    Write-Output "unchanged"
    return
  }
  # A MatchEvaluator inserts the row literally ($ and \ in a replacement string would be special).
  $updatedBlock = [regex]::Replace($block, $rowPattern, [Text.RegularExpressions.MatchEvaluator]{ $row })
} else {
  # Append after the last table row (the block ends with the newline before the end anchor).
  $updatedBlock = $block.TrimEnd("`r", "`n") + $newline + $row + $newline
}
if ($updatedBlock -eq $block) {
  Write-Verbose "the row already names $Version"
  Write-Output "unchanged"
  return
}
$updated = $text.Substring(0, $blockStart) + $updatedBlock + $text.Substring($endIndex)
[IO.File]::WriteAllText($Readme, $updated, [Text.UTF8Encoding]::new($false))
Write-Output "updated"
