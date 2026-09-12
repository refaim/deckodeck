[CmdletBinding()]
param(
  # Coverage configure/build/test preset: `coverage` (x64, default) or `coverage-x86`. The build
  # directory, the instrumented plugin DLLs and the test executables all follow the preset name.
  # The gate spans src/** and plugins/*/src/** and requires every built plugin DLL to have written
  # its own profile (filed under its plugin id, see Assert-PluginProfile); it is a whole-tree gate,
  # so run it with every plugin enabled (PVDKIT_PLUGINS unset).
  [ValidateSet("coverage", "coverage-x86")]
  [string]$Preset = "coverage"
)

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

# A plugin's e2e process hosts two profile runtimes, <id>_e2e_tests.exe's and the one inside the
# DLL it loads, and each writes its own file: same %p, different %m. Nothing else in this gate
# notices when the DLL's file goes missing, because Exports.cpp is also compiled into pvd_tests
# and DefaultPlugin.cpp into the plugin's adapter tests, and llvm-cov reads their counters against
# the DLL's mapping (same function names, same structural hashes) - and so does every other
# plugin DLL's profile, since Exports.cpp is identical in all of them. The check is therefore per
# plugin id: pvdkit_add_plugin_e2e_tests runs <id>_e2e_tests under
# LLVM_PROFILE_FILE=pvdkit-<id>-%p-%m.profraw (a ctest ENVIRONMENT property of coverage builds),
# and this function looks only at that plugin's files: find a process among them that wrote two
# profiles, merge only those, and ask the DLL's own mapping whether its exports executed. Another
# plugin's profiles, however complete, cannot stand in.
function Assert-PluginProfile {
  param(
    [string]$PluginId,
    [string]$Plugin,
    [IO.FileInfo[]]$ProfileFiles,
    [string]$ExportsSource,
    [string]$LlvmProfdata,
    [string]$LlvmCov,
    [string]$WorkDirectory
  )

  $pattern = [regex]('^pvdkit-' + [regex]::Escape($PluginId) + '-(\d+)-([^.]+)\.profraw$')
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
    throw "Plugin profile check failed for '$PluginId': no process wrote two raw profiles named pvdkit-$PluginId-<pid>-<module>.profraw (same pid, different module), so $Plugin never wrote its own counters. Expected ${PluginId}_e2e_tests.exe (run by ctest under the LLVM_PROFILE_FILE that pvdkit_add_plugin_e2e_tests sets) and the DLL it loads to each leave a file."
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
      Write-Output "Plugin profile check passed for '$PluginId': process $($pair.Name) wrote $($files.Count) profiles ($names); $Plugin reports $($exports.summary.functions.covered)/$($exports.summary.functions.count) Exports.cpp functions executed from them."
      return
    }
  }
  throw "Plugin profile check failed for '$PluginId': the raw profiles of process(es) $(($pairs | ForEach-Object { $_.Name }) -join ', ') do not carry $Plugin's counters (Exports.cpp functions executed: 0)."
}

$repository = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildDirectory = Join-Path $repository "build\$Preset$env:PVDKIT_BUILD_SUFFIX"
$llvmDirectory = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin"
$llvmProfdata = Join-Path $llvmDirectory "llvm-profdata.exe"
$llvmCov = Join-Path $llvmDirectory "llvm-cov.exe"
$hadProfileFile = Test-Path -LiteralPath "Env:LLVM_PROFILE_FILE"
$previousProfileFile = $env:LLVM_PROFILE_FILE

Push-Location $repository
try {
  Invoke-Checked "cmake" @("--preset", $Preset)
  Invoke-Checked "cmake" @("--build", "--preset", $Preset)

  Get-ChildItem -LiteralPath $buildDirectory -Filter "pvdkit-*.profraw" -File -ErrorAction SilentlyContinue |
    ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }
  # %p keeps parallel test processes apart; %m (a per-binary signature) keeps the instrumented
  # plugin DLL apart from the e2e executable that loads it: both profile runtimes live in one process
  # and read the same variable, and each writes its own counters when its module unloads (the DLL
  # on FreeLibrary, the executable at exit). With %p alone the second writer would overwrite the
  # first and the DLL's counters would be lost. This generic name serves every test but the e2e
  # ones: each <id>_e2e_tests carries a ctest ENVIRONMENT property (pvdkit_add_plugin_e2e_tests)
  # that overrides it with pvdkit-<id>-%p-%m.profraw in the same directory, which is how
  # Assert-PluginProfile below attributes profiles to plugins.
  # The test inventory is read before the run: --show-only writes Testing/Temporary/LastTest.log
  # like a run does, and would otherwise erase the [leak] lines the leak tests just logged there.
  $testInventoryJson = & ctest --test-dir $buildDirectory --show-only=json-v1
  if ($LASTEXITCODE -ne 0) {
    throw "ctest test discovery failed with exit code $LASTEXITCODE"
  }

  $env:LLVM_PROFILE_FILE = Join-Path $buildDirectory "pvdkit-%p-%m.profraw"
  Invoke-Checked "ctest" @("--preset", $Preset)

  $profileFiles = @(Get-ChildItem -LiteralPath $buildDirectory -Filter "*.profraw" -File)
  if ($profileFiles.Count -eq 0) {
    throw "No raw coverage profiles were produced in $buildDirectory"
  }
  Write-Output "Raw profiles produced: $($profileFiles.Count)"
  $profileFiles | ForEach-Object { Write-Output "  $($_.Name) ($($_.Length) bytes)" }
  $profileData = Join-Path $buildDirectory "coverage.profdata"
  Invoke-Checked $llvmProfdata (@("merge", "-sparse") + $profileFiles.FullName + @("-o", $profileData))

  $normalizedBuildDirectory = (ConvertTo-NormalizedPath $buildDirectory) + [IO.Path]::DirectorySeparatorChar
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

  # Every plugin DLL is instrumented too (its e2e test loads it with LoadLibrary), and its mapping
  # for Exports.cpp / DefaultPlugin.cpp lives in the DLL, not in any test executable. A DLL is
  # built under plugins/<id>/ (the plugin's binary directory), so the first path component below
  # plugins/ is the id its e2e test files its profiles under.
  $pluginsDirectory = ConvertTo-NormalizedPath (Join-Path $buildDirectory "plugins")
  $plugins = @(
    Get-ChildItem -LiteralPath $pluginsDirectory -Recurse -File -Filter "*.pvd" |
      ForEach-Object {
        $path = ConvertTo-NormalizedPath $_.FullName
        $relative = $path.Substring($pluginsDirectory.Length + 1)
        [pscustomobject]@{ Id = $relative.Split([IO.Path]::DirectorySeparatorChar)[0]; Path = $path }
      } |
      Sort-Object -Property Path -Unique
  )
  if ($plugins.Count -eq 0) {
    throw "No instrumented plugin (*.pvd) was found under $pluginsDirectory"
  }
  Write-Output "Coverage objects discovered as plugins: $($plugins.Count)"
  $plugins | ForEach-Object { Write-Output "  $($_.Path) (plugin id '$($_.Id)')" }
  $objects += @($plugins | ForEach-Object { $_.Path })

  $exportsSource = ConvertTo-NormalizedPath (Join-Path $repository "src\pvd\Exports.cpp")
  foreach ($plugin in $plugins) {
    Assert-PluginProfile -PluginId $plugin.Id -Plugin $plugin.Path -ProfileFiles $profileFiles `
      -ExportsSource $exportsSource -LlvmProfdata $llvmProfdata -LlvmCov $llvmCov -WorkDirectory $buildDirectory
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

  # The gate spans the shared sources and every plugin's: src/** and plugins/*/src/**.
  $sourceRoots = @(Join-Path $repository "src") + @(
    Get-ChildItem -LiteralPath (Join-Path $repository "plugins") -Directory |
      ForEach-Object { Join-Path $_.FullName "src" } |
      Where-Object { Test-Path -LiteralPath $_ -PathType Container }
  )
  Write-Output "Coverage source roots: $($sourceRoots.Count)"
  $sourceRoots | ForEach-Object { Write-Output "  $_" }
  $requiredSources = @(
    $sourceRoots |
      ForEach-Object { Get-ChildItem -LiteralPath $_ -Recurse -File } |
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
