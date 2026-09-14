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
