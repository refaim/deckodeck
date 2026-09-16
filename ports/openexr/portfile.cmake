# This overlay port pins openexr 3.4.13#2 (see vcpkg.json in this directory) independently of the
# top-level vcpkg.json's "builtin-baseline": it is the stock vcpkg port plus two build patches.
# clang-cl-zip-sse4.patch: OpenEXRCore's internal_zip.c selects its SSE4.1 byte-reconstruction
# kernel whenever _MSC_VER is defined on x86/x64 - cl.exe accepts any intrinsic regardless of
# /arch - but clang-cl defines _MSC_VER too and refuses to inline _mm_shuffle_epi8 /
# _mm_extract_epi8 into a function compiled without the ssse3/sse4.1 target features, so the
# port did not build with this repository's triplets. The patch gives that one function a
# `target("ssse3,sse4.1")` attribute under clang, which reproduces exactly what the MSVC build
# does (the kernel is not dispatched at run time upstream either; every x86-64 CPU since 2008
# has SSE4.1). clang-cl-x86-interlocked.patch is explained next to its PATCHES entry below.
# Moving the baseline does not update this port; bump version/port-version here by hand (and
# re-verify SHA512 below) when a newer OpenEXR is needed.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO AcademySoftwareFoundation/openexr
    REF "v${VERSION}"
    SHA512 da3310f9c3f8b927c7f8fca9edeb381f16e5a492298ae19a3f9d54fa46859542a71ca923a7806ed40a9bbddea34e15cfaa25f9a07a288cc70f0e0fd267a52729
    HEAD_REF main
    PATCHES
        clang-cl-zip-sse4.patch
        # clang >= 20 makes -Wincompatible-pointer-types an error; the 32-bit atomic helpers of
        # internal_structs.h pass uintptr_t* to InterlockedOr/CompareExchange (upstream x86 bug).
        clang-cl-x86-interlocked.patch
)

vcpkg_check_features(OUT_FEATURE_OPTIONS OPTIONS
    FEATURES
        tools   OPENEXR_BUILD_TOOLS
        tools   OPENEXR_INSTALL_TOOLS
)
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${OPTIONS}
        -DBUILD_TESTING=OFF
        -DBUILD_WEBSITE=OFF
        -DCMAKE_REQUIRE_FIND_PACKAGE_libdeflate=ON
        -DOPENEXR_BUILD_EXAMPLES=OFF
        -DOPENEXR_INSTALL_PKG_CONFIG=ON
    OPTIONS_DEBUG
        -DOPENEXR_BUILD_TOOLS=OFF
        -DOPENEXR_INSTALL_TOOLS=OFF
)
vcpkg_cmake_install()
vcpkg_copy_pdbs()

vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/OpenEXR)

vcpkg_fixup_pkgconfig()

if(OPENEXR_INSTALL_TOOLS)
    vcpkg_copy_tools(
        TOOL_NAMES
            exr2aces
            # not installed: exrcheck
            exrenvmap
            exrheader
            exrinfo
            exrmakepreview
            exrmaketiled
            exrmanifest
            exrmetrics
            exrmultipart
            exrmultiview
            exrstdattr
        AUTO_CLEAN
    )
endif()

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.md")
