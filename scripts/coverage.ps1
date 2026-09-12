[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

function Invoke-Checked {
  param(
    [string]$Command,
    [string[]]$Arguments
  )

  & $Command @Arguments
  if ($LASTEXITCODE -ne 0) {
    throw "$Command failed with exit code $LASTEXITCODE"
  }
}

# Heuristic: a header is treated as "executable" (and therefore required to appear in the
# coverage report) when, after stripping comments and string/char literals, it still contains a
# ")" or "]" followed eventually by a "{" with no ";"/"}" in between - i.e. it looks like it
# defines a function/method body rather than only declaring types or names. This errs toward
# false positives: a header wrongly deemed executable (e.g. a brace that isn't a function body)
# makes the completeness gate below fail spuriously on an uninstrumented file. A header wrongly
# deemed non-executable is not a silent gap either - it is still exercised through the overall
# line/branch coverage totals, just not required individually by name. Adjust the regex below if
# a legitimate header shape trips this heuristic.
function Test-ExecutableHeader {
  param([string]$Path)

  $text = Get-Content -LiteralPath $Path -Raw
  $text = [regex]::Replace($text, '(?s)/\*.*?\*/|(?m)//.*$', '')
  $text = [regex]::Replace($text, '(?s)"(?:\\.|[^"\\])*"|''(?:\\.|[^''\\])*''', '')
  return $text -match '(?s)(?:\)|\])[^;{}]*\{'
}

function ConvertTo-NormalizedPath {
  param([string]$Path)

  return [IO.Path]::GetFullPath($Path).Replace('/', [IO.Path]::DirectorySeparatorChar)
}

# The e2e process hosts two profile runtimes, e2e_tests.exe's and the one inside the AVIF.pvd it
# loads, and each writes its own file: same %p, different %m. Nothing else in this gate notices
# when the DLL's file goes missing, because Exports.cpp is also compiled into pvd_tests and
# DefaultPlugin.cpp into adapter_tests, and llvm-cov reads their counters against the DLL's
# mapping (same function names, same structural hashes). So: find a process that wrote two
# profiles, merge only those, and ask the DLL's own mapping whether its exports executed.
function Assert-PluginProfile {
  param(
    [string]$Plugin,
    [IO.FileInfo[]]$ProfileFiles,
    [string]$ExportsSource,
    [string]$LlvmProfdata,
    [string]$LlvmCov,
    [string]$WorkDirectory
  )

  $pattern = [regex]'^avifpvd-(\d+)-([^.]+)\.profraw$'
  $pairs = @(
    $ProfileFiles |
      ForEach-Object {
        $match = $pattern.Match($_.Name)
        if ($match.Success) {
          [pscustomobject]@{ ProcessId = $match.Groups[1].Value; Module = $match.Groups[2].Value; File = $_ }
        }
      } |
      Group-Object -Property ProcessId |
      Where-Object { @($_.Group | ForEach-Object { $_.Module } | Sort-Object -Unique).Count -ge 2 }
  )
  if ($pairs.Count -eq 0) {
    throw "Plugin profile check failed: no process wrote two raw profiles (same %p, different %m), so $Plugin never wrote its own counters. Expected e2e_tests.exe and the DLL it loads to each leave a file."
  }

  $mergedProfile = Join-Path $WorkDirectory "plugin-profile-check.profdata"
  foreach ($pair in $pairs) {
    $files = @($pair.Group | ForEach-Object { $_.File.FullName })
    Invoke-Checked $LlvmProfdata (@("merge", "-sparse") + $files + @("-o", $mergedProfile))
    $json = & $LlvmCov export -summary-only $Plugin "-instr-profile=$mergedProfile"
    if ($LASTEXITCODE -ne 0) {
      throw "llvm-cov export on $Plugin failed with exit code $LASTEXITCODE"
    }
    $exports = ($json -join "`n" | ConvertFrom-Json).data[0].files |
      Where-Object { (ConvertTo-NormalizedPath $_.filename) -eq $ExportsSource }
    if ($exports -and $exports.summary.functions.covered -gt 0) {
      $names = ($pair.Group | ForEach-Object { $_.File.Name }) -join ', '
      Write-Output "Plugin profile check passed: process $($pair.Name) wrote $($files.Count) profiles ($names); $Plugin reports $($exports.summary.functions.covered)/$($exports.summary.functions.count) Exports.cpp functions executed from them."
      return
    }
  }
  throw "Plugin profile check failed: the raw profiles of process(es) $(($pairs | ForEach-Object { $_.Name }) -join ', ') do not carry $Plugin's counters (Exports.cpp functions executed: 0)."
}

$repository = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildDirectory = Join-Path $repository "build\coverage$env:AVIFPVD_BUILD_SUFFIX"
$llvmDirectory = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin"
$llvmProfdata = Join-Path $llvmDirectory "llvm-profdata.exe"
$llvmCov = Join-Path $llvmDirectory "llvm-cov.exe"
$hadProfileFile = Test-Path -LiteralPath "Env:LLVM_PROFILE_FILE"
$previousProfileFile = $env:LLVM_PROFILE_FILE

Push-Location $repository
try {
  Invoke-Checked "cmake" @("--preset", "coverage")
  Invoke-Checked "cmake" @("--build", "--preset", "coverage")

  Get-ChildItem -LiteralPath $buildDirectory -Filter "avifpvd-*.profraw" -File -ErrorAction SilentlyContinue |
    ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }
  # %p keeps parallel test processes apart; %m (a per-binary signature) keeps the instrumented
  # AVIF.pvd apart from the e2e_tests.exe that loads it: both profile runtimes live in one process
  # and read the same variable, and each writes its own counters when its module unloads (the DLL
  # on FreeLibrary, the executable at exit). With %p alone the second writer would overwrite the
  # first and the DLL's counters would be lost.
  $env:LLVM_PROFILE_FILE = Join-Path $buildDirectory "avifpvd-%p-%m.profraw"
  Invoke-Checked "ctest" @("--preset", "coverage")

  $profileFiles = @(Get-ChildItem -LiteralPath $buildDirectory -Filter "*.profraw" -File)
  if ($profileFiles.Count -eq 0) {
    throw "No raw coverage profiles were produced in $buildDirectory"
  }
  Write-Output "Raw profiles produced: $($profileFiles.Count)"
  $profileFiles | ForEach-Object { Write-Output "  $($_.Name) ($($_.Length) bytes)" }
  $profileData = Join-Path $buildDirectory "coverage.profdata"
  Invoke-Checked $llvmProfdata (@("merge", "-sparse") + $profileFiles.FullName + @("-o", $profileData))

  $normalizedBuildDirectory = (ConvertTo-NormalizedPath $buildDirectory) + [IO.Path]::DirectorySeparatorChar
  $testInventoryJson = & ctest --test-dir $buildDirectory --show-only=json-v1
  if ($LASTEXITCODE -ne 0) {
    throw "ctest test discovery failed with exit code $LASTEXITCODE"
  }
  $testInventory = $testInventoryJson | ConvertFrom-Json
  # Only executables built into this tree carry coverage mappings; a test that runs an external
  # program (e.g. the PowerShell import check) must not be handed to llvm-cov as an object.
  $objects = @(
    $testInventory.tests |
      ForEach-Object { $_.command[0] } |
      Where-Object { $_ -and [IO.Path]::GetExtension($_) -eq ".exe" } |
      ForEach-Object { ConvertTo-NormalizedPath $_ } |
      Where-Object { $_.StartsWith($normalizedBuildDirectory, [StringComparison]::OrdinalIgnoreCase) } |
      Sort-Object -Unique
  )
  if ($objects.Count -eq 0) {
    throw "CTest did not discover any test executables in $buildDirectory"
  }
  foreach ($object in $objects) {
    if (-not (Test-Path -LiteralPath $object -PathType Leaf)) {
      throw "CTest discovered a missing test executable: $object"
    }
  }
  Write-Output "Coverage objects discovered from CTest: $($objects.Count)"
  $objects | ForEach-Object { Write-Output "  $_" }

  # AVIF.pvd is instrumented too (e2e_tests loads it with LoadLibrary), and its mapping for
  # Exports.cpp / DefaultPlugin.cpp lives in the DLL, not in any test executable.
  $plugins = @(
    Get-ChildItem -LiteralPath (Join-Path $buildDirectory "src") -Recurse -File -Filter "*.pvd" |
      ForEach-Object { ConvertTo-NormalizedPath $_.FullName } |
      Sort-Object -Unique
  )
  if ($plugins.Count -eq 0) {
    throw "No instrumented plugin (*.pvd) was found under $buildDirectory\src"
  }
  Write-Output "Coverage objects discovered as plugins: $($plugins.Count)"
  $plugins | ForEach-Object { Write-Output "  $_" }
  $objects += $plugins

  $exportsSource = ConvertTo-NormalizedPath (Join-Path $repository "src\pvd\Exports.cpp")
  foreach ($plugin in $plugins) {
    Assert-PluginProfile -Plugin $plugin -ProfileFiles $profileFiles -ExportsSource $exportsSource `
      -LlvmProfdata $llvmProfdata -LlvmCov $llvmCov -WorkDirectory $buildDirectory
  }

  $ignoreRegex = '([\\/]tests[\\/]|[\\/]third_party[\\/]|[\\/]build[\\/]|[\\/]installed[\\/]|[\\/]packages[\\/]|Program Files|Windows Kits)'
  $commonArguments = @($objects[0])
  foreach ($object in $objects | Select-Object -Skip 1) {
    $commonArguments += "-object=$object"
  }
  $commonArguments += "-instr-profile=$profileData"
  $commonArguments += "-ignore-filename-regex=$ignoreRegex"

  $report = & $llvmCov report @commonArguments
  if ($LASTEXITCODE -ne 0) {
    throw "llvm-cov report failed with exit code $LASTEXITCODE"
  }
  $report | Write-Output

  # -summary-only avoids paying for a full per-region/per-branch export just to read
  # files[].filename for the completeness check below; it still reports data[0].totals for the
  # coverage gate at the end of this script.
  $coverageJson = & $llvmCov export -summary-only @commonArguments
  if ($LASTEXITCODE -ne 0) {
    throw "llvm-cov export failed with exit code $LASTEXITCODE"
  }
  $coverage = $coverageJson | ConvertFrom-Json
  $totals = $coverage.data[0].totals

  $coveredFiles = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
  foreach ($file in $coverage.data[0].files) {
    [void]$coveredFiles.Add((ConvertTo-NormalizedPath $file.filename))
  }

  $requiredSources = @(
    Get-ChildItem -LiteralPath (Join-Path $repository "src") -Recurse -File |
      Where-Object {
        $_.Extension -eq ".cpp" -or ($_.Extension -eq ".hpp" -and (Test-ExecutableHeader $_.FullName))
      }
  )
  $missingSources = @(
    $requiredSources |
      Where-Object { -not $coveredFiles.Contains((ConvertTo-NormalizedPath $_.FullName)) } |
      ForEach-Object {
        (ConvertTo-NormalizedPath $_.FullName).Substring($repository.Length + 1).Replace('\', '/')
      }
  )
  if ($missingSources.Count -ne 0) {
    throw "Coverage source completeness failed. Missing files:`n  $($missingSources -join "`n  ")"
  }
  Write-Output "Coverage source completeness passed: $($requiredSources.Count) executable source files present."

  $htmlDirectory = Join-Path $buildDirectory "html"
  $expectedBuildRoot = [IO.Path]::GetFullPath((Join-Path $repository "build")) + [IO.Path]::DirectorySeparatorChar
  $resolvedHtmlDirectory = [IO.Path]::GetFullPath($htmlDirectory)
  if (-not $resolvedHtmlDirectory.StartsWith($expectedBuildRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to replace an HTML directory outside $expectedBuildRoot"
  }
  if (Test-Path -LiteralPath $htmlDirectory) {
    Remove-Item -LiteralPath $htmlDirectory -Recurse -Force
  }
  Invoke-Checked $llvmCov (@("show") + $commonArguments + @("-format=html", "-output-dir=$htmlDirectory"))

  if ($totals.lines.percent -ne 100 -or $totals.branches.percent -ne 100) {
    throw "Coverage gate failed: lines $($totals.lines.percent)%, branches $($totals.branches.percent)%"
  }

  Write-Output "Coverage gate passed: lines 100%, branches 100%."
  Write-Output "HTML report: $htmlDirectory"
} finally {
  if ($hadProfileFile) {
    $env:LLVM_PROFILE_FILE = $previousProfileFile
  } else {
    Remove-Item -LiteralPath "Env:LLVM_PROFILE_FILE" -ErrorAction SilentlyContinue
  }
  Pop-Location
}
