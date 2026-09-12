set(AURORA_INC ${CMAKE_SOURCE_DIR}/extern/aurora/include)

function(port_add_test name)
    add_executable(${name} ${ARGN} port_host_stubs.c)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                               ${PORT_SRC_DIR} ${AURORA_INC})
    target_compile_options(${name} PRIVATE -Wall -Wextra -std=c99)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

port_add_test(smoke_test smoke_test.c)
port_add_test(os_alarm_test os_alarm_test.c ${PORT_SRC_DIR}/os_shim/os_alarm.c)
port_add_test(fst_test fst_test.c ${PORT_SRC_DIR}/dvd_web/fst.c)
port_add_test(dvd_web_test dvd_web_test.c ${PORT_SRC_DIR}/dvd_web/dvd_web.c
              ${PORT_SRC_DIR}/dvd_web/fst.c)
