# Same as clang-cl.toolchain.cmake but targets 32-bit x86. clang-cl.exe from the x64 LLVM
# directory is a cross compiler: CMAKE_<LANG>_COMPILER_TARGET=i686-pc-windows-msvc makes it emit
# i386 objects, CMake then invokes lld-link with /machine:X86, and lld-link locates the x86
# CRT/SDK libraries through its own MSVC auto-detection. vcpkg ports additionally get the x86
# vcvars environment from the triplet (VCPKG_LOAD_VCVARS_ENV) for meson-based dav1d.
set(_avifpvd_llvm_bin
    "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/Llvm/x64/bin")

set(CMAKE_C_COMPILER "${_avifpvd_llvm_bin}/clang-cl.exe" CACHE FILEPATH "clang-cl C compiler")
set(CMAKE_CXX_COMPILER "${_avifpvd_llvm_bin}/clang-cl.exe" CACHE FILEPATH "clang-cl C++ compiler")
set(CMAKE_C_COMPILER_TARGET i686-pc-windows-msvc CACHE STRING "32-bit target" FORCE)
set(CMAKE_CXX_COMPILER_TARGET i686-pc-windows-msvc CACHE STRING "32-bit target" FORCE)
set(CMAKE_LINKER "${_avifpvd_llvm_bin}/lld-link.exe" CACHE FILEPATH "LLVM COFF linker" FORCE)
set(CMAKE_RC_COMPILER "${_avifpvd_llvm_bin}/llvm-rc.exe" CACHE FILEPATH "LLVM resource compiler" FORCE)
set(CMAKE_MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    CACHE STRING "Static MSVC runtime for every target" FORCE)

unset(_avifpvd_llvm_bin)
