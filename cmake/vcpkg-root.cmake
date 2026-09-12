include("${CMAKE_CURRENT_LIST_DIR}/find-ninja.cmake")

if(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
  set(_pvdkit_vcpkg_root "$ENV{VCPKG_ROOT}")
else()
  set(_pvdkit_vcpkg_root "C:/Users/Roma/scoop/apps/vcpkg/current")
endif()

# The triplet file is the one place that names the chainload toolchain for its architecture
# (x64: cmake/clang-cl.toolchain.cmake, x86: cmake/clang-cl-x86.toolchain.cmake). vcpkg reads it
# for the ports; reading the same file here hands the same VCPKG_CHAINLOAD_TOOLCHAIN_FILE to this
# project, so a preset only has to choose VCPKG_TARGET_TRIPLET.
if(NOT DEFINED VCPKG_TARGET_TRIPLET)
  set(VCPKG_TARGET_TRIPLET "x64-windows-static-clang" CACHE STRING "vcpkg target triplet")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../triplets/${VCPKG_TARGET_TRIPLET}.cmake")

# Each plugin's codec libraries are a vcpkg manifest feature named after the plugin id
# (vcpkg.json: default-features lists every plugin). Restricting the build with
# -DPVDKIT_PLUGINS=<ids> restricts the manifest install to those features as well.
if(DEFINED PVDKIT_PLUGINS AND NOT PVDKIT_PLUGINS STREQUAL "")
  set(VCPKG_MANIFEST_FEATURES ${PVDKIT_PLUGINS})
  set(VCPKG_MANIFEST_NO_DEFAULT_FEATURES ON)
endif()

include("${_pvdkit_vcpkg_root}/scripts/buildsystems/vcpkg.cmake")

unset(_pvdkit_vcpkg_root)
