@{
    # scripts/lint.ps1 runs Invoke-ScriptAnalyzer -Settings <this file> over scripts/ and
    # plugins/*/scripts/. Every warning and error fails the gate. No rule is excluded; an exclusion
    # added here needs a one-line reason next to it.
    Severity = @('Error', 'Warning')
    ExcludeRules = @()
}
