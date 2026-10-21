# Run with Windows PowerShell:
# Invoke-Pester -Script @{ Path = "tests/scripts/DistributionNotices.Tests.ps1"; Parameters = @{ RepositoryRoot = "<repository>" } }
param(
    [string] $RepositoryRoot
)

if ([string]::IsNullOrWhiteSpace($RepositoryRoot)) {
    $RepositoryRoot = Join-Path $PSScriptRoot "..\.."
}
$RepositoryRoot = (Resolve-Path -LiteralPath $RepositoryRoot).ProviderPath

$notices = @(
    @{
        Path = "third_party/resvg/LICENSES-resvg.txt"
        Length = 197864
        Sha256 = "195E6D92BC40D6276DA2722A160D5973967CCA14F06B904EF66022E68C536518"
    }
    @{
        Path = "third_party/resvg/RUST-COPYRIGHT-library.html"
        Length = 1499465
        Sha256 = "5647BE074C8EDF7339FD863055923A8FC80BC5610A8D4661EC3B767B9D392C27"
    }
    @{
        Path = "third_party/resvg/RUST-COPYRIGHT.html"
        Length = 14423589
        Sha256 = "D518AE2D76BC7A1F0CBF0069B8DEAFB56EFC39C2614CAB74A859EF0B467F8373"
    }
)

function Invoke-Git {
    param(
        [Parameter(Mandatory = $true)]
        [string] $WorkingDirectory,
        [Parameter(Mandatory = $true)]
        [string[]] $Arguments
    )

    # Merge native stderr as text without letting a benign Git warning terminate under the
    # caller's ErrorActionPreference. The actual exit code still decides success or failure.
    $ErrorActionPreference = "Continue"
    $output = @(& git -C $WorkingDirectory @Arguments 2>&1 | ForEach-Object { "$_" })
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "git $($Arguments -join ' ') failed with exit code $exitCode`n$($output -join "`n")"
    }
}

Describe "distribution notice checkout bytes" {
    It "preserves the reviewed notices when core.autocrlf is true" {
        $fixture = Join-Path $TestDrive "notice-checkout"
        $noticeDirectory = Join-Path $fixture "third_party\resvg"
        $checkout = Join-Path $fixture "checked-out"
        New-Item -ItemType Directory -Path $noticeDirectory, $checkout -Force | Out-Null

        Copy-Item -LiteralPath (Join-Path $RepositoryRoot ".gitattributes") -Destination $fixture
        foreach ($notice in $notices) {
            $relative = $notice.Path
            Copy-Item -LiteralPath (Join-Path $RepositoryRoot $relative) `
                -Destination (Join-Path $fixture $relative)
        }

        # A text control proves that checkout conversion is active in this Git fixture.
        $control = Join-Path $fixture "autocrlf-control.txt"
        [System.IO.File]::WriteAllText($control, "first line`nsecond line`n", [System.Text.UTF8Encoding]::new($false))

        Invoke-Git $fixture @("init", "--quiet")
        Invoke-Git $fixture @("config", "core.autocrlf", "true")
        $paths = @(".gitattributes", "autocrlf-control.txt") + @($notices | ForEach-Object { $_.Path })
        $addArguments = @("add", "--") + $paths
        Invoke-Git $fixture $addArguments

        $separator = [System.IO.Path]::DirectorySeparatorChar
        Invoke-Git $fixture @("checkout-index", "--all", "--prefix=$checkout$separator")

        $controlBytes = [System.IO.File]::ReadAllBytes((Join-Path $checkout "autocrlf-control.txt"))
        [System.Text.Encoding]::UTF8.GetString($controlBytes) | Should Be "first line`r`nsecond line`r`n"

        foreach ($notice in $notices) {
            $actual = Join-Path $checkout $notice.Path
            (Get-Item -LiteralPath $actual).Length | Should Be $notice.Length
            (Get-FileHash -LiteralPath $actual -Algorithm SHA256).Hash | Should Be $notice.Sha256
        }
    }
}

Describe "Git command failure handling" {
    It "throws when Git actually exits nonzero" {
        $fixture = Join-Path $TestDrive "git-failure"
        New-Item -ItemType Directory -Path $fixture -Force | Out-Null
        Invoke-Git $fixture @("init", "--quiet")

        { Invoke-Git $fixture @("rev-parse", "--verify", "refs/heads/does-not-exist") } | Should Throw
    }
}
