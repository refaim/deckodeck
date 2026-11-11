add_subdirectory(core)
add_subdirectory(adapters)
add_subdirectory(e2e)
add_test(
  NAME rpgmvp_webp_license
  COMMAND "${CMAKE_COMMAND}" "-DLICENSE_PATH=${CMAKE_CURRENT_BINARY_DIR}/../package/LICENSES.txt"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/CheckWebpLicense.cmake")
