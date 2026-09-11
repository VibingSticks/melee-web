cmake_minimum_required(VERSION 3.25)
project(melee-web LANGUAGES C CXX)

set(PORT_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(GAME_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

option(PORT_BUILD_TESTS "Build host unit tests" OFF)
option(PORT_SINGLE_FILE "Embed the wasm in the JS (-sSINGLE_FILE)" OFF)

if (PORT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

if (EMSCRIPTEN)
    # Game static library, Aurora, and the wasm executable are added by the
    # plan's Tasks 5, 9 and 10 (see docs/superpowers/plans/).
    include(cmake/game_sources.cmake OPTIONAL)
endif ()
