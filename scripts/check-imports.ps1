[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$Path,
  # Comma-separated allowlist from the plugin's package manifest. The default keeps every plugin
  # that does not declare a wider policy at KERNEL32.dll.
  [string]$AllowedModules = "KERNEL32.dll"
)

$ErrorActionPreference = "Stop"

$resolvedPath = (Resolve-Path -LiteralPath $Path).Path
. (Join-Path $PSScriptRoot "import-policy.ps1")
. (Join-Path $PSScriptRoot "llvm-dir.ps1")
$llvmReadObj = Join-Path (Get-LlvmDir) "llvm-readobj.exe"
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

$modules = @(Select-ImportedModule -ReadObjOutput $output)
$allowed = @($AllowedModules -split "," | ForEach-Object { $_.Trim() } | Where-Object { $_ })

if (-not (Test-AllowedModule -Modules $modules -AllowedModules $allowed)) {
  [Console]::Error.WriteLine(
    "Import policy violation. Expected exactly: $($allowed -join ', '); found: $($modules -join ', ')")
  exit 1
}

Write-Output "Import policy passed: exactly $($allowed -join ', ') imported."
