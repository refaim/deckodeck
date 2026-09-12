file(GLOB_RECURSE AVIFPVD_ADAPTER_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp")

add_library(avifpvd_adapters STATIC ${AVIFPVD_ADAPTER_SOURCES})
target_include_directories(avifpvd_adapters PUBLIC "${PROJECT_SOURCE_DIR}/src")
# The vcpkg `avif` target carries the configuration-correct static dav1d archive, and
# libavif-config.cmake's find_dependency(libyuv) defines the `yuv` target it links.
target_link_libraries(avifpvd_adapters PUBLIC avif yuv avifpvd_core avifpvd_options)
