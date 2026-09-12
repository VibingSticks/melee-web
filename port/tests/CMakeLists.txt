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
port_add_game_test(archive_swap_test archive_swap_test.c ${PORT_SRC_DIR}/hsd_endian/archive_swap.c)
