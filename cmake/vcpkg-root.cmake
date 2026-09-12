include("${CMAKE_CURRENT_LIST_DIR}/find-ninja.cmake")

if(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
  set(_avifpvd_vcpkg_root "$ENV{VCPKG_ROOT}")
else()
  set(_avifpvd_vcpkg_root "C:/Users/Roma/scoop/apps/vcpkg/current")
endif()

set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE
    "${CMAKE_CURRENT_LIST_DIR}/clang-cl.toolchain.cmake"
    CACHE FILEPATH "AVIF.pvd clang-cl chainload toolchain")
include("${_avifpvd_vcpkg_root}/scripts/buildsystems/vcpkg.cmake")

unset(_avifpvd_vcpkg_root)
