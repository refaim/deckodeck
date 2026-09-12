if(CMAKE_MAKE_PROGRAM)
  return()
endif()

find_program(_pvdkit_ninja_from_path NAMES ninja ninja-build)
if(_pvdkit_ninja_from_path)
  set(CMAKE_MAKE_PROGRAM "${_pvdkit_ninja_from_path}" CACHE FILEPATH "Ninja build program")
  unset(_pvdkit_ninja_from_path CACHE)
  return()
endif()

# Resolve the vcpkg root the same way cmake/vcpkg-root.cmake does ($ENV{VCPKG_ROOT} first, then
# the scoop install) instead of hard-coding the scoop path a second time. find-ninja.cmake is
# included before vcpkg-root.cmake computes its own copy of this, so it is re-derived here rather
# than passed in.
if(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
  set(_pvdkit_vcpkg_root "$ENV{VCPKG_ROOT}")
else()
  set(_pvdkit_vcpkg_root "C:/Users/Roma/scoop/apps/vcpkg/current")
endif()

set(_pvdkit_pinned_ninja
    "${_pvdkit_vcpkg_root}/downloads/tools/ninja-1.13.2-windows/ninja.exe")
file(GLOB _pvdkit_downloaded_ninjas LIST_DIRECTORIES FALSE
     "${_pvdkit_vcpkg_root}/downloads/tools/ninja-*/ninja.exe")
list(REMOVE_ITEM _pvdkit_downloaded_ninjas "${_pvdkit_pinned_ninja}")
if(_pvdkit_downloaded_ninjas)
  list(SORT _pvdkit_downloaded_ninjas COMPARE NATURAL ORDER DESCENDING)
  list(GET _pvdkit_downloaded_ninjas 0 _pvdkit_ninja)
elseif(EXISTS "${_pvdkit_pinned_ninja}")
  set(_pvdkit_ninja "${_pvdkit_pinned_ninja}")
else()
  message(FATAL_ERROR "Ninja was not found on PATH or under vcpkg's downloads/tools directory")
endif()

set(CMAKE_MAKE_PROGRAM "${_pvdkit_ninja}" CACHE FILEPATH "Ninja build program")
unset(_pvdkit_downloaded_ninjas)
unset(_pvdkit_ninja)
unset(_pvdkit_ninja_from_path CACHE)
unset(_pvdkit_pinned_ninja)
unset(_pvdkit_vcpkg_root)
