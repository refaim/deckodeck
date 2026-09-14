[CmdletBinding()]
param(
  # A release tag: <id>/vX.Y.Z, where <id> is a plugin directory name under plugins/ (avif/v1.2.0).
  [Parameter(Mandatory = $true)]
  [string]$Tag
)

# Checks that a release tag names a plugin and the version that plugin declares, and describes the
# plugin for the release workflow: the tag must be <id>/vX.Y.Z, plugins/<id>/CMakeLists.txt must
# declare exactly that VERSION in its pvdkit_plugin_identity(...) call, and the first line of
# plugins/<id>/package/ChangeLog must be "<NAME> X.Y.Z DD.MM.YYYY" (the configure-time check in
# cmake/pvdkit-package-docs.cmake enforces the same line against CMake; this script enforces it
# against the tag before anything is built). Emits one object: Id, Name, Version, Tag.
$ErrorActionPreference = "Stop"

$repository = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ($Tag -cnotmatch '^(?<id>[a-z0-9_]+)/v(?<version>\d+\.\d+\.\d+)$') {
  throw "Tag '$Tag' is not <plugin id>/vMAJOR.MINOR.PATCH (lower-case id, as the directory under plugins/)"
}
$id = $Matches.id
$version = $Matches.version

$cmakeLists = Join-Path $repository "plugins\$id\CMakeLists.txt"
if (-not (Test-Path -LiteralPath $cmakeLists -PathType Leaf)) {
  throw "Tag '$Tag' names plugin '$id', but $cmakeLists does not exist"
}
$cmake = [IO.File]::ReadAllText($cmakeLists)
$identity = [regex]::Match($cmake, "pvdkit_plugin_identity\(\s*$id\s+NAME\s+(?<name>[A-Za-z0-9_]+)\s+VERSION\s+(?<version>\d+\.\d+\.\d+)")
if (-not $identity.Success) {
  throw "$cmakeLists has no pvdkit_plugin_identity($id NAME <NAME> VERSION <M.m.p> ...) call"
}
$name = $identity.Groups["name"].Value
$declared = $identity.Groups["version"].Value
if ($declared -ne $version) {
  throw "Tag '$Tag' asks for $version, but $cmakeLists declares VERSION $declared"
}

$changeLog = Join-Path $repository "plugins\$id\package\ChangeLog"
$bytes = [IO.File]::ReadAllBytes($changeLog)
if ($bytes.Length -lt 3 -or $bytes[0] -ne 0xEF -or $bytes[1] -ne 0xBB -or $bytes[2] -ne 0xBF) {
  throw "$changeLog must start with the UTF-8 BOM"
}
$firstLine = ([Text.Encoding]::UTF8.GetString($bytes, 3, $bytes.Length - 3) -split "`r`n")[0]
if ($firstLine -cnotmatch "^$name $([regex]::Escape($version)) \d\d\.\d\d\.\d{4}$") {
  throw "$changeLog must start with '$name $version DD.MM.YYYY'; its first line is '$firstLine'"
}

[pscustomobject]@{
  Id = $id
  Name = $name
  Version = $version
  Tag = $Tag
}
