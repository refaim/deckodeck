# clang-cl / lld-link / llvm-rc from the LLVM directory cmake/find-llvm.cmake resolves
# (PVDKIT_LLVM_DIR: explicit, environment, the known VS 2022 layouts, PATH), with the static
# MSVC runtime for every target. Chainloaded by triplets/x64-windows-static-clang.cmake for the
# vcpkg ports and, through cmake/vcpkg-root.cmake, for this project.
include("${CMAKE_CURRENT_LIST_DIR}/find-llvm.cmake")

set(CMAKE_C_COMPILER "${PVDKIT_LLVM_DIR}/clang-cl.exe" CACHE FILEPATH "clang-cl C compiler")
set(CMAKE_CXX_COMPILER "${PVDKIT_LLVM_DIR}/clang-cl.exe" CACHE FILEPATH "clang-cl C++ compiler")
set(CMAKE_LINKER "${PVDKIT_LLVM_DIR}/lld-link.exe" CACHE FILEPATH "LLVM COFF linker" FORCE)
set(CMAKE_RC_COMPILER "${PVDKIT_LLVM_DIR}/llvm-rc.exe" CACHE FILEPATH "LLVM resource compiler" FORCE)
set(CMAKE_MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    CACHE STRING "Static MSVC runtime for every target" FORCE)
