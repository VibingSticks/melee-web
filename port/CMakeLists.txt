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
        src/dvd_web/dvd_web.c
        src/vi_shim.c
        src/gx_shim.c
        src/ax_hle/ax_stub.c
        src/game_stubs.c
        src/hsd_endian/walker.c)
    target_include_directories(melee_port PUBLIC src extern/aurora/include)
    target_compile_options(melee_port PRIVATE -std=gnu99 -Wall -Wextra -Wno-unused-parameter)

    # Aurora (see extern/aurora-patches for the Emscripten changes)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(CMAKE_POSITION_INDEPENDENT_CODE OFF)
    set(AURORA_ENABLE_DVD OFF CACHE BOOL "" FORCE)     # replaced by src/dvd_web
    set(AURORA_ENABLE_CARD ON CACHE BOOL "" FORCE)
    set(AURORA_CACHE_USE_ZSTD OFF CACHE BOOL "" FORCE)
    set(AURORA_EMDAWNWEBGPU_PORT ${CMAKE_CURRENT_SOURCE_DIR}/extern/emdawnwebgpu-v20260910.214722.remoteport.py CACHE STRING "" FORCE)
    add_subdirectory(extern/aurora EXCLUDE_FROM_ALL)
    target_link_libraries(melee_port PUBLIC aurora::core)

    include(cmake/emscripten_link.cmake)
    add_executable(melee src/main_loop.c)
    target_include_directories(melee PRIVATE src)
    target_link_libraries(melee PRIVATE melee_game melee_port
        aurora::core aurora::os aurora::gx aurora::gd aurora::pad aurora::si aurora::ms
        aurora::mtx aurora::vi aurora::card aurora::main)
    target_link_options(melee PRIVATE ${PORT_LINK_FLAGS})
    set_target_properties(melee PROPERTIES OUTPUT_NAME melee SUFFIX ".js")
    set_property(TARGET melee APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/web/js/imports.js)

    # Hosted page next to melee.js
    foreach (f web/shell/index.html web/js/boot.js web/js/disc_source.js web/js/gpu-gl2.js)
        get_filename_component(fn ${f} NAME)
        configure_file(${f} ${CMAKE_BINARY_DIR}/${fn} COPYONLY)
    endforeach ()
    # WebGL2 fallback translator (built by tools/build_naga.sh; optional).
    configure_file(web/js/naga/naga.js ${CMAKE_BINARY_DIR}/naga/naga.js COPYONLY)
    if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/web/js/naga/naga.wasm)
        configure_file(web/js/naga/naga.wasm ${CMAKE_BINARY_DIR}/naga/naga.wasm COPYONLY)
    else ()
        message(WARNING "web/js/naga/naga.wasm missing: run tools/build_naga.sh or the WebGL2 fallback will not work")
    endif ()
endif ()
