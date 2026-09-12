set(_avifpvd_llvm_bin
    "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/Llvm/x64/bin")

set(CMAKE_C_COMPILER "${_avifpvd_llvm_bin}/clang-cl.exe" CACHE FILEPATH "clang-cl C compiler")
set(CMAKE_CXX_COMPILER "${_avifpvd_llvm_bin}/clang-cl.exe" CACHE FILEPATH "clang-cl C++ compiler")
set(CMAKE_LINKER "${_avifpvd_llvm_bin}/lld-link.exe" CACHE FILEPATH "LLVM COFF linker" FORCE)
set(CMAKE_RC_COMPILER "${_avifpvd_llvm_bin}/llvm-rc.exe" CACHE FILEPATH "LLVM resource compiler" FORCE)
set(CMAKE_MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    CACHE STRING "Static MSVC runtime for every target" FORCE)

unset(_avifpvd_llvm_bin)
