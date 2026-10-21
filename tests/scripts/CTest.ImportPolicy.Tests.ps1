[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$BuildDir,

  [string]$Configuration = "Release",

  # CTest writes its LastTest.log under BuildDir even with --show-only. Point this test at a
  # disposable CI build directory, not at a build tree another process is using.
  [string]$CTest = "ctest",

  [hashtable]$ExpectedPolicies = @{
    svg = @("KERNEL32.dll", "ntdll.dll", "ole32.dll")
  }
)

$ErrorActionPreference = "Stop"

function Get-CTestRegistration {
  param(
    [string]$BuildDir,
    [string]$Configuration,
    [string]$CTest
  )

  $jsonPath = Join-Path ([IO.Path]::GetTempPath()) "pvdkit-ctest-$PID-$([Guid]::NewGuid().ToString('N')).json"
  try {
    & $CTest --test-dir $BuildDir -C $Configuration --show-only=json-v1 > $jsonPath 2>&1
    if ($LASTEXITCODE -ne 0) {
      throw "ctest --show-only=json-v1 failed with exit code $LASTEXITCODE"
    }
    return (Get-Content -LiteralPath $jsonPath -Raw | ConvertFrom-Json)
  } finally {
    Remove-Item -LiteralPath $jsonPath -Force -ErrorAction SilentlyContinue
  }
}

function Get-ImportTest {
  param(
    [object]$Registration,
    [string]$PluginId
  )

  $name = "${PluginId}_check_imports"
  return @($Registration.tests | Where-Object { $_.name -eq $name }) | Select-Object -First 1
}

function Get-OptionValue {
  param(
    [string[]]$Command,
    [string]$Option
  )

  $index = [Array]::IndexOf($Command, $Option)
  if ($index -lt 0 -or $index + 1 -ge $Command.Count) {
    throw "CTest command has no value after $Option`: $($Command -join ' ')"
  }
  return $Command[$index + 1]
}

function Assert-ImportPolicyRegistration {
  param(
    [object]$Registration,
    [hashtable]$ExpectedPolicies
  )

  $importTests = @($Registration.tests | Where-Object { $_.name -match "^(.+)_check_imports$" })
  if ($importTests.Count -eq 0) {
    throw "The configured build registers no <plugin>_check_imports tests"
  }

  foreach ($test in $importTests) {
    # A Where-Object Match leaves $Matches pointing at the last item, so derive the id here.
    if ($test.name -notmatch "^(.+)_check_imports$") {
      throw "Unexpected import-policy test name '$($test.name)'"
    }
    $pluginId = $Matches[1]
    $command = @($test.command)
    $allowed = Get-OptionValue $command "-AllowedModules"
    $expectedModules = if ($ExpectedPolicies.ContainsKey($pluginId)) {
      @($ExpectedPolicies[$pluginId])
    } else {
      @("KERNEL32.dll")
    }
    $expected = $expectedModules -join ","

    if ($allowed -cne $expected) {
      throw "$($test.name) registers -AllowedModules '$allowed'; expected '$expected'"
    }
    if ([string]::IsNullOrWhiteSpace($allowed)) {
      throw "$($test.name) registers an empty -AllowedModules value"
    }
  }
}

function New-MixedImportRegistration {
  return [pscustomobject]@{
    tests = @(
      [pscustomobject]@{
        name = "avif_check_imports"
        command = @(
          "pwsh", "-NoProfile", "-File", "check-imports.ps1",
          "-Path", "AVIF.pvd", "-AllowedModules", "KERNEL32.dll"
        )
      },
      [pscustomobject]@{
        name = "svg_check_imports"
        command = @(
          "pwsh", "-NoProfile", "-File", "check-imports.ps1",
          "-Path", "SVG.pvd", "-AllowedModules", "KERNEL32.dll,ntdll.dll,ole32.dll"
        )
      }
    )
  }
}

function Assert-Throws {
  param([scriptblock]$Body)

  $thrown = $false
  try {
    & $Body | Out-Null
  } catch {
    $thrown = $true
  }
  if (-not $thrown) {
    throw "Expected the expression to throw"
  }
}

Describe "CTest import-policy registration" {
  It "registers an executable svg_check_imports command with the declared SVG policy" {
    $registration = Get-CTestRegistration $BuildDir $Configuration $CTest
    $test = Get-ImportTest $registration "svg"
    if (-not $test) {
      throw "The configured build does not register svg_check_imports for $Configuration"
    }

    $command = @($test.command)
    $scriptPath = Get-OptionValue $command "-File"
    $dllPath = Get-OptionValue $command "-Path"
    $allowed = Get-OptionValue $command "-AllowedModules"
    $expected = @($ExpectedPolicies["svg"]) -join ","

    $scriptPath | Should Match "check-imports\.ps1$"
    $dllPath | Should Match "[\\/]plugins[\\/]svg[\\/][^\\/]+\.pvd$"
    $allowed | Should Be $expected
    $allowed | Should Not BeNullOrEmpty
  }

  It "uses the exact declared policy or KERNEL32.dll default for every registered plugin" {
    $registration = Get-CTestRegistration $BuildDir $Configuration $CTest
    Assert-ImportPolicyRegistration $registration $ExpectedPolicies
  }

  It "accepts distinct mixed AVIF+SVG policies and rejects an empty policy for either" {
    $policies = @{
      avif = @("KERNEL32.dll")
      svg = @("KERNEL32.dll", "ntdll.dll", "ole32.dll")
    }
    $registration = New-MixedImportRegistration

    Assert-ImportPolicyRegistration $registration $policies

    foreach ($plugin in @("avif", "svg")) {
      $broken = New-MixedImportRegistration
      $test = @($broken.tests | Where-Object { $_.name -eq "${plugin}_check_imports" })[0]
      $allowedIndex = [Array]::IndexOf(@($test.command), "-AllowedModules") + 1
      $test.command[$allowedIndex] = ""

      Assert-Throws { Assert-ImportPolicyRegistration $broken $policies }
    }
  }
}
