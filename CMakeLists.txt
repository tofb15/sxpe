cmake_minimum_required(VERSION 3.28)
project(sxpe VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
)
target_link_libraries(sxpe_games_sims3 PUBLIC sxpe_core)

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

enable_testing()
add_test(NAME game_profile_registry COMMAND sxpe_tests)
add_test(NAME dbpf_io COMMAND sxpe_dbpf_io_test)
add_test(NAME refpack COMMAND sxpe_refpack_test)
add_test(NAME fnv COMMAND sxpe_fnv_test)
