[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$Path
)

$ErrorActionPreference = "Stop"

$resolvedPath = (Resolve-Path -LiteralPath $Path).Path
$llvmReadObj = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\llvm-readobj.exe"
if (-not (Test-Path -LiteralPath $llvmReadObj)) {
  throw "llvm-readobj was not found at $llvmReadObj"
}

$output = & $llvmReadObj --coff-imports $resolvedPath 2>&1
$toolExitCode = $LASTEXITCODE
$output | Write-Output
if ($toolExitCode -ne 0) {
  [Console]::Error.WriteLine("llvm-readobj failed with exit code $toolExitCode")
  exit 1
}

$modules = @(
  $output |
    ForEach-Object { if ($_ -match '^\s*Name:\s*(\S+)\s*$') { $Matches[1] } } |
    Sort-Object -Unique
)

if ($modules.Count -ne 1 -or -not $modules[0].Equals("KERNEL32.dll", [StringComparison]::OrdinalIgnoreCase)) {
  [Console]::Error.WriteLine("Import policy violation. Expected only KERNEL32.dll; found: $($modules -join ', ')")
  exit 1
}

Write-Output "Import policy passed: KERNEL32.dll is the only imported module."
