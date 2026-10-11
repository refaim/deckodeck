add_subdirectory(core)
add_subdirectory(adapters)
add_subdirectory(e2e)

# ZIP reconstruction is called without CPU dispatch by OpenEXRCore. Its object must
# retain the toolchain's baseline ISA, including on pre-SSSE3/SSE4.1 processors.
add_test(
  NAME exr_zip_baseline
  COMMAND "${CMAKE_COMMAND}"
          "-DPVDKIT_ARCHIVE=$<TARGET_FILE:OpenEXR::OpenEXRCore>"
          "-DPVDKIT_LLVM_DIR=${PVDKIT_LLVM_DIR}"
          "-DPVDKIT_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/zip-baseline/$<CONFIG>"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/ZipBaseline.cmake")
set_tests_properties(exr_zip_baseline PROPERTIES TIMEOUT 30)
