if(CMAKE_MAKE_PROGRAM)
  return()
endif()

find_program(_avifpvd_ninja_from_path NAMES ninja ninja-build)
if(_avifpvd_ninja_from_path)
  set(CMAKE_MAKE_PROGRAM "${_avifpvd_ninja_from_path}" CACHE FILEPATH "Ninja build program")
  unset(_avifpvd_ninja_from_path CACHE)
  return()
endif()

# Resolve the vcpkg root the same way cmake/vcpkg-root.cmake does ($ENV{VCPKG_ROOT} first, then
# the scoop install) instead of hard-coding the scoop path a second time. find-ninja.cmake is
# included before vcpkg-root.cmake computes its own copy of this, so it is re-derived here rather
# than passed in.
if(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
  set(_avifpvd_vcpkg_root "$ENV{VCPKG_ROOT}")
else()
  set(_avifpvd_vcpkg_root "C:/Users/Roma/scoop/apps/vcpkg/current")
endif()

set(_avifpvd_pinned_ninja
    "${_avifpvd_vcpkg_root}/downloads/tools/ninja-1.13.2-windows/ninja.exe")
file(GLOB _avifpvd_downloaded_ninjas LIST_DIRECTORIES FALSE
     "${_avifpvd_vcpkg_root}/downloads/tools/ninja-*/ninja.exe")
list(REMOVE_ITEM _avifpvd_downloaded_ninjas "${_avifpvd_pinned_ninja}")
if(_avifpvd_downloaded_ninjas)
  list(SORT _avifpvd_downloaded_ninjas COMPARE NATURAL ORDER DESCENDING)
  list(GET _avifpvd_downloaded_ninjas 0 _avifpvd_ninja)
elseif(EXISTS "${_avifpvd_pinned_ninja}")
  set(_avifpvd_ninja "${_avifpvd_pinned_ninja}")
else()
  message(FATAL_ERROR "Ninja was not found on PATH or under vcpkg's downloads/tools directory")
endif()

set(CMAKE_MAKE_PROGRAM "${_avifpvd_ninja}" CACHE FILEPATH "Ninja build program")
unset(_avifpvd_downloaded_ninjas)
unset(_avifpvd_ninja)
unset(_avifpvd_ninja_from_path CACHE)
unset(_avifpvd_pinned_ninja)
unset(_avifpvd_vcpkg_root)
