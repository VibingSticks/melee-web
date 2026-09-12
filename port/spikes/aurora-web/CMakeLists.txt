# Spike S1: does Aurora's `simple` example build and run under Emscripten?
# Throwaway. Configure with:  emcmake cmake -S . -B build -G Ninja
cmake_minimum_required(VERSION 3.25)
project(aurora-web-spike LANGUAGES C CXX)

if (NOT EMSCRIPTEN)
    message(FATAL_ERROR "This spike must be configured with emcmake")
endif ()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)  # Emscripten: no side modules for third-party libs
set(AURORA_ENABLE_DVD OFF CACHE BOOL "" FORCE)
set(AURORA_ENABLE_CARD OFF CACHE BOOL "" FORCE)
set(AURORA_CACHE_USE_ZSTD OFF CACHE BOOL "" FORCE)
# Newer emdawnwebgpu than the one bundled with Emscripten: adds SetImmediates (WebGPU immediates),
# which Aurora's GX draw path requires.
set(AURORA_EMDAWNWEBGPU_PORT ${CMAKE_CURRENT_SOURCE_DIR}/../../extern/emdawnwebgpu-v20260910.214722.remoteport.py CACHE STRING "" FORCE)

set(AURORA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../extern/aurora)
add_subdirectory(${AURORA_DIR} aurora EXCLUDE_FROM_ALL)

add_executable(simple ${CMAKE_CURRENT_SOURCE_DIR}/simple_web.c)  # examples/simple.c + per-frame emscripten_sleep
target_link_libraries(simple PRIVATE aurora::core aurora::gx aurora::main aurora::vi aurora::mtx)
target_link_options(simple PRIVATE
    -sASYNCIFY
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=128MB
    -sASSERTIONS=1
    -sSTACK_SIZE=1MB
    --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html)
set_target_properties(simple PROPERTIES SUFFIX ".html")
set_property(TARGET simple APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shell.html)
