# Pester 3.4 exposes per-test outcomes in TestResult.Result. Its result object has no
# InconclusiveCount property, so callers must not treat a null count as zero.

function Get-PesterSummary {
  param([object]$Result)

  $tests = @()
  if ($null -ne $Result) {
    $tests = @($Result.TestResult | Where-Object { $null -ne $_ })
  }

  $passed = @($tests | Where-Object { $_.Result -eq "Passed" -or $_.Result -eq "Success" }).Count
  $failed = @($tests | Where-Object { $_.Result -eq "Failed" -or $_.Result -eq "Failure" }).Count
  $inconclusive = @($tests | Where-Object { $_.Result -eq "Inconclusive" }).Count
  $skipped = @($tests | Where-Object { $_.Result -eq "Ignored" -or $_.Result -eq "Skipped" }).Count
  $pending = @($tests | Where-Object { $_.Result -eq "Pending" }).Count
  $unknown = $tests.Count - $passed - $failed - $inconclusive - $skipped - $pending

  return [pscustomobject]@{
    Total = $tests.Count
    Passed = $passed
    Failed = $failed
    Inconclusive = $inconclusive
    Skipped = $skipped
    Pending = $pending
    Unknown = $unknown
  }
}

function Test-PesterSummarySuccessful {
  param([object]$Summary)

  return (
    $null -ne $Summary -and
    $Summary.Total -gt 0 -and
    $Summary.Passed -eq $Summary.Total -and
    $Summary.Failed -eq 0 -and
    $Summary.Inconclusive -eq 0 -and
    $Summary.Skipped -eq 0 -and
    $Summary.Pending -eq 0 -and
    $Summary.Unknown -eq 0
  )
}
