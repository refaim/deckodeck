[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$Path
)

# The export table of every plugin DLL must be exactly the eight PVD entry points under their bare
# names: no leading underscore and no @N (the __stdcall decoration x86 gives the symbols), and
# nothing else. 0PictureView.dll resolves them with GetProcAddress by these names.
$ErrorActionPreference = "Stop"

$expected = @("pvdExit", "pvdFileClose", "pvdFileOpen", "pvdInit", "pvdPageDecode", "pvdPageFree",
              "pvdPageInfo", "pvdPluginInfo")

$resolvedPath = (Resolve-Path -LiteralPath $Path).Path
$llvmReadObj = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\llvm-readobj.exe"
if (-not (Test-Path -LiteralPath $llvmReadObj)) {
  throw "llvm-readobj was not found at $llvmReadObj"
}

$output = & $llvmReadObj --coff-exports $resolvedPath 2>&1
$toolExitCode = $LASTEXITCODE
$output | Write-Output
if ($toolExitCode -ne 0) {
  [Console]::Error.WriteLine("llvm-readobj failed with exit code $toolExitCode")
  exit 1
}

$names = @(
  $output |
    ForEach-Object { if ($_ -match '^\s*Name:\s*(\S+)\s*$') { $Matches[1] } } |
    Sort-Object { $_ } -CaseSensitive
)

if (($names -join ",") -cne ($expected -join ",")) {
  [Console]::Error.WriteLine("Export policy violation. Expected exactly: $($expected -join ', '); found: $($names -join ', ')")
  exit 1
}

Write-Output "Export policy passed: the eight PVD entry points are exported under their bare names."
