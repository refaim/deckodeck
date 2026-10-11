# Run with Windows PowerShell: Invoke-Pester -Script tests/scripts/UpdateReadmeDownloads.Tests.ps1
$updater = Join-Path $PSScriptRoot "..\..\scripts\update-readme-downloads.ps1"

Describe "README release updates" {
  It "preserves descriptions and libraries with <Label> line endings" -TestCases @(
    @{ Label = "LF"; Newline = "`n" }
    @{ Label = "CRLF"; Newline = "`r`n" }
  ) {
    param($Label, $Newline)

    $readme = Join-Path $TestDrive "$Label.md"
    $before = @(
      "# Downloads"
      "<!-- downloads:begin -->"
      "| Plugin | Latest version | Release | Description | Libraries |"
      "| --- | --- | --- | --- | --- |"
      '| AVIF.pvd | 1.2.0 | [avif/v1.2.0](https://github.com/refaim/deckodeck/releases/tag/avif/v1.2.0) | Photos, HDR and animation | [libavif](https://github.com/AOMediaCodec/libavif), dav1d, libyuv |'
      '| EXR.pvd | 1.0.0 | [exr/v1.0.0](https://github.com/refaim/deckodeck/releases/tag/exr/v1.0.0) | HDR renders, layers and views | OpenEXR, Imath, libdeflate, OpenJPH |'
      "<!-- downloads:end -->"
      ""
    ) -join $Newline
    [IO.File]::WriteAllText($readme, $before, [Text.UTF8Encoding]::new($false))

    & $updater -Readme $readme -Name AVIF -Version 1.3.0 -Tag avif/v1.3.0 | Should Be "updated"
    $after = [IO.File]::ReadAllText($readme)
    $after | Should Be $before.Replace("avif/v1.2.0", "avif/v1.3.0").Replace("| 1.2.0 |", "| 1.3.0 |")
    [IO.File]::ReadAllBytes($readme)[0] | Should Be 35

    & $updater -Readme $readme -Name AVIF -Version 1.3.0 -Tag avif/v1.3.0 | Should Be "unchanged"
    [IO.File]::ReadAllText($readme) | Should Be $after
  }

  It "keeps an already current row and its extra cells unchanged" {
    $readme = Join-Path $TestDrive "current.md"
    $before = "<!-- downloads:begin -->`n| AVIF.pvd | 1.2.0 | [avif/v1.2.0](https://github.com/refaim/deckodeck/releases/tag/avif/v1.2.0) | Photos | libavif, dav1d, libyuv |`n<!-- downloads:end -->`n"
    [IO.File]::WriteAllText($readme, $before, [Text.UTF8Encoding]::new($false))

    & $updater -Readme $readme -Name AVIF -Version 1.2.0 -Tag avif/v1.2.0 | Should Be "unchanged"
    [IO.File]::ReadAllText($readme) | Should Be $before
  }

  It "does not downgrade a newer release" {
    $readme = Join-Path $TestDrive "newer.md"
    $before = "<!-- downloads:begin -->`n| AVIF.pvd | 1.3.0 | [avif/v1.3.0](https://github.com/refaim/deckodeck/releases/tag/avif/v1.3.0) | Photos | libavif |`n<!-- downloads:end -->`n"
    [IO.File]::WriteAllText($readme, $before, [Text.UTF8Encoding]::new($false))

    & $updater -Readme $readme -Name AVIF -Version 1.2.0 -Tag avif/v1.2.0 | Should Be "unchanged"
    [IO.File]::ReadAllText($readme) | Should Be $before
  }

  It "still updates the original three-column table" {
    $readme = Join-Path $TestDrive "original.md"
    $before = "<!-- downloads:begin -->`n| AVIF.pvd | 1.1.0 | [avif/v1.1.0](https://github.com/refaim/deckodeck/releases/tag/avif/v1.1.0) |`n<!-- downloads:end -->`n"
    [IO.File]::WriteAllText($readme, $before, [Text.UTF8Encoding]::new($false))

    & $updater -Readme $readme -Name AVIF -Version 1.2.0 -Tag avif/v1.2.0 | Should Be "updated"
    [IO.File]::ReadAllText($readme) | Should Be $before.Replace("1.1.0", "1.2.0")
  }

  It "appends a release when its plugin has no row yet" {
    $readme = Join-Path $TestDrive "append.md"
    $before = "<!-- downloads:begin -->`n| Plugin | Latest version | Release |`n| --- | --- | --- |`n<!-- downloads:end -->`n"
    [IO.File]::WriteAllText($readme, $before, [Text.UTF8Encoding]::new($false))

    & $updater -Readme $readme -Name AVIF -Version 1.2.0 -Tag avif/v1.2.0 | Should Be "updated"
    $row = "| AVIF.pvd | 1.2.0 | [avif/v1.2.0](https://github.com/refaim/deckodeck/releases/tag/avif/v1.2.0) |`n"
    [IO.File]::ReadAllText($readme) | Should Be $before.Replace("<!-- downloads:end -->", "$row<!-- downloads:end -->")
  }

  It "uses the default README path without touching the repository README" {
    $readme = Join-Path $TestDrive "default.md"
    $before = "<!-- downloads:begin -->`n| AVIF.pvd | 1.2.0 | [avif/v1.2.0](https://github.com/refaim/deckodeck/releases/tag/avif/v1.2.0) | Photos | libavif |`n<!-- downloads:end -->`n"
    [IO.File]::WriteAllText($readme, $before, [Text.UTF8Encoding]::new($false))
    Mock Join-Path { [IO.Path]::Combine($TestDrive, "default.md") } -ParameterFilter { $ChildPath -eq "..\README.md" }

    & $updater -Name AVIF -Version 1.2.0 -Tag avif/v1.2.0 | Should Be "unchanged"
    [IO.File]::ReadAllText($readme) | Should Be $before
    Assert-MockCalled Join-Path -Exactly -Times 1 -Scope It -ParameterFilter { $ChildPath -eq "..\README.md" }
  }

  It "rejects <Label> download anchors" -TestCases @(
    @{ Label = "missing"; Text = "# Downloads`n" }
    @{ Label = "missing begin"; Text = "<!-- downloads:end -->`n" }
    @{ Label = "missing end"; Text = "<!-- downloads:begin -->`n" }
    @{ Label = "reversed"; Text = "<!-- downloads:end -->`n<!-- downloads:begin -->`n" }
  ) {
    param($Label, $Text)

    $readme = Join-Path $TestDrive "$Label.md"
    [IO.File]::WriteAllText($readme, $Text, [Text.UTF8Encoding]::new($false))

    { & $updater -Readme $readme -Name AVIF -Version 1.2.0 -Tag avif/v1.2.0 } | Should Throw
    [IO.File]::ReadAllText($readme) | Should Be $Text
  }
}
