# pvdkit plugin helpers. A plugin directory (plugins/<id>/CMakeLists.txt) calls, in this order:
#
#   pvdkit_plugin_identity(<id> NAME <Name> VERSION <M.m.p> PRIORITY <n>
#                          DESCRIPTION <text> COMMENTS <text>)
#     -> INTERFACE target <id>_identity carrying the generated pvd/PluginConstants.hpp (the one
#        source of the plugin's identity for Exports.cpp, the composition root, Plugin.rc and the
#        e2e version test) and the properties PVDKIT_PLUGIN_NAME / PVDKIT_PLUGIN_VERSION.
#
#   pvdkit_add_plugin(<id> LINK <composition libs...>
#                     LICENSES <port> <display name> [<port> <display name> ...])
#     -> SHARED target <id>_plugin (<Name>.pvd): the shared Exports.cpp, Plugin.def and Plugin.rc
#        over the plugin's composition root, linked with pvdkit_pvd; plus the package staging
#        directory <bindir>/package (the plugin's static readme_en.txt, readme_ru.txt and
#        ChangeLog copied byte-for-byte, plus generated LICENSES.txt and manifest.json) that
#        scripts/build-all.ps1 and scripts/package.ps1 consume; configure and CTest both validate
#        the static documents' identity, encodings and line endings.
#
#   pvdkit_add_plugin_e2e_tests(<id> FIXTURES <dir> SEQUENCE_EXPECTATIONS <source>
#                                SOURCES <files...>)   (tests only)
#     -> executable <id>_e2e_tests = the shared host driver (tests/e2e/PluginHost.*), the shared
#        VERSIONINFO read-back test and the plugin's own e2e sources, compiled against the
#        plugin's identity, fixtures and DLL path; ctest entries <id>_e2e_tests (in coverage
#        builds run under LLVM_PROFILE_FILE=pvdkit-<id>-%p-%m.profraw so scripts/coverage.ps1 can
#        require this DLL's own profile), <id>_check_imports and <id>_check_exports (the last two
#        in the Release configuration);
#     -> executable <id>_leak_tests = the same host driver plus the shared leak scenarios
#        (tests/support/leak/*, the property PVDKIT_LEAK_SCENARIO_SOURCES of pvdkit_leakcheck) over
#        the same fixture directory, which discovers the fixtures through the DLL itself; ctest
#        entry <id>_leak_tests (the same coverage-build profile name). A plugin gets the leak gate
#        by registering its e2e tests and nothing else.
#
#   pvdkit_add_plugin_sequence_tests(<id> FIXTURES <dir> EXPECTATIONS <source>)
#     (called by the e2e helper)
#     -> executable <id>_sequence_tests = the framework-free in-process host-sequence driver plus
#        its shared doctest source, linked to this plugin's composition root with reduced limits.

include("${CMAKE_CURRENT_LIST_DIR}/pvdkit-package-docs.cmake")

# Escapes a string for use inside a C string literal in a generated header (backslashes, quotes).
function(_pvdkit_escape_literal out value)
  string(REPLACE "\\" "\\\\" escaped "${value}")
  string(REPLACE "\"" "\\\"" escaped "${escaped}")
  set(${out} "${escaped}" PARENT_SCOPE)
endfunction()

function(pvdkit_plugin_identity id)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "NAME;VERSION;PRIORITY;DESCRIPTION;COMMENTS" "")
  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "pvdkit_plugin_identity(${id}): unexpected arguments ${arg_UNPARSED_ARGUMENTS}")
  endif()
  foreach(required NAME VERSION PRIORITY DESCRIPTION COMMENTS)
    if(NOT DEFINED arg_${required} OR arg_${required} STREQUAL "")
      message(FATAL_ERROR "pvdkit_plugin_identity(${id}): ${required} is required")
    endif()
  endforeach()
  if(NOT arg_NAME MATCHES "^[A-Za-z0-9_]+$")
    message(FATAL_ERROR "pvdkit_plugin_identity(${id}): NAME '${arg_NAME}' must be a plain file-name stem")
  endif()
  if(NOT arg_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
    message(FATAL_ERROR "pvdkit_plugin_identity(${id}): VERSION '${arg_VERSION}' must be MAJOR.MINOR.PATCH")
  endif()
  set(PVDKIT_PLUGIN_VERSION_MAJOR "${CMAKE_MATCH_1}")
  set(PVDKIT_PLUGIN_VERSION_MINOR "${CMAKE_MATCH_2}")
  set(PVDKIT_PLUGIN_VERSION_PATCH "${CMAKE_MATCH_3}")
  if(NOT arg_PRIORITY MATCHES "^[0-9]+$")
    message(FATAL_ERROR "pvdkit_plugin_identity(${id}): PRIORITY '${arg_PRIORITY}' must be an unsigned integer")
  endif()

  set(PVDKIT_PLUGIN_NAME "${arg_NAME}")
  set(PVDKIT_PLUGIN_VERSION "${arg_VERSION}")
  set(PVDKIT_PLUGIN_PRIORITY "${arg_PRIORITY}")
  # The strings land inside C string literals and the .rc; escape backslashes and quotes so an
  # override such as -DPVDKIT_AUTHOR=`He said "hi"` cannot break the generated header.
  _pvdkit_escape_literal(PVDKIT_PLUGIN_AUTHOR_ESCAPED "${PVDKIT_AUTHOR}")
  _pvdkit_escape_literal(PVDKIT_PLUGIN_COPYRIGHT_ESCAPED "${PVDKIT_COPYRIGHT}")
  _pvdkit_escape_literal(PVDKIT_PLUGIN_DESCRIPTION_ESCAPED "${arg_DESCRIPTION}")
  _pvdkit_escape_literal(PVDKIT_PLUGIN_COMMENTS_ESCAPED "${arg_COMMENTS}")

  set(generated "${PROJECT_BINARY_DIR}/generated/${id}")
  configure_file("${PROJECT_SOURCE_DIR}/src/pvd/PluginConstants.hpp.in"
                 "${generated}/pvd/PluginConstants.hpp" @ONLY)

  add_library(${id}_identity INTERFACE)
  target_include_directories(${id}_identity INTERFACE "${generated}")
  set_target_properties(
    ${id}_identity
    PROPERTIES PVDKIT_PLUGIN_NAME "${arg_NAME}"
               PVDKIT_PLUGIN_VERSION "${arg_VERSION}"
               PVDKIT_PLUGIN_PRIORITY "${arg_PRIORITY}"
               PVDKIT_PLUGIN_DESCRIPTION "${arg_DESCRIPTION}"
               PVDKIT_PLUGIN_COMMENTS "${arg_COMMENTS}")
endfunction()

function(pvdkit_add_plugin id)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "LINK;LICENSES")
  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "pvdkit_add_plugin(${id}): unexpected arguments ${arg_UNPARSED_ARGUMENTS}")
  endif()
  file(RELATIVE_PATH plugin_directory_id "${PROJECT_SOURCE_DIR}/plugins" "${CMAKE_CURRENT_SOURCE_DIR}")
  if(NOT plugin_directory_id STREQUAL "${id}")
    message(FATAL_ERROR
            "pvdkit_add_plugin(${id}): id must equal its directory name under plugins/ "
            "('${plugin_directory_id}') because coverage derives the id from that path")
  endif()
  if(NOT TARGET ${id}_identity)
    message(FATAL_ERROR "pvdkit_add_plugin(${id}): call pvdkit_plugin_identity(${id} ...) first")
  endif()
  if(NOT arg_LINK)
    message(FATAL_ERROR "pvdkit_add_plugin(${id}): LINK must name the composition root library")
  endif()
  list(LENGTH arg_LICENSES license_count)
  math(EXPR license_remainder "${license_count} % 2")
  if(license_count EQUAL 0 OR NOT license_remainder EQUAL 0)
    message(FATAL_ERROR "pvdkit_add_plugin(${id}): LICENSES takes <port> <display name> pairs")
  endif()

  get_target_property(name ${id}_identity PVDKIT_PLUGIN_NAME)
  get_target_property(version ${id}_identity PVDKIT_PLUGIN_VERSION)
  get_target_property(export_sources pvdkit_pvd PVDKIT_EXPORT_SOURCES)
  get_target_property(def_file pvdkit_pvd PVDKIT_DEF_FILE)
  get_target_property(rc_file pvdkit_pvd PVDKIT_RC_FILE)

  # The DLL: the shared composition root (Exports.cpp, compiled here against this plugin's
  # identity) over the plugin's own composition root. The module definition file is the complete
  # export list; nothing in the tree uses __declspec(dllexport). Its bare names are the exported
  # names on both architectures: on x86 the linker resolves each one to the __stdcall-decorated
  # symbol (`pvdInit` -> `_pvdInit@0`) itself, so the file needs no aliases.
  add_library(${id}_plugin SHARED ${export_sources} "${def_file}" "${rc_file}")
  set_target_properties(${id}_plugin PROPERTIES OUTPUT_NAME "${name}" PREFIX "" SUFFIX ".pvd")
  target_link_libraries(${id}_plugin PRIVATE ${id}_identity pvdkit_pvd ${arg_LINK} pvdkit_options)

  # Package staging for scripts/package.ps1: the plugin's hand-written documents copied without
  # changing a byte, LICENSES.txt assembled from the license texts vcpkg installed for the listed
  # ports (share/<port>/copyright), and manifest.json naming the DLL, version and architecture.
  set(package_dir "${CMAKE_CURRENT_BINARY_DIR}/package")
  set(package_source_dir "${CMAKE_CURRENT_SOURCE_DIR}/package")
  pvdkit_check_package_docs(
    PACKAGE_DIR "${package_source_dir}"
    NAME "${name}"
    VERSION "${version}"
    CONTEXT "pvdkit_add_plugin(${id})")
  file(REMOVE_RECURSE "${package_dir}")
  file(MAKE_DIRECTORY "${package_dir}")
  foreach(document readme_en.txt readme_ru.txt ChangeLog)
    configure_file("${package_source_dir}/${document}" "${package_dir}/${document}" COPYONLY)
  endforeach()

  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PVDKIT_PLUGIN_ARCHITECTURE "x64")
  else()
    set(PVDKIT_PLUGIN_ARCHITECTURE "x86")
  endif()

  set(licenses "")
  set(license_names "")
  string(REPEAT "=" 78 rule)
  while(arg_LICENSES)
    list(POP_FRONT arg_LICENSES port display)
    # find_file searches CMAKE_PREFIX_PATH, which the vcpkg toolchain points at vcpkg_installed.
    find_file(PVDKIT_${id}_${port}_COPYRIGHT NAMES copyright PATH_SUFFIXES "share/${port}" REQUIRED)
    file(READ "${PVDKIT_${id}_${port}_COPYRIGHT}" text)
    string(REGEX REPLACE "[ \t\r\n]+$" "" text "${text}")
    string(APPEND licenses "${rule}\n${display} - as installed by vcpkg in share/${port}/copyright\n${rule}\n\n${text}\n\n\n")
    list(APPEND license_names "\"${display}\"")
  endwhile()
  file(WRITE "${package_dir}/LICENSES.txt" "${licenses}")
  list(JOIN license_names ", " license_names)
  file(WRITE "${package_dir}/manifest.json"
       "{\n  \"name\": \"${name}\",\n  \"version\": \"${version}\",\n  \"architecture\": \"${PVDKIT_PLUGIN_ARCHITECTURE}\",\n  \"file\": \"${name}.pvd\",\n  \"licenses\": [${license_names}]\n}\n")
  if(BUILD_TESTING)
    add_test(
      NAME ${id}_package_docs
      COMMAND
        "${CMAKE_COMMAND}" "-DPVDKIT_PACKAGE_DIR=${package_dir}" "-DPVDKIT_PLUGIN_NAME=${name}"
        "-DPVDKIT_PLUGIN_VERSION=${version}" -P "${PROJECT_SOURCE_DIR}/cmake/pvdkit-package-docs.cmake")
  endif()
endfunction()

function(pvdkit_add_plugin_e2e_tests id)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "FIXTURES;SEQUENCE_EXPECTATIONS" "SOURCES")
  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "pvdkit_add_plugin_e2e_tests(${id}): unexpected arguments ${arg_UNPARSED_ARGUMENTS}")
  endif()
  if(NOT TARGET ${id}_plugin)
    message(FATAL_ERROR "pvdkit_add_plugin_e2e_tests(${id}): call pvdkit_add_plugin(${id} ...) first")
  endif()
  if(NOT arg_FIXTURES OR NOT IS_DIRECTORY "${arg_FIXTURES}")
    message(FATAL_ERROR "pvdkit_add_plugin_e2e_tests(${id}): FIXTURES must name an existing directory")
  endif()
  if(NOT arg_SOURCES)
    message(FATAL_ERROR "pvdkit_add_plugin_e2e_tests(${id}): SOURCES is required")
  endif()
  if(NOT arg_SEQUENCE_EXPECTATIONS OR NOT EXISTS "${arg_SEQUENCE_EXPECTATIONS}"
     OR IS_DIRECTORY "${arg_SEQUENCE_EXPECTATIONS}")
    message(
      FATAL_ERROR
        "pvdkit_add_plugin_e2e_tests(${id}): SEQUENCE_EXPECTATIONS must name the plugin's expectation source")
  endif()

  # The test loads the built DLL with LoadLibraryW and drives its eight exports like the host; it
  # links none of the plugin's static libraries (pvdkit_options only carries flags, and the pvd
  # include path serves the SDK types). The shared driver sources are compiled into every plugin's
  # e2e test because pluginPath()/fixturePath() come from per-plugin compile definitions.
  file(GLOB shared_sources CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/tests/e2e/*.cpp" "${PROJECT_SOURCE_DIR}/tests/e2e/*.hpp")
  add_executable(${id}_e2e_tests ${arg_SOURCES} ${shared_sources} "${PROJECT_SOURCE_DIR}/tests/TestMain.cpp")
  add_dependencies(${id}_e2e_tests ${id}_plugin)
  target_include_directories(${id}_e2e_tests PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/tests/e2e")
  # version.lib serves the VERSIONINFO read-back test; it is linked into this test only, never
  # into the plugin (check_imports keeps every DLL at KERNEL32.dll).
  target_link_libraries(${id}_e2e_tests PRIVATE ${id}_identity doctest::doctest pvdkit_options version)
  target_compile_definitions(
    ${id}_e2e_tests
    PRIVATE
      PVDKIT_PLUGIN_PATH="$<TARGET_FILE:${id}_plugin>"
      PVDKIT_FIXTURE_DIR="${arg_FIXTURES}")
  add_test(NAME ${id}_e2e_tests COMMAND ${id}_e2e_tests)
  if(PVDKIT_COVERAGE)
    # The instrumented DLL writes its own raw profile from the runtime inside it when the e2e
    # process unloads it, and scripts/coverage.ps1 requires that file per plugin. Exports.cpp has
    # the same function names and hashes in every plugin DLL, so a profile written by another
    # plugin's e2e process satisfies llvm-cov just as well: only the file name can tell them
    # apart. Every plugin's e2e test therefore files its profiles under its own id (the script
    # matches pvdkit-<id>-<pid>-<module>.profraw for that DLL and nothing else); %p keeps parallel
    # tests apart and %m keeps the DLL's runtime apart from the executable's in the same process.
    set_tests_properties(
      ${id}_e2e_tests
      PROPERTIES ENVIRONMENT "LLVM_PROFILE_FILE=${PROJECT_BINARY_DIR}/pvdkit-${id}-%p-%m.profraw")
  endif()

  # The import-table policy is part of the Release definition of done (AGENTS.md): every plugin
  # imports KERNEL32.dll and nothing else. CONFIGURATIONS (rather than a CMAKE_BUILD_TYPE test at
  # configure time) keeps the test on the multi-config `Visual Studio 17 2022 -T ClangCL` fallback
  # too; ctest selects it through `-C Release`, which the release test presets pass.
  find_program(PVDKIT_POWERSHELL NAMES pwsh powershell REQUIRED)
  add_test(
    NAME ${id}_check_imports
    CONFIGURATIONS Release
    COMMAND "${PVDKIT_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass -File
            "${PROJECT_SOURCE_DIR}/scripts/check-imports.ps1" -Path "$<TARGET_FILE:${id}_plugin>")

  # The export-table policy: exactly the eight PVD entry points under their bare names. On x86 the
  # __stdcall symbols are `_pvdInit@0` etc.; the .def file makes the linker export them undecorated,
  # and this test keeps it that way on both architectures (same Release-only registration as above).
  add_test(
    NAME ${id}_check_exports
    CONFIGURATIONS Release
    COMMAND "${PVDKIT_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass -File
            "${PROJECT_SOURCE_DIR}/scripts/check-exports.ps1" -Path "$<TARGET_FILE:${id}_plugin>")

  pvdkit_add_plugin_sequence_tests(
    ${id}
    FIXTURES "${arg_FIXTURES}"
    EXPECTATIONS "${arg_SEQUENCE_EXPECTATIONS}")
  _pvdkit_add_plugin_leak_tests(${id} "${arg_FIXTURES}")
endfunction()

# One executable per plugin is intentional: every composition root defines pvd::makePlugin, so
# combining compositions in one sequence executable would violate the one-definition rule.
function(pvdkit_add_plugin_sequence_tests id)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "FIXTURES;EXPECTATIONS" "")
  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "pvdkit_add_plugin_sequence_tests(${id}): unexpected arguments ${arg_UNPARSED_ARGUMENTS}")
  endif()
  if(NOT TARGET pvdkit_sequence)
    message(FATAL_ERROR "pvdkit_add_plugin_sequence_tests(${id}): tests/support must be configured before the plugins")
  endif()
  if(NOT TARGET ${id}_composition)
    message(FATAL_ERROR "pvdkit_add_plugin_sequence_tests(${id}): ${id}_composition does not exist")
  endif()
  if(NOT arg_FIXTURES OR NOT IS_DIRECTORY "${arg_FIXTURES}")
    message(FATAL_ERROR "pvdkit_add_plugin_sequence_tests(${id}): FIXTURES must name an existing directory")
  endif()
  if(NOT arg_EXPECTATIONS OR NOT EXISTS "${arg_EXPECTATIONS}" OR IS_DIRECTORY "${arg_EXPECTATIONS}")
    message(
      FATAL_ERROR
        "pvdkit_add_plugin_sequence_tests(${id}): EXPECTATIONS must name a plugin-local expectation source")
  endif()

  add_executable(
    ${id}_sequence_tests
    "${arg_EXPECTATIONS}"
    "${PROJECT_SOURCE_DIR}/tests/support/sequence/SequenceDriverTests.cpp"
    "${PROJECT_SOURCE_DIR}/tests/TestMain.cpp")
  target_link_libraries(
    ${id}_sequence_tests
    PRIVATE ${id}_composition ${id}_identity pvdkit_sequence doctest::doctest pvdkit_options)
  target_compile_definitions(
    ${id}_sequence_tests
    PRIVATE
      PVDKIT_FIXTURE_DIR="${arg_FIXTURES}")
  if(PVDKIT_COVERAGE)
    # The death-test child flushes its profile before its intentional abort.
    target_compile_definitions(${id}_sequence_tests PRIVATE PVDKIT_COVERAGE=1)
  endif()
  add_test(NAME ${id}_sequence_tests COMMAND ${id}_sequence_tests)
endfunction()

# The leak gate (level 1, heap and handle counts): the shared scenarios in tests/support/leak
# drive the DLL like the host through the same driver the e2e test uses, for N host-level
# operations per scenario after a warm-up, and require zero heap-block, heap-byte and handle
# deltas (tests/support/LeakCheck.hpp explains the accounting and why it is one mechanism for
# Debug and Release). The scenarios need no plugin-specific code: they discover the fixture
# directory through the plugin itself, so pvdkit_add_plugin_e2e_tests registers this for every
# plugin. Only the host driver is shared with the e2e executable (not its VERSIONINFO test).
function(_pvdkit_add_plugin_leak_tests id fixtures)
  if(NOT TARGET pvdkit_leakcheck)
    message(FATAL_ERROR "_pvdkit_add_plugin_leak_tests(${id}): tests/support must be configured before the plugins")
  endif()
  get_target_property(scenario_sources pvdkit_leakcheck PVDKIT_LEAK_SCENARIO_SOURCES)
  if(NOT scenario_sources)
    message(FATAL_ERROR "_pvdkit_add_plugin_leak_tests(${id}): pvdkit_leakcheck carries no PVDKIT_LEAK_SCENARIO_SOURCES")
  endif()
  file(GLOB host_driver CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/tests/e2e/PluginHost.*")
  add_executable(${id}_leak_tests ${scenario_sources} ${host_driver} "${PROJECT_SOURCE_DIR}/tests/TestMain.cpp")
  add_dependencies(${id}_leak_tests ${id}_plugin)
  target_include_directories(${id}_leak_tests PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/tests/e2e")
  target_link_libraries(${id}_leak_tests PRIVATE ${id}_identity pvdkit_leakcheck doctest::doctest pvdkit_options)
  target_compile_definitions(
    ${id}_leak_tests
    PRIVATE
      PVDKIT_PLUGIN_PATH="$<TARGET_FILE:${id}_plugin>"
      PVDKIT_FIXTURE_DIR="${fixtures}")
  add_test(NAME ${id}_leak_tests COMMAND ${id}_leak_tests)
  if(PVDKIT_COVERAGE)
    # Same reasoning as for ${id}_e2e_tests above: this process loads the instrumented DLL too, and
    # its profiles are filed under the plugin id so scripts/coverage.ps1 can attribute them. The
    # definition lets the LoadLibrary/FreeLibrary scenario allow for what the profile runtime inside
    # the instrumented DLL keeps per load (LeakScenarios.cpp).
    target_compile_definitions(${id}_leak_tests PRIVATE PVDKIT_COVERAGE=1)
    set_tests_properties(
      ${id}_leak_tests
      PROPERTIES ENVIRONMENT "LLVM_PROFILE_FILE=${PROJECT_BINARY_DIR}/pvdkit-${id}-%p-%m.profraw")
  endif()
endfunction()
