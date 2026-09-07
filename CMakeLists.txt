cmake_minimum_required(VERSION 3.28)
project(sxpe VERSION 0.6.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Single source of truth for CLI --version, GUI About, and MCP serverInfo.
# When bumping, keep vcpkg.json "version-string" equal to PROJECT_VERSION.
set(SXPE_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/sxpe_version.hpp.in"
    "${SXPE_GENERATED_INCLUDE_DIR}/sxpe/version.hpp"
    @ONLY)

set(SXPE_VENDOR "${CMAKE_BINARY_DIR}/_vendor")
file(MAKE_DIRECTORY "${SXPE_VENDOR}/nlohmann")
if(NOT EXISTS "${SXPE_VENDOR}/nlohmann/json.hpp")
    message(STATUS "Downloading nlohmann/json.hpp")
    file(DOWNLOAD
        "https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp"
        "${SXPE_VENDOR}/nlohmann/json.hpp"
        SHOW_PROGRESS
        STATUS _dl
        TIMEOUT 120)
    list(GET _dl 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
        message(FATAL_ERROR "Failed to download nlohmann/json.hpp: ${_dl}")
    endif()
endif()
if(NOT EXISTS "${SXPE_VENDOR}/CLI11.hpp")
    message(STATUS "Downloading CLI11.hpp")
    file(DOWNLOAD
        "https://github.com/CLIUtils/CLI11/releases/download/v2.4.2/CLI11.hpp"
        "${SXPE_VENDOR}/CLI11.hpp"
        SHOW_PROGRESS
        STATUS _dl2
        TIMEOUT 120)
    list(GET _dl2 0 _dl2_code)
    if(NOT _dl2_code EQUAL 0)
        message(FATAL_ERROR "Failed to download CLI11.hpp: ${_dl2}")
    endif()
endif()

add_library(sxpe_games INTERFACE)
target_include_directories(sxpe_games INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_library(sxpe_core
    src/core/mapped_file.cpp
)
target_link_libraries(sxpe_core PUBLIC sxpe_games)

add_library(sxpe_games_sims3
    src/games_sims3/sims3_game_profile.cpp
    src/games_sims3/package.cpp
    src/games_sims3/refpack.cpp
    src/games_sims3/fnv.cpp
    src/games_sims3/sims3pack.cpp
)
target_link_libraries(sxpe_games_sims3 PUBLIC sxpe_core)

add_library(sxpe_resources
    src/resources/stbl.cpp
    src/resources/nmap.cpp
    src/resources/dds.cpp
    src/resources/s3sa.cpp
    src/resources/objk.cpp
    src/resources/vpxy.cpp
    src/resources/objd.cpp
    src/resources/casp.cpp
    src/resources/clip.cpp
    src/resources/rcol.cpp
    src/resources/dir.cpp
    src/resources/xml.cpp
    src/resources/refs.cpp
    src/resources/key_table.cpp
)
target_link_libraries(sxpe_resources PUBLIC sxpe_games_sims3)

add_library(sxpe_commands
    src/commands/bus.cpp
)
target_include_directories(sxpe_commands PUBLIC "${SXPE_VENDOR}")
target_link_libraries(sxpe_commands PUBLIC sxpe_resources)

add_executable(sxpe src/cli/main.cpp)
target_include_directories(sxpe PRIVATE "${SXPE_VENDOR}" "${SXPE_GENERATED_INCLUDE_DIR}")
target_link_libraries(sxpe PRIVATE sxpe_commands)

add_executable(sxpe_mcp src/mcp/main.cpp)
target_include_directories(sxpe_mcp PRIVATE "${SXPE_VENDOR}" "${SXPE_GENERATED_INCLUDE_DIR}")
target_link_libraries(sxpe_mcp PRIVATE sxpe_commands)

add_executable(sxpe_tests tests/game_profile_registry_test.cpp)
target_link_libraries(sxpe_tests PRIVATE sxpe_games_sims3)

add_executable(sxpe_dbpf_io_test tests/dbpf_io_test.cpp)
target_link_libraries(sxpe_dbpf_io_test PRIVATE sxpe_games_sims3)
target_compile_definitions(sxpe_dbpf_io_test PRIVATE
    "SXPE_SYNTHETIC_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/fixtures/synthetic\"")

add_executable(sxpe_refpack_test tests/refpack_test.cpp)
target_link_libraries(sxpe_refpack_test PRIVATE sxpe_games_sims3)
target_compile_definitions(sxpe_refpack_test PRIVATE
    "SXPE_SYNTHETIC_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/fixtures/synthetic\"")

add_executable(sxpe_fnv_test tests/fnv_test.cpp)
target_link_libraries(sxpe_fnv_test PRIVATE sxpe_games_sims3)

add_executable(sxpe_commands_test tests/commands_test.cpp)
target_link_libraries(sxpe_commands_test PRIVATE sxpe_commands)
target_include_directories(sxpe_commands_test PRIVATE "${SXPE_VENDOR}")
target_compile_definitions(sxpe_commands_test PRIVATE
    "SXPE_SYNTHETIC_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/fixtures/synthetic\""
    "SXPE_CLI=\"$<TARGET_FILE:sxpe>\"")

add_executable(sxpe_stbl_test tests/stbl_test.cpp)
target_link_libraries(sxpe_stbl_test PRIVATE sxpe_resources)

add_executable(sxpe_objk_vpxy_test tests/objk_vpxy_test.cpp)
target_link_libraries(sxpe_objk_vpxy_test PRIVATE sxpe_resources)

add_executable(sxpe_refs_test tests/refs_test.cpp)
target_link_libraries(sxpe_refs_test PRIVATE sxpe_resources)

add_executable(sxpe_preview_wave2_test tests/preview_wave2_test.cpp)
target_link_libraries(sxpe_preview_wave2_test PRIVATE sxpe_resources)

add_executable(sxpe_s3sa_test tests/s3sa_test.cpp)
target_link_libraries(sxpe_s3sa_test PRIVATE sxpe_resources)

add_executable(sxpe_dir_test tests/dir_test.cpp)
target_link_libraries(sxpe_dir_test PRIVATE sxpe_resources)

add_executable(sxpe_sims3pack_test tests/sims3pack_test.cpp)
target_link_libraries(sxpe_sims3pack_test PRIVATE sxpe_games_sims3)
target_compile_definitions(sxpe_sims3pack_test PRIVATE
    "SXPE_SYNTHETIC_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/fixtures/synthetic\"")

set(_sxpe_qt_hint "${CMAKE_CURRENT_SOURCE_DIR}/../qt/6.8.2/msvc2022_64")
if(EXISTS "${_sxpe_qt_hint}/lib/cmake/Qt6/Qt6Config.cmake")
    list(PREPEND CMAKE_PREFIX_PATH "${_sxpe_qt_hint}")
endif()
find_package(Qt6 6.5 COMPONENTS Widgets Network QUIET)
if(Qt6_FOUND)
    qt_standard_project_setup()
    add_executable(sxpe_gui WIN32
        src/gui/main.cpp
        src/gui/main_window.cpp
        src/gui/package_tab.cpp
        src/gui/resource_model.cpp
        src/gui/inspector.cpp
        src/gui/palette.cpp
        src/gui/dialogs.cpp
        src/gui/plugin_host.cpp
    )
    target_include_directories(sxpe_gui PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gui"
        "${SXPE_VENDOR}"
        "${SXPE_GENERATED_INCLUDE_DIR}")
    target_link_libraries(sxpe_gui PRIVATE Qt6::Widgets Qt6::Network sxpe_commands)
    set_target_properties(sxpe_gui PROPERTIES AUTOMOC ON)
    get_filename_component(_qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
    find_program(SXPE_WINDEPLOYQT windeployqt HINTS "${_qt_prefix}/bin")
    if(SXPE_WINDEPLOYQT)
        add_custom_command(TARGET sxpe_gui POST_BUILD
            COMMAND "${SXPE_WINDEPLOYQT}" --no-translations --no-compiler-runtime
                    "$<TARGET_FILE:sxpe_gui>"
            COMMENT "windeployqt sxpe_gui")
    endif()
else()
    message(STATUS "Qt 6 Widgets not found; skipping sxpe_gui (CLI/MCP still build)")
endif()

enable_testing()
add_test(NAME game_profile_registry COMMAND sxpe_tests)
add_test(NAME dbpf_io COMMAND sxpe_dbpf_io_test)
add_test(NAME refpack COMMAND sxpe_refpack_test)
add_test(NAME fnv COMMAND sxpe_fnv_test)
add_test(NAME commands COMMAND sxpe_commands_test)
add_test(NAME stbl COMMAND sxpe_stbl_test)
add_test(NAME objk_vpxy COMMAND sxpe_objk_vpxy_test)
add_test(NAME refs COMMAND sxpe_refs_test)
add_test(NAME preview_wave2 COMMAND sxpe_preview_wave2_test)
add_test(NAME s3sa COMMAND sxpe_s3sa_test)
add_test(NAME dir COMMAND sxpe_dir_test)
add_test(NAME sims3pack COMMAND sxpe_sims3pack_test)
add_test(NAME cli_manifest COMMAND sxpe manifest --format json)
add_test(NAME cli_hash COMMAND sxpe hash fnv --text a --width 32 --format json)
add_test(NAME cli_list COMMAND sxpe resource list --package
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/synthetic/single-blob.bin" --format json --limit 10)
add_test(NAME cli_info COMMAND sxpe package info --package
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/synthetic/empty.bin" --format json)
add_test(NAME cli_help COMMAND sxpe --help)
set_tests_properties(cli_help PROPERTIES PASS_REGULAR_EXPRESSION "resource.rename")
add_test(NAME cli_version COMMAND sxpe --version)
set_tests_properties(cli_version PROPERTIES PASS_REGULAR_EXPRESSION "${PROJECT_VERSION}")
add_test(NAME cli_help_command COMMAND sxpe resource rename --help)
set_tests_properties(cli_help_command PROPERTIES PASS_REGULAR_EXPRESSION "NMAP")
add_test(NAME cli_help_noun COMMAND sxpe help resource)
set_tests_properties(cli_help_noun PROPERTIES PASS_REGULAR_EXPRESSION "resource.rename")
add_test(NAME cli_package_new COMMAND sxpe package new --package
    "${CMAKE_BINARY_DIR}/cli-new.package" --force --format json)
set_tests_properties(cli_package_new PROPERTIES PASS_REGULAR_EXPRESSION "\"ok\":true")
add_test(NAME cli_rename COMMAND sxpe resource rename --package
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/synthetic/single-blob.bin"
    --type 0 --group 0 --instance 1 --name TestName --dry-run --format json)
set_tests_properties(cli_rename PROPERTIES PASS_REGULAR_EXPRESSION "\"ok\":true")
add_test(NAME cli_folder_scan COMMAND sxpe folder scan --path
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/synthetic" --format json)
set_tests_properties(cli_folder_scan PROPERTIES PASS_REGULAR_EXPRESSION "\"ok\":true")
add_test(NAME cli_sims3pack_list COMMAND sxpe sims3pack list --path
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/synthetic/minimal.sims3pack" --format json)
set_tests_properties(cli_sims3pack_list PROPERTIES PASS_REGULAR_EXPRESSION "\"ok\":true")
add_test(NAME cli_sims3pack_extract COMMAND sxpe sims3pack extract --path
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/synthetic/minimal.sims3pack"
    --out-dir "${CMAKE_BINARY_DIR}/sims3pack-extract-cli" --index 0 --force --format json)
set_tests_properties(cli_sims3pack_extract PROPERTIES PASS_REGULAR_EXPRESSION "\"ok\":true")
if(TARGET sxpe_gui)
    add_test(NAME gui_smoke COMMAND sxpe_gui --smoke
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/synthetic/single-blob.bin")
    get_filename_component(_qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
    # Official Qt: <prefix>/plugins; Debian/Ubuntu multiarch: <libdir>/qt6/plugins.
    set(_qt_plugins "${_qt_prefix}/plugins")
    if(NOT EXISTS "${_qt_plugins}/platforms")
        if(EXISTS "${_qt_prefix}/qt6/plugins/platforms")
            set(_qt_plugins "${_qt_prefix}/qt6/plugins")
        endif()
    endif()
    set(_gui_smoke_env "QT_QPA_PLATFORM=offscreen")
    if(EXISTS "${_qt_plugins}/platforms")
        string(APPEND _gui_smoke_env ";QT_PLUGIN_PATH=${_qt_plugins}")
    endif()
    set_tests_properties(gui_smoke PROPERTIES
        ENVIRONMENT "${_gui_smoke_env}"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_prefix}/bin")
endif()
