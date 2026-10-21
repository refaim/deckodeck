# Pure import-policy helpers shared by scripts/check-imports.ps1 and its Pester tests.

function Select-ImportedModule {
  param([string[]]$ReadObjOutput)
  return @(
    $ReadObjOutput |
      ForEach-Object { if ($_ -match '^\s*Name:\s*(\S+)\s*$') { $Matches[1] } } |
      Sort-Object -Unique
  )
}

function Test-AllowedModule {
  param(
    [string[]]$Modules,
    [string[]]$AllowedModules
  )
  if ($AllowedModules.Count -eq 0) {
    return $false
  }
  $actual = @($Modules | ForEach-Object { $_.ToLowerInvariant() } | Sort-Object -Unique)
  $allowed = @($AllowedModules | ForEach-Object { $_.ToLowerInvariant() } | Sort-Object -Unique)
  return (($actual -join ",") -ceq ($allowed -join ","))
}

function Resolve-AllowedModule {
  param([object[]]$Imports)

  # ConvertFrom-Json gives $null for both a missing property and an explicit JSON null, while
  # a JSON [] becomes an empty array. Neither form may erase the fail-closed KERNEL32 default.
  $allowed = @(
    $Imports |
      Where-Object { $null -ne $_ -and -not [string]::IsNullOrWhiteSpace([string]$_) } |
      ForEach-Object { ([string]$_).Trim() } |
      Sort-Object -Unique
  )
  if ($allowed.Count -eq 0) {
    return @("KERNEL32.dll")
  }
  return $allowed
}
