file(GLOB_RECURSE AVIFPVD_CORE_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp")

add_library(avifpvd_core STATIC ${AVIFPVD_CORE_SOURCES})
target_include_directories(avifpvd_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(avifpvd_core PUBLIC avifpvd_options)
