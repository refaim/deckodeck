file(GLOB_RECURSE PVDKIT_WIN_SOURCES CONFIGURE_DEPENDS "win/*.cpp" "win/*.hpp")

# The shared Win32 adapter: FileMapping / FileSource over CreateFileW + MapViewOfFile, UTF-8
# paths. Codec adapters live with their plugin (plugins/<id>/src/adapters/).
add_library(pvdkit_win STATIC ${PVDKIT_WIN_SOURCES})
target_include_directories(pvdkit_win PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(pvdkit_win PUBLIC pvdkit_core pvdkit_options)
