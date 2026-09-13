set(AURORA_INC ${CMAKE_SOURCE_DIR}/extern/aurora/include)

function(port_add_test name)
    add_executable(${name} ${ARGN} port_host_stubs.c)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                               ${PORT_SRC_DIR} ${AURORA_INC})
    target_compile_options(${name} PRIVATE -Wall -Wextra -Wno-missing-field-initializers -std=c99)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

port_add_test(smoke_test smoke_test.c)
port_add_test(os_alarm_test os_alarm_test.c ${PORT_SRC_DIR}/os_shim/os_alarm.c)
port_add_test(fst_test fst_test.c ${PORT_SRC_DIR}/dvd_web/fst.c)
port_add_test(dvd_web_test dvd_web_test.c ${PORT_SRC_DIR}/dvd_web/dvd_web.c
              ${PORT_SRC_DIR}/dvd_web/fst.c)
port_add_test(walker_test walker_test.c ${PORT_SRC_DIR}/hsd_endian/walker.c)
port_add_test(formats_test formats_test.c ${PORT_SRC_DIR}/hsd_endian/formats.c
              ${PORT_SRC_DIR}/hsd_endian/walker.c)
add_test(NAME gen_schema_test COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/gen_schema_test.py)
add_test(NAME pack_test COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/pack_test.py)

# Tests for helpers compiled inside the game library: game headers, compat
# headers (4-byte bool), no LINT (the layout asserts assume 4-byte pointers).
function(port_add_game_test name)
    add_executable(${name} ${ARGN} port_host_stubs.c)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                               ${PORT_SRC_DIR}/compat ${PORT_SRC_DIR} ${GAME_ROOT}/src ${AURORA_INC})
    target_compile_definitions(${name} PRIVATE TARGET_PC VERSION_GALE01 BUILD_VERSION=0)
    target_compile_options(${name} PRIVATE -Wall -Wextra -std=gnu99 -Wno-unused-parameter)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

port_add_game_test(vtx_arrays_test vtx_arrays_test.c ${PORT_SRC_DIR}/hsd_port/vtx_arrays.c)
port_add_game_test(archive_swap_test archive_swap_test.c ${PORT_SRC_DIR}/hsd_endian/archive_swap.c
                   ${PORT_SRC_DIR}/hsd_endian/walker.c)
port_add_game_test(archive_roots_test archive_roots_test.c ${PORT_SRC_DIR}/hsd_endian/archive_swap.c
                   ${PORT_SRC_DIR}/hsd_endian/walker.c)
port_add_game_test(ft_script_test ft_script_test.c ${PORT_SRC_DIR}/hsd_endian/formats.c
                   ${PORT_SRC_DIR}/hsd_endian/walker.c)

# Host tool: archive schema coverage straight from a disc image (not a test).
# The tables are generated exactly as for the web build (wasm32 layout = the
# on-disc layout); only the sysroot for the parse comes from the local emsdk.
set(HOST_SCHEMA_TABLES ${CMAKE_BINARY_DIR}/schema_tables.c)
set(EMSDK_SYSROOT ${CMAKE_SOURCE_DIR}/extern/emsdk/upstream/emscripten/cache/sysroot)
file(GLOB_RECURSE SCHEMA_HEADERS CONFIGURE_DEPENDS ${GAME_ROOT}/src/sysdolphin/baselib/*.h ${GAME_ROOT}/src/melee/*.h)
add_custom_command(OUTPUT ${HOST_SCHEMA_TABLES}
    COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/gen_schema.py
        --header ${CMAKE_SOURCE_DIR}/schema/all_headers.h
        --annotations ${CMAKE_SOURCE_DIR}/schema/annotations.yml
        --roots ${CMAKE_SOURCE_DIR}/schema/roots.yml --quiet
        --clang-arg=-I${GAME_ROOT}/src --clang-arg=-isystem${AURORA_INC}
        --clang-arg=-I${PORT_SRC_DIR}/compat --clang-arg=-I${PORT_SRC_DIR} --clang-arg=-I${CMAKE_SOURCE_DIR}/schema
        --clang-arg=-DVERSION_GALE01 --clang-arg=-DBUILD_VERSION=0 --clang-arg=--sysroot=${EMSDK_SYSROOT}
        ${HOST_SCHEMA_TABLES}
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/gen_schema.py ${CMAKE_SOURCE_DIR}/schema/all_headers.h
            ${CMAKE_SOURCE_DIR}/schema/mirrors.h ${CMAKE_SOURCE_DIR}/schema/annotations.yml
            ${CMAKE_SOURCE_DIR}/schema/roots.yml ${SCHEMA_HEADERS}
    COMMENT "Generating archive endian schema tables (host)")
add_executable(archive_coverage ${CMAKE_SOURCE_DIR}/tools/archive_coverage.c ${HOST_SCHEMA_TABLES}
               ${PORT_SRC_DIR}/hsd_endian/archive_swap.c ${PORT_SRC_DIR}/hsd_endian/walker.c
               ${PORT_SRC_DIR}/dvd_web/fst.c port_host_stubs.c)
target_include_directories(archive_coverage PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                           ${PORT_SRC_DIR}/compat ${PORT_SRC_DIR} ${GAME_ROOT}/src ${AURORA_INC})
target_compile_definitions(archive_coverage PRIVATE TARGET_PC VERSION_GALE01 BUILD_VERSION=0)
target_compile_options(archive_coverage PRIVATE -Wall -Wextra -std=gnu99 -Wno-unused-parameter)
