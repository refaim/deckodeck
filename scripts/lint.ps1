[CmdletBinding()]
param(
  # Configured build directory holding compile_commands.json and CMakeCache.txt (clang-tidy and
  # cppcheck consume the real clang-cl command lines). Defaults to build/debug<PVDKIT_BUILD_SUFFIX>.
  [string]$BuildDir,
  # Built Release directory whose plugins/<id>/<NAME>.pvd files BinSkim analyses. Defaults to
  # build/release<PVDKIT_BUILD_SUFFIX>.
  [string]$ReleaseDir,
  # Repository root (the analyzer configuration files and the source roots); this checkout by default.
  [string]$Root,
  # Which analyzers to run; all of them by default. Any of clang-format, clang-tidy, cppcheck,
  # psscriptanalyzer, binskim, as separate arguments or comma-separated (`-Tools clang-format,cppcheck`
  # reaches a script started with `powershell -File` as the one string "clang-format,cppcheck").
  [string[]]$Tools = @("clang-format", "clang-tidy", "cppcheck", "psscriptanalyzer", "binskim"),
  # Parallel clang-tidy processes: half the logical cores by default (at least 1), so an
  # interactive machine keeps half of them; every tool process also runs at BelowNormal priority.
  [int]$Jobs = [Math]::Max(1, [Math]::Floor([Environment]::ProcessorCount / 2)),
  # LLVM tools directory (clang-format, clang-tidy).
  [string]$LlvmDir = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin"
)

# The static-analysis gate: clang-format (--dry-run --Werror over src/**, plugins/*/src/**,
# tests/** and plugins/*/tests/**), clang-tidy (.clang-tidy, every translation unit of the
# compile database, run in parallel), cppcheck (the compile database restricted to src/ and
# plugins/*/src/, cppcheck-suppressions.txt), PSScriptAnalyzer (scripts/, plugins/*/scripts/ and
# the root .psd1 files, PSScriptAnalyzerSettings.psd1) and BinSkim (every release <NAME>.pvd,
# binskim.psd1; plus the /HIGHENTROPYVA bit of every 64-bit image, read with llvm-readobj because
# BinSkim's BA2015 does not apply to DLLs). Prints every finding as file:line:col: severity:
# message [rule] and a count per tool; exits 1 if any tool reported a finding or failed to run.
# Run it once per architecture (-BuildDir/-ReleaseDir pair); scripts/package.ps1 does so with its
# release directory as both before it zips anything.
$ErrorActionPreference = "Stop"

if (-not $Root) {
  # $PSScriptRoot is not yet set while parameter defaults are evaluated under powershell -File.
  $Root = Join-Path $PSScriptRoot ".."
}
$Root = (Resolve-Path -LiteralPath $Root).Path
if (-not $BuildDir) {
  $BuildDir = Join-Path $Root "build\debug$env:PVDKIT_BUILD_SUFFIX"
}
if (-not $ReleaseDir) {
  $ReleaseDir = Join-Path $Root "build\release$env:PVDKIT_BUILD_SUFFIX"
}
$BuildDir = [IO.Path]::GetFullPath($BuildDir)
$ReleaseDir = [IO.Path]::GetFullPath($ReleaseDir)
$LlvmDir = [IO.Path]::GetFullPath($LlvmDir)
$Jobs = [Math]::Max(1, $Jobs)
$knownTools = @("clang-format", "clang-tidy", "cppcheck", "psscriptanalyzer", "binskim")
$Tools = @($Tools | ForEach-Object { $_ -split "," } | ForEach-Object { $_.Trim().ToLowerInvariant() } | Where-Object { $_ })
foreach ($tool in $Tools) {
  if ($knownTools -notcontains $tool) {
    throw "unknown tool '$tool'; -Tools accepts $($knownTools -join ', ')"
  }
}
if ($Tools.Count -eq 0) {
  throw "-Tools names no tool"
}

function Get-Tool {
  param([string]$Name, [string]$Directory)

  if ($Directory) {
    $candidate = Join-Path $Directory "$Name.exe"
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
      throw "$Name was not found at $candidate"
    }
    return $candidate
  }
  $command = Get-Command $Name -ErrorAction SilentlyContinue
  if (-not $command) {
    throw "$Name was not found on PATH"
  }
  return $command.Source
}

# Runs a native tool with stderr merged into stdout. Windows PowerShell turns a redirected stderr
# line into a terminating error under $ErrorActionPreference = "Stop", and every analyzer prints
# its findings to stderr, so the preference is relaxed for the call only.
function Invoke-Native {
  param([string]$Command, [string[]]$Arguments)

  $ErrorActionPreference = "Continue"
  $output = @(& $Command @Arguments 2>&1 | ForEach-Object { "$_" })
  return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = $output }
}

# A JSON array property as an array, empty when the property is absent (@($null) would carry one
# null element).
function Get-JsonArray {
  param($Value)

  return @($Value | Where-Object { $null -ne $_ })
}

function Get-SourceFileList {
  param([string[]]$Directories, [string[]]$Extensions)

  $files = @()
  foreach ($directory in $Directories) {
    if (Test-Path -LiteralPath $directory -PathType Container) {
      $files += @(Get-ChildItem -LiteralPath $directory -Recurse -File | Where-Object { $Extensions -contains $_.Extension })
    }
  }
  return @($files | Sort-Object FullName -Unique | ForEach-Object { $_.FullName })
}

# <Root>/<Leaf> and every <Root>/plugins/<id>/<Leaf>.
function Get-RootDirectoryList {
  param([string]$Leaf)

  $directories = @((Join-Path $Root $Leaf))
  $plugins = Join-Path $Root "plugins"
  if (Test-Path -LiteralPath $plugins -PathType Container) {
    $directories += @(Get-ChildItem -LiteralPath $plugins -Directory | ForEach-Object { Join-Path $_.FullName $Leaf })
  }
  return $directories
}

# Translation units of the compile database that belong to this tree: C++ sources under $Root
# (the .rc entry and anything under the build directory are not for clang-tidy).
function Get-CompileDatabaseSourceList {
  $database = Join-Path $BuildDir "compile_commands.json"
  if (-not (Test-Path -LiteralPath $database -PathType Leaf)) {
    throw "$database does not exist: configure the build first (CMAKE_EXPORT_COMPILE_COMMANDS is on in every preset)"
  }
  $rootPrefix = $Root.TrimEnd("\") + "\"
  $buildPrefix = $BuildDir.TrimEnd("\") + "\"
  # Windows PowerShell's ConvertFrom-Json emits a JSON array as one object: unroll it.
  $entries = Get-JsonArray (Get-Content -LiteralPath $database -Raw | ConvertFrom-Json)
  return @(
    $entries |
      ForEach-Object { [IO.Path]::GetFullPath($_.file) } |
      Where-Object {
        $_.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase) -and
        -not $_.StartsWith($buildPrefix, [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Path]::GetExtension($_) -eq ".cpp"
      } |
      Sort-Object -Unique
  )
}

# The architecture the build directory was configured for, from its vcpkg triplet.
function Get-CppcheckPlatform {
  $cache = Join-Path $BuildDir "CMakeCache.txt"
  if (-not (Test-Path -LiteralPath $cache -PathType Leaf)) {
    throw "$cache does not exist: configure the build first"
  }
  $triplet = @(Get-Content -LiteralPath $cache | Where-Object { $_ -match '^VCPKG_TARGET_TRIPLET:\w+=(.+)$' } | ForEach-Object { $Matches[1] })
  if ($triplet.Count -ne 1) {
    throw "VCPKG_TARGET_TRIPLET is not set in $cache"
  }
  if ($triplet[0] -like "x86-*") {
    return "win32W"
  }
  return "win64"
}

function Invoke-ClangFormat {
  $clangFormat = Get-Tool "clang-format" $LlvmDir
  $files = @(Get-SourceFileList -Directories (@(Get-RootDirectoryList "src") + @(Get-RootDirectoryList "tests")) -Extensions @(".cpp", ".hpp"))
  if ($files.Count -eq 0) {
    throw "clang-format: no source files under $Root"
  }
  $findings = @()
  # One process per batch keeps the command line well under the 32 K limit.
  for ($start = 0; $start -lt $files.Count; $start += 40) {
    $batch = @($files[$start..([Math]::Min($start + 39, $files.Count - 1))])
    $run = Invoke-Native $clangFormat (@("--dry-run", "--Werror", "--style=file") + $batch)
    $batchFindings = @($run.Output | Where-Object { $_ -match '^(.+?):(\d+):(\d+): (error|warning): ' })
    if ($run.ExitCode -ne 0 -and $batchFindings.Count -eq 0) {
      throw "clang-format exited with $($run.ExitCode) without a diagnostic:`n$($run.Output -join "`n")"
    }
    $findings += $batchFindings
  }
  $findings | Write-Output
  return $findings.Count
}

function Invoke-ClangTidy {
  $clangTidy = Get-Tool "clang-tidy" $LlvmDir
  $sources = @(Get-CompileDatabaseSourceList)
  if ($sources.Count -eq 0) {
    throw "clang-tidy: the compile database in $BuildDir lists no source under $Root"
  }
  # One clang-tidy process per translation unit, $Jobs at a time (Windows PowerShell has no
  # ForEach-Object -Parallel, and run-clang-tidy needs a Python that is not installed).
  $pool = [runspacefactory]::CreateRunspacePool(1, $Jobs)
  $pool.Open()
  $tasks = @()
  $seen = @{}
  $findings = @()
  try {
    foreach ($source in $sources) {
      $shell = [powershell]::Create()
      $shell.RunspacePool = $pool
      [void]$shell.AddScript({
        param($Tool, $Database, $File)
        $output = @(& $Tool -p $Database --quiet $File 2>&1 | ForEach-Object { "$_" })
        [pscustomobject]@{ File = $File; ExitCode = $LASTEXITCODE; Output = $output }
      }).AddArgument($clangTidy).AddArgument($BuildDir).AddArgument($source)
      $tasks += [pscustomobject]@{ Shell = $shell; Handle = $shell.BeginInvoke() }
    }
    foreach ($task in $tasks) {
      $result = $task.Shell.EndInvoke($task.Handle)[0]
      $task.Shell.Dispose()
      $diagnostics = 0
      foreach ($line in $result.Output) {
        if ($line -match '^(?<file>.+?):(?<line>\d+):(?<col>\d+): (error|warning): .* \[(?<check>[^\]]+)\]$') {
          $diagnostics++
          # A header diagnostic repeats in every translation unit that includes it: report once.
          $key = ([IO.Path]::GetFullPath($Matches.file) + ":" + $Matches.line + ":" + $Matches.col + ":" + $Matches.check).ToLowerInvariant()
          if (-not $seen.ContainsKey($key)) {
            $seen[$key] = $true
            $findings += $line
          }
        }
      }
      if ($result.ExitCode -ne 0 -and $diagnostics -eq 0) {
        throw "clang-tidy exited with $($result.ExitCode) on $($result.File) without a diagnostic:`n$($result.Output -join "`n")"
      }
    }
  } finally {
    $pool.Close()
    $pool.Dispose()
  }
  $findings | Write-Output
  return $findings.Count
}

function Invoke-Cppcheck {
  $cppcheck = Get-Tool "cppcheck"
  $database = Join-Path $BuildDir "compile_commands.json"
  if (-not (Test-Path -LiteralPath $database -PathType Leaf)) {
    throw "$database does not exist: configure the build first"
  }
  $platform = Get-CppcheckPlatform
  $arguments = @(
    "--project=$database",
    # src/** and plugins/*/src/**; the tests are not checked.
    "--file-filter=*/src/*",
    "--enable=warning,performance,portability",
    "--std=c++23",
    "--platform=$platform",
    "--inline-suppr",
    "--error-exitcode=1",
    "--library=windows",
    "--suppressions-list=$(Join-Path $Root 'cppcheck-suppressions.txt')",
    "--template={file}:{line}:{column}: {severity}: {message} [{id}]",
    "--quiet"
  )
  $run = Invoke-Native $cppcheck $arguments
  $findings = @($run.Output | Where-Object { $_ -match '^(.+?):(\d+):(\d+): (error|warning|performance|portability|style|information): .* \[[\w-]+\]$' })
  if ($run.ExitCode -ne 0 -and $findings.Count -eq 0) {
    throw "cppcheck exited with $($run.ExitCode) without a diagnostic:`n$($run.Output -join "`n")"
  }
  $findings | Write-Output
  return $findings.Count
}

function Invoke-PSScriptAnalyzer {
  Import-Module PSScriptAnalyzer -ErrorAction Stop
  $settings = Join-Path $Root "PSScriptAnalyzerSettings.psd1"
  $directories = @(Get-RootDirectoryList "scripts" | Where-Object { Test-Path -LiteralPath $_ -PathType Container })
  $files = @(Get-ChildItem -LiteralPath $Root -File -Filter "*.psd1" | ForEach-Object { $_.FullName })
  $diagnostics = @()
  foreach ($directory in $directories) {
    $diagnostics += @(Invoke-ScriptAnalyzer -Path $directory -Recurse -Settings $settings)
  }
  foreach ($file in $files) {
    $diagnostics += @(Invoke-ScriptAnalyzer -Path $file -Settings $settings)
  }
  $findings = @($diagnostics | ForEach-Object {
    "$($_.ScriptPath):$($_.Line):$($_.Column): $($_.Severity.ToString().ToLowerInvariant()): $($_.Message) [$($_.RuleName)]"
  })
  $findings | Write-Output
  return $findings.Count
}

# The level of a SARIF result. BinSkim writes `level` on errors only and its rule objects carry no
# `defaultConfiguration`, so the SARIF 2.1.0 defaults apply (section 3.27.10): with `kind` absent
# or "fail" the level is "warning", with any other kind (pass, notApplicable, ...) it is "none".
function Get-SarifResultLevel {
  param($Result, $Rule)

  if ($Result.level) {
    return $Result.level
  }
  if ($Rule -and $Rule.defaultConfiguration -and $Rule.defaultConfiguration.level) {
    return $Rule.defaultConfiguration.level
  }
  if ($Result.kind -and $Result.kind -ne "fail") {
    return "none"
  }
  return "warning"
}

# The first line of a SARIF message: its `text`, or the rule's `messageStrings.<id>` with the
# message `arguments` substituted for {0}, {1}, ... (the raw string otherwise).
function Get-SarifMessageText {
  param($Message, $Rule)

  $text = $Message.text
  if (-not $text -and $Message.id -and $Rule) {
    $text = $Rule.messageStrings.($Message.id).text
    $arguments = @(Get-JsonArray $Message.arguments | ForEach-Object { "$_" })
    # Substitute what the arguments cover; a placeholder beyond them stays literal rather than
    # letting String.Format throw over a malformed message string.
    for ($index = 0; $text -and $index -lt $arguments.Count; $index++) {
      $text = $text.Replace("{$index}", $arguments[$index])
    }
  }
  return "$(("$text" -split "`r?`n")[0])"
}

function Invoke-BinSkim {
  $binskim = Get-Tool "binskim"
  $policy = Import-PowerShellDataFile -LiteralPath (Join-Path $Root "binskim.psd1")
  $pluginsDir = Join-Path $ReleaseDir "plugins"
  $plugins = @()
  if (Test-Path -LiteralPath $pluginsDir -PathType Container) {
    $plugins = @(Get-ChildItem -LiteralPath $pluginsDir -Recurse -File -Filter "*.pvd" | ForEach-Object { $_.FullName })
  }
  if ($plugins.Count -eq 0) {
    throw "BinSkim: no <NAME>.pvd under $pluginsDir; build the release preset first"
  }
  $reports = Join-Path $ReleaseDir "lint"
  New-Item -ItemType Directory -Force -Path $reports | Out-Null
  $findings = @()
  foreach ($plugin in $plugins) {
    $report = Join-Path $reports ([IO.Path]::GetFileNameWithoutExtension($plugin) + ".binskim.sarif")
    # The boolean switches go last: BinSkim's parser rejects a switch followed by a valued option.
    $run = Invoke-Native $binskim @("analyze", $plugin, "--output", $report, "--log", "ForceOverwrite",
                                    "--level", "Error;Warning", "--kind", "Fail", "--ignorePdbLoadError", "--disable-telemetry")
    if (-not (Test-Path -LiteralPath $report -PathType Leaf)) {
      throw "BinSkim did not write $report (exit $($run.ExitCode)):`n$($run.Output -join "`n")"
    }
    $pluginFindings = @()
    $sarif = Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
    foreach ($sarifRun in (Get-JsonArray $sarif.runs)) {
      $rules = @{}
      foreach ($rule in (Get-JsonArray $sarifRun.tool.driver.rules)) {
        $rules[$rule.id] = $rule
      }
      foreach ($result in (Get-JsonArray $sarifRun.results)) {
        $level = Get-SarifResultLevel $result $rules[$result.ruleId]
        if ($policy.FailOnLevels -notcontains $level -or $policy.AcceptedResults.ContainsKey($result.ruleId)) {
          continue
        }
        $text = Get-SarifMessageText $result.message $rules[$result.ruleId]
        $pluginFindings += "$($plugin):0:0: $($level): $text [$($result.ruleId) $($rules[$result.ruleId].name)]"
      }
      foreach ($invocation in (Get-JsonArray $sarifRun.invocations)) {
        foreach ($notification in ((Get-JsonArray $invocation.toolExecutionNotifications) + (Get-JsonArray $invocation.toolConfigurationNotifications))) {
          $id = $notification.descriptor.id
          if ($notification.level -ne "error" -or $policy.AcceptedNotifications.ContainsKey($id)) {
            continue
          }
          $pluginFindings += "$($plugin):0:0: error: $($notification.message.text) [$id]"
        }
      }
    }
    if ($run.ExitCode -ne 0 -and $pluginFindings.Count -eq 0) {
      throw "BinSkim exited with $($run.ExitCode) on $plugin without a finding:`n$($run.Output -join "`n")"
    }
    $findings += $pluginFindings
    $findings += @(Test-HighEntropyVirtualAddressBit $plugin)
  }
  $findings | Write-Output
  return $findings.Count
}

# BinSkim's BA2015 (EnableHighEntropyVirtualAddresses) is not applicable to a DLL - the flag is
# only decisive on the executable - so the /HIGHENTROPYVA bit of a 64-bit image is read directly
# from its optional header (a 32-bit image cannot carry it). Returns a finding line or nothing.
function Test-HighEntropyVirtualAddressBit {
  param([string]$Image)

  $readobj = Get-Tool "llvm-readobj" $LlvmDir
  $run = Invoke-Native $readobj @("--file-headers", $Image)
  if ($run.ExitCode -ne 0) {
    throw "llvm-readobj exited with $($run.ExitCode) on ${Image}:`n$($run.Output -join "`n")"
  }
  $is64 = [bool]($run.Output | Where-Object { $_ -match '^\s*Magic:\s*0x20B\s*$' })
  $highEntropy = [bool]($run.Output | Where-Object { $_ -match 'IMAGE_DLL_CHARACTERISTICS_HIGH_ENTROPY_VA' })
  if ($is64 -and -not $highEntropy) {
    return "$($Image):0:0: error: 64-bit image without IMAGE_DLL_CHARACTERISTICS_HIGH_ENTROPY_VA (link with /HIGHENTROPYVA) [HighEntropyVA]"
  }
}

$steps = [ordered]@{
  "clang-format" = { Invoke-ClangFormat }
  "clang-tidy" = { Invoke-ClangTidy }
  "cppcheck" = { Invoke-Cppcheck }
  "psscriptanalyzer" = { Invoke-PSScriptAnalyzer }
  "binskim" = { Invoke-BinSkim }
}
$labels = @{
  "clang-format" = "clang-format"
  "clang-tidy" = "clang-tidy"
  "cppcheck" = "cppcheck"
  "psscriptanalyzer" = "PSScriptAnalyzer"
  "binskim" = "BinSkim"
}

$summary = @()
$total = 0
# Every tool (the clang-tidy children of the runspace pool included) is started from this process
# and inherits its priority class, so lowering it here keeps the machine responsive while the
# analyzers run; the previous priority comes back before the script returns.
$self = [Diagnostics.Process]::GetCurrentProcess()
$previousPriority = $self.PriorityClass
$self.PriorityClass = [Diagnostics.ProcessPriorityClass]::BelowNormal
try {
  Write-Output "lint: jobs=$Jobs priority=$($self.PriorityClass)"
  foreach ($name in $steps.Keys) {
    if ($Tools -notcontains $name) {
      continue
    }
    $label = $labels[$name]
    Write-Output "== $label"
    $stopwatch = [Diagnostics.Stopwatch]::StartNew()
    # A step emits its findings followed by their count.
    $results = @(& $steps[$name])
    $count = [int]$results[-1]
    if ($results.Count -gt 1) {
      $results[0..($results.Count - 2)] | Write-Output
    }
    $total += $count
    $line = [string]::Format([Globalization.CultureInfo]::InvariantCulture, "{0}: {1} finding(s) in {2:0.0} s",
                             $label, $count, $stopwatch.Elapsed.TotalSeconds)
    Write-Output $line
    $summary += $line
  }
} finally {
  $self.PriorityClass = $previousPriority
}

Write-Output "== summary"
$summary | Write-Output
if ($total -ne 0) {
  [Console]::Error.WriteLine("lint: $total finding(s)")
  exit 1
}
Write-Output "lint: clean"
