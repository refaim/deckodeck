# Run with Windows PowerShell: Invoke-Pester -Script tests/scripts/ImportPolicy.Tests.ps1
$policy = Join-Path $PSScriptRoot "..\..\scripts\import-policy.ps1"
. $policy

Describe "plugin import policy" {
  It "extracts sorted unique DLL names from llvm-readobj output" {
    $output = @(
      "Import {",
      "  Name: ntdll.dll",
      "  ImportAddressTableSize: 16",
      "}",
      "Import {",
      "  Name: KERNEL32.dll",
      "}",
      "Import {",
      "  Name: ntdll.dll",
      "}"
    )
    (Select-ImportedModule -ReadObjOutput $output) -join "," | Should Be "KERNEL32.dll,ntdll.dll"
  }

  It "accepts exactly the declared modules, case-insensitively" {
    Test-AllowedModule -Modules @("KERNEL32.dll", "NTDLL.DLL", "ole32.dll") `
      -AllowedModules @("kernel32.dll", "ntdll.dll", "OLE32.dll") | Should Be $true
  }

  It "rejects an undeclared module" {
    Test-AllowedModule -Modules @("KERNEL32.dll", "user32.dll") `
      -AllowedModules @("KERNEL32.dll", "ntdll.dll", "ole32.dll") | Should Be $false
  }

  It "rejects a missing module and an empty allowlist" {
    Test-AllowedModule -Modules @("KERNEL32.dll") `
      -AllowedModules @("KERNEL32.dll", "ntdll.dll") | Should Be $false
    Test-AllowedModule -Modules @("KERNEL32.dll") -AllowedModules @() | Should Be $false
  }

  It "defaults missing, null and explicitly empty manifests to KERNEL32.dll" {
    (Resolve-AllowedModule -Imports $null) -join "," | Should Be "KERNEL32.dll"
    (Resolve-AllowedModule -Imports @()) -join "," | Should Be "KERNEL32.dll"
    (Resolve-AllowedModule -Imports @($null, "  ")) -join "," | Should Be "KERNEL32.dll"
  }

  It "preserves a declared import policy instead of replacing it with the default" {
    (Resolve-AllowedModule -Imports @("ole32.dll", "KERNEL32.dll", "ntdll.dll") ) -join "," |
      Should Be "KERNEL32.dll,ntdll.dll,ole32.dll"
  }
}
