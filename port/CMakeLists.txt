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
    include(cmake/game_sources.cmake)

    # Port-only sources. Compiled WITHOUT src/compat so `bool` matches Aurora's
    # C++ side (see src/compat/stdbool.h).
    add_library(melee_port STATIC
        src/os_shim/os_shim.c
        src/os_shim/os_alarm.c
        src/dvd_web/fst.c
        src/dvd_web/dvd_web.c)
    target_include_directories(melee_port PUBLIC src extern/aurora/include)
    target_compile_options(melee_port PRIVATE -std=gnu99 -Wall -Wextra -Wno-unused-parameter)

    # Aurora and the wasm executable are added by the plan's Tasks 9 and 10.
endif ()
