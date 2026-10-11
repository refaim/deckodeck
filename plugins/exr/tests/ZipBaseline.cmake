# Inspect the linked codec archive, rather than the overlay patch's spelling.
# LLVM's tools handle both x86 and x64 COFF, and TARGET_FILE selects each preset's
# actual Release/Debug dependency. Only the non-dispatched ZIP object is checked:
# other OpenEXR objects may legitimately contain CPU-dispatched SIMD kernels.
cmake_minimum_required(VERSION 3.28)

foreach(required PVDKIT_ARCHIVE PVDKIT_LLVM_DIR PVDKIT_WORK_DIR)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()
if(NOT EXISTS "${PVDKIT_ARCHIVE}")
  message(FATAL_ERROR "OpenEXRCore archive does not exist: ${PVDKIT_ARCHIVE}")
endif()
foreach(tool llvm-ar llvm-objdump)
  if(NOT EXISTS "${PVDKIT_LLVM_DIR}/${tool}.exe")
    message(FATAL_ERROR "${tool}.exe was not found in PVDKIT_LLVM_DIR=${PVDKIT_LLVM_DIR}")
  endif()
endforeach()

execute_process(
  COMMAND "${PVDKIT_LLVM_DIR}/llvm-ar.exe" t "${PVDKIT_ARCHIVE}"
  RESULT_VARIABLE listed
  OUTPUT_VARIABLE members
  ERROR_VARIABLE listing_error)
if(NOT listed EQUAL 0)
  message(FATAL_ERROR "Cannot list OpenEXRCore archive: ${listing_error}")
endif()
string(REPLACE "\r\n" "\n" members "${members}")
string(REPLACE "\n" ";" members "${members}")
set(zip_members)
foreach(member IN LISTS members)
  if(member MATCHES "(^|[/\\])internal_zip\\.c\\.(obj|o)$")
    list(APPEND zip_members "${member}")
  endif()
endforeach()
list(LENGTH zip_members zip_count)
if(NOT zip_count EQUAL 1)
  message(FATAL_ERROR "Expected one internal_zip.c object in ${PVDKIT_ARCHIVE}, found ${zip_count}")
endif()
list(GET zip_members 0 zip_member)

file(MAKE_DIRECTORY "${PVDKIT_WORK_DIR}")
set(zip_object "${PVDKIT_WORK_DIR}/internal_zip.c.obj")
execute_process(
  COMMAND "${PVDKIT_LLVM_DIR}/llvm-ar.exe" p "${PVDKIT_ARCHIVE}" "${zip_member}"
  RESULT_VARIABLE extracted
  OUTPUT_FILE "${zip_object}"
  ERROR_VARIABLE extraction_error)
if(NOT extracted EQUAL 0)
  message(FATAL_ERROR "Cannot extract ${zip_member}: ${extraction_error}")
endif()
file(SIZE "${zip_object}" zip_size)
if(zip_size EQUAL 0)
  message(FATAL_ERROR "Extracted ZIP object is empty: ${zip_member}")
endif()

set(disassembly_file "${PVDKIT_WORK_DIR}/internal_zip.asm")
execute_process(
  COMMAND "${PVDKIT_LLVM_DIR}/llvm-objdump.exe" -d --no-show-raw-insn --x86-asm-syntax=intel "${zip_object}"
  RESULT_VARIABLE disassembled
  OUTPUT_FILE "${disassembly_file}"
  ERROR_VARIABLE disassembly_error)
if(NOT disassembled EQUAL 0)
  message(FATAL_ERROR "Cannot disassemble ${zip_member}: ${disassembly_error}")
endif()
file(READ "${disassembly_file}" disassembly)
# A real ZIP decode definition must be present. The scalar reconstruct helper can
# be inlined into it, so requiring a separate reconstruct symbol would be wrong.
if(NOT disassembly MATCHES "[0-9a-f]+ <_?internal_exr_undo_zip>:")
  message(FATAL_ERROR "ZIP decode implementation missing from ${zip_member}; inspect ${disassembly_file}")
endif()

# SSE3, SSSE3 and SSE4.x mnemonics; pextrw/pinsrw are SSE2 and remain allowed.
# Match decoded instruction lines, not symbol names, operands or raw byte values.
# A VEX form of one of these instructions also exceeds the baseline.
set(sse3 "addsubp[ds]|haddp[ds]|hsubp[ds]|lddqu|movddup|movshdup|movsldup|monitor|mwait")
set(ssse3 "pabs[bdw]|palignr|phadd[dw]|phaddsw|phsub[dw]|phsubsw|pmaddubsw|pmulhrsw|pshufb|psign[bdw]")
set(sse41 "blendp[ds]|blendvp[ds]|dpp[ds]|extractps|insertps|movntdqa|mpsadbw|packusdw|pblendvb|pblendw|pcmpeqq|pextr[bdq]|phminposuw|pinsr[bdq]|pmax(sb|sd|ud|uw)|pmin(sb|sd|ud|uw)|pmovsx(bd|bq|bw|dq|wd|wq)|pmovzx(bd|bq|bw|dq|wd|wq)|pmuldq|pmulld|ptest|round(p[ds]|s[ds])")
set(sse42 "crc32|pcmpestr[im]|pcmpistr[im]|pcmpgtq|popcnt")
set(sse4a "extrq|insertq|movntsd|movntss")
string(REGEX MATCHALL "[0-9a-f]+:[ \t]+v?(${sse3}|${ssse3}|${sse41}|${sse42}|${sse4a})([ \t][^\n]*|\n)" forbidden "${disassembly}")
if(forbidden)
  list(LENGTH forbidden forbidden_count)
  list(GET forbidden 0 first_forbidden)
  message(FATAL_ERROR
    "ZIP reconstruction requires SSE3/SSSE3/SSE4.x without CPU dispatch: ${forbidden_count} instructions; "
    "first: ${first_forbidden}. Archive: ${PVDKIT_ARCHIVE}. Disassembly: ${disassembly_file}")
endif()
message(STATUS "PASS ZIP baseline: ${zip_member}, ${zip_size} bytes, no SSE3/SSSE3/SSE4.x instructions")
