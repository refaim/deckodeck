# The LLVM bin directory of the toolchain (clang-cl, lld-link, llvm-rc; the scripts also take
# llvm-cov, llvm-profdata, llvm-readobj, clang-format and clang-tidy from it), resolved into the
# cache entry PVDKIT_LLVM_DIR. Order: the PVDKIT_LLVM_DIR environment variable (the CI build,
# lint and coverage jobs set it; the triplets pass it through to the vcpkg port builds), the
# VS 2022 layouts this project is known to build with - the reference machine's Build Tools
# first, so nothing changes there - then the LLVM of any Visual Studio major and edition under
# either Program Files root (Microsoft Visual Studio/*/*/VC/Tools/Llvm/x64/bin, the first in path
# order; the glob .github/actions/toolchain uses, which is how the VS 2026 runner image is found),
# and finally clang-cl on PATH. The Program Files roots are read from the ProgramFiles and
# ProgramFiles(x86) environment variables (vcpkg passes both through to its port builds; a probe
# can point the (x86) one at a fake tree, Windows re-derives the other for every child process).
# An explicit -DPVDKIT_LLVM_DIR works too, but prefer the environment variable: this file runs
# again, with a fresh cache, in every try_compile project CMake and vcpkg spawn, and only the
# environment reaches those (the
# CMAKE_TRY_COMPILE_PLATFORM_VARIABLES entry below forwards a -D value into CMake's own checks,
# not into vcpkg's port builds). A variable that names a directory without clang-cl.exe is an
# error, never a fallback. Included by both chainload toolchains, so the project and every vcpkg
# port resolve the same directory. scripts/llvm-dir.ps1 is the PowerShell counterpart with the
# same order.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES PVDKIT_LLVM_DIR)
if(PVDKIT_LLVM_DIR)
  if(NOT EXISTS "${PVDKIT_LLVM_DIR}/clang-cl.exe")
    message(FATAL_ERROR "PVDKIT_LLVM_DIR=${PVDKIT_LLVM_DIR} does not contain clang-cl.exe")
  endif()
else()
  set(_pvdkit_llvm_found "")
  if(DEFINED ENV{PVDKIT_LLVM_DIR} AND NOT "$ENV{PVDKIT_LLVM_DIR}" STREQUAL "")
    if(NOT EXISTS "$ENV{PVDKIT_LLVM_DIR}/clang-cl.exe")
      message(FATAL_ERROR "PVDKIT_LLVM_DIR=$ENV{PVDKIT_LLVM_DIR} (environment) does not contain clang-cl.exe")
    endif()
    set(_pvdkit_llvm_found "$ENV{PVDKIT_LLVM_DIR}")
  endif()
  # The two Program Files roots as CMake paths (empty when the variable is unset, e.g. on a
  # 32-bit host or in a probe that clears it).
  set(_pvdkit_program_files "$ENV{ProgramFiles}")
  set(_pvdkit_program_files_x86 "$ENV{ProgramFiles\(x86\)}")
  if(_pvdkit_program_files)
    file(TO_CMAKE_PATH "${_pvdkit_program_files}" _pvdkit_program_files)
  endif()
  if(_pvdkit_program_files_x86)
    file(TO_CMAKE_PATH "${_pvdkit_program_files_x86}" _pvdkit_program_files_x86)
  endif()
  if(NOT _pvdkit_llvm_found)
    foreach(_pvdkit_vs_root
            "${_pvdkit_program_files_x86}/Microsoft Visual Studio/2022/BuildTools"
            "${_pvdkit_program_files}/Microsoft Visual Studio/2022/Enterprise"
            "${_pvdkit_program_files}/Microsoft Visual Studio/2022/Professional"
            "${_pvdkit_program_files}/Microsoft Visual Studio/2022/Community"
            "${_pvdkit_program_files}/Microsoft Visual Studio/2022/BuildTools")
      if(EXISTS "${_pvdkit_vs_root}/VC/Tools/Llvm/x64/bin/clang-cl.exe")
        set(_pvdkit_llvm_found "${_pvdkit_vs_root}/VC/Tools/Llvm/x64/bin")
        break()
      endif()
    endforeach()
    unset(_pvdkit_vs_root)
  endif()
  if(NOT _pvdkit_llvm_found)
    set(_pvdkit_any_vs "")
    foreach(_pvdkit_root "${_pvdkit_program_files}" "${_pvdkit_program_files_x86}")
      if(_pvdkit_root)
        file(GLOB _pvdkit_root_matches "${_pvdkit_root}/Microsoft Visual Studio/*/*/VC/Tools/Llvm/x64/bin/clang-cl.exe")
        list(APPEND _pvdkit_any_vs ${_pvdkit_root_matches})
      endif()
    endforeach()
    if(_pvdkit_any_vs)
      list(SORT _pvdkit_any_vs)
      list(GET _pvdkit_any_vs 0 _pvdkit_any_vs)
      get_filename_component(_pvdkit_llvm_found "${_pvdkit_any_vs}" DIRECTORY)
    endif()
    unset(_pvdkit_root)
    unset(_pvdkit_root_matches)
    unset(_pvdkit_any_vs)
  endif()
  unset(_pvdkit_program_files)
  unset(_pvdkit_program_files_x86)
  if(NOT _pvdkit_llvm_found)
    find_program(_pvdkit_clang_cl_on_path NAMES clang-cl)
    if(_pvdkit_clang_cl_on_path)
      get_filename_component(_pvdkit_llvm_found "${_pvdkit_clang_cl_on_path}" DIRECTORY)
    endif()
    unset(_pvdkit_clang_cl_on_path CACHE)
  endif()
  if(NOT _pvdkit_llvm_found)
    message(FATAL_ERROR
            "clang-cl.exe was not found: set PVDKIT_LLVM_DIR (environment) to the LLVM bin "
            "directory of a Visual Studio installation (VC/Tools/Llvm/x64/bin) or put clang-cl on PATH")
  endif()
  file(TO_CMAKE_PATH "${_pvdkit_llvm_found}" _pvdkit_llvm_found)
  set(PVDKIT_LLVM_DIR "${_pvdkit_llvm_found}" CACHE PATH "LLVM bin directory (clang-cl, lld-link, llvm-rc, ...)")
  unset(_pvdkit_llvm_found)
endif()
