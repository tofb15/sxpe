cmake_minimum_required(VERSION 3.28)
project(sxpe VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(sxpe_games INTERFACE)
target_include_directories(sxpe_games INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_library(sxpe_games_sims3
    src/games_sims3/sims3_game_profile.cpp
)
target_link_libraries(sxpe_games_sims3 PUBLIC sxpe_games)

add_library(sxpe_core INTERFACE)
target_link_libraries(sxpe_core INTERFACE sxpe_games_sims3)

add_executable(sxpe_tests tests/game_profile_registry_test.cpp)
target_link_libraries(sxpe_tests PRIVATE sxpe_core)

enable_testing()
add_test(NAME game_profile_registry COMMAND sxpe_tests)
