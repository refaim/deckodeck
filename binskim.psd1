@{
    # Policy for the BinSkim step of scripts/lint.ps1 (binskim analyze <NAME>.pvd --level
    # "Error;Warning" --kind Fail --ignorePdbLoadError, one run per release DLL, both
    # architectures). Every rule result at one of these levels fails the gate unless its rule id is
    # listed in AcceptedResults with a reason. BinSkim's SARIF omits `level` on warning results;
    # lint.ps1 applies the SARIF 2.1.0 default (warning), so warnings are as fatal as errors.
    FailOnLevels = @('error', 'warning')

    # Rule results accepted with a reason (rule id = reason). Empty: nothing is accepted today.
    AcceptedResults = @{}

    # Tool-execution notifications accepted with a reason; any other error-level notification
    # fails the gate.
    AcceptedNotifications = @{
        # The Release presets link with /DEBUG:NONE (docs/ARCHITECTURE.md section 4): a plugin
        # ships without a PDB and the build produces none, so BinSkim cannot load one.
        #
        # What that costs (measured against a scratch clang-cl DLL built with /Zi /ZH:SHA_256 and
        # linked /DEBUG so BinSkim could read its PDB):
        # - rules that only pass or fail with a PDB and pass on our toolchain: BA2002
        #   DoNotIncorporateVulnerableDependencies, BA2004 EnableSecureSourceCodeHashing (needs
        #   /ZH:SHA_256), BA2006 BuildWithSecureTools, BA2007 EnableCriticalCompilerWarnings, BA2011
        #   EnableStackProtection, BA2013 InitializeStackProtection, BA2014
        #   DoNotDisableStackProtectionForFunctions;
        # - rules that would FAIL as warnings and are deliberately not done: BA2024
        #   EnableSpectreMitigations (clang-cl has no /Qspectre, and the static CRT we link is
        #   MSVC's default libcmt, not its /spectre variant), BA2025 EnableShadowStack (CET;
        #   lld-link accepts /CETCOMPAT, but the flag is only decisive on the host executable and
        #   0PictureView.dll / Far do not set it, so it is left off), BA2026
        #   EnableMicrosoftCompilerSdlSwitch (/sdl is an MSVC-only switch; clang-cl ignores it).
        # - BA2027 EnableSourceLink and BA6001-BA6006 (incremental linking, string pooling, COMDAT
        #   folding, /OPT:REF, LTCG) are notApplicable to an lld-link image even with a PDB: they
        #   read MSVC-linker records that lld-link does not write.
        #
        # Everything BinSkim reads from the image itself is checked and passes: BA2008 /guard:cf,
        # BA2009 /DYNAMICBASE, BA2016 /NXCOMPAT and BA2018 /SAFESEH (x86; notApplicable to x64 by
        # BinSkim's own classification), BA2012 stack cookie, BA2001 64-bit base above 4 GiB,
        # BA2005 no known-vulnerable binary, BA2010/BA2019/BA2021 section flags. BA2015
        # EnableHighEntropyVirtualAddresses is notApplicable to a DLL (the bit only counts on the
        # executable), so lint.ps1 reads IMAGE_DLL_CHARACTERISTICS_HIGH_ENTROPY_VA from every 64-bit
        # image's optional header with llvm-readobj instead and fails without it.
        'ERR997.ExceptionLoadingPdb' = 'Release links with /DEBUG:NONE by design; there is no PDB to load'
    }
}
