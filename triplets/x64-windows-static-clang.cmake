set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE
    "${CMAKE_CURRENT_LIST_DIR}/../cmake/clang-cl.toolchain.cmake")
# vcpkg builds every port in a cleaned environment; the chainload toolchain resolves the LLVM
# directory through cmake/find-llvm.cmake, whose first choice is this variable (untracked: the
# compiler itself is already part of every port's ABI hash through vcpkg's compiler detection).
set(VCPKG_ENV_PASSTHROUGH_UNTRACKED PVDKIT_LLVM_DIR)

# Note: VCPKG_LOAD_VCVARS_ENV is deliberately left off here. dav1d's meson build activates
# vcvars64.bat on its own when no VS environment is present (mesonbuild/utils/vsenv.py), which is
# correct for x64. The x86 triplet must load vcvars explicitly because meson's self-activation
# always picks the 64-bit environment. Turning this on for x64 would only invalidate the cache.

# The chainload toolchain selects the static CRT through CMAKE_MSVC_RUNTIME_LIBRARY, which a
# port's CMake honours only under policy CMP0091 (CMake 3.15). Ports whose cmake_minimum_required
# is older (OpenEXR, Imath, libdeflate, OpenJPH declare 3.10-3.14) silently fall back to CMake's
# default /MD flags, and the plugin would then import the dynamic CRT. Setting the policy default
# on every port's configure line makes the toolchain's choice apply everywhere (the libspng
# overlay port carries the same option; the two agree).
set(VCPKG_CMAKE_CONFIGURE_OPTIONS -DCMAKE_POLICY_DEFAULT_CMP0091=NEW)
