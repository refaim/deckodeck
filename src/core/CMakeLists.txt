file(GLOB_RECURSE PVDKIT_CORE_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp")

# Format-neutral decisions: CodecPlugin, FileSession, Transform, PixelBuffer, Narrow, Error and
# the IDecoder / IFileSource / IImageDescriber contracts the plugins implement.
add_library(pvdkit_core STATIC ${PVDKIT_CORE_SOURCES})
target_include_directories(pvdkit_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(pvdkit_core PUBLIC pvdkit_options)
