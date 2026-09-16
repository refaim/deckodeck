# This overlay port pins imath 3.2.2#1 (see vcpkg.json in this directory) independently of the
# top-level vcpkg.json's "builtin-baseline": it is the stock vcpkg port with one option changed,
# IMATH_HALF_USE_LOOKUP_TABLE=OFF. With the table on (the default), every half-to-float
# conversion in Imath's headers reads imath_half_to_float_table, a 256 KiB array that half.cpp
# defines; half.cpp also includes <iostream>, so its object carries the std::locale::id dynamic
# initialisers of the MSVC STL, whose guards pull libcmt's thread_safe_statics.obj - the object
# that imports WaitOnAddress/WakeByAddressAll from api-ms-win-core-synch-l1-2-0.dll on MSVC >=
# 14.50 (docs/ARCHITECTURE.md par. 7, rule 13). A plugin DLL imports KERNEL32.dll only, and its
# release import gate must not depend on the linker discarding that object as dead code, so the
# table is switched off here: ImathConfig.h then leaves IMATH_HALF_USE_LOOKUP_TABLE undefined,
# the conversion is the bit-exact arithmetic in half.h (a few integer operations, the same values
# for every half) for every consumer - OpenEXR's objects and the plugins' - and half.cpp.obj is
# never referenced (plugins/exr/DESIGN.md, "Library"). Moving the baseline does not update this
# port; bump version/port-version here by hand (and re-verify SHA512 below) when a newer Imath is
# needed.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO AcademySoftwareFoundation/Imath
    REF "v${VERSION}"
    SHA512 492a624e4c0b59685d1ea58a3c2c63ddb4ba5ab9177c7d2a1b7e80be95d38ce02c74fafd2fe0982f7d21e5e75c938cc24a33a12d827dec32727cb8dcd5066450
    HEAD_REF master
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DIMATH_INSTALL_SYM_LINK=OFF
        -DBUILD_TESTING=OFF
        -DIMATH_INSTALL_PKG_CONFIG=ON
        -DIMATH_HALF_USE_LOOKUP_TABLE=OFF
)

vcpkg_cmake_install()

vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/Imath)
vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

file(INSTALL "${SOURCE_PATH}/LICENSE.md" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
