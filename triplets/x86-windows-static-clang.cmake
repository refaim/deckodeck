set(VCPKG_TARGET_ARCHITECTURE x86)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE
    "${CMAKE_CURRENT_LIST_DIR}/../cmake/clang-cl-x86.toolchain.cmake")

# vcpkg does not load the Visual Studio environment for a chainloaded toolchain. Without it,
# meson (dav1d's build system) activates vcvars64.bat on its own (mesonbuild/utils/vsenv.py: no
# VSINSTALLDIR and no compiler on PATH), so LIB names the x64 CRT while clang-cl compiles for
# i686 and the compiler sanity check dies with "unresolved external symbol _mainCRTStartup" /
# LNK4272. Loading the x86 vcvars here gives every port (meson and CMake alike) an x86 LIB and a
# VSINSTALLDIR that stops meson's own activation. The compilers are still clang-cl/lld-link from
# the chainload above; vcvars only supplies the library environment.
set(VCPKG_LOAD_VCVARS_ENV ON)
