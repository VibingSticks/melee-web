function(port_add_test name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                               ${PORT_SRC_DIR})
    target_compile_options(${name} PRIVATE -Wall -Wextra -std=c99)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

port_add_test(smoke_test smoke_test.c)
