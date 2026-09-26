# Game sources compiled into the `melee_game` static library.
#
# Every unit under src/melee and src/sysdolphin is included unless listed in
# GAME_EXCLUDE. Each exclusion names the reason and the plan task that removes
# it; port/docs/milestones.md mirrors this list.

file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS
    ${GAME_ROOT}/src/melee/*.c
    ${GAME_ROOT}/src/sysdolphin/*.c)

# The SDK's THP video decoder, which the movies (.mth) and ending stills (.thp)
# need. Its Huffman decoding and inverse DCTs are PowerPC asm behind
# __MWERKS__, so on this target THPVideoDecode and the two tile stores hand
# the frame to a portable JPEG decoder (port/src/thp_web/thp_jpeg.c).
list(APPEND GAME_SOURCES ${GAME_ROOT}/libs/dolphin/src/dolphin/thp/THPDec.c)

set(GAME_EXCLUDE
    dberror.c debug.c debugconsole_main.c   # PPC register dumps / debug console thread (never)

)
foreach (x IN LISTS GAME_EXCLUDE)
    list(FILTER GAME_SOURCES EXCLUDE REGEX "/${x}$")
endforeach ()

# MSL pieces the game references directly (an infinity constant).
list(APPEND GAME_SOURCES ${GAME_ROOT}/src/MSL/float.c)

# Port helpers that must see the game's headers and 4-byte bool.
list(APPEND GAME_SOURCES ${PORT_SRC_DIR}/hsd_port/vtx_arrays.c ${PORT_SRC_DIR}/hsd_port/font_atlas_stub.c
     ${PORT_SRC_DIR}/game_hooks/debug_vs.c ${PORT_SRC_DIR}/game_hooks/ft_articles.c
     ${PORT_SRC_DIR}/game_hooks/ptr_check.c ${PORT_SRC_DIR}/game_hooks/pad_queue.c
     ${PORT_SRC_DIR}/hsd_endian/archive_swap.c ${PORT_SRC_DIR}/hsd_endian/formats.c
     ${SCHEMA_TABLES})   # generated port_roots[] + port_type tables

# String literals in Shift-JIS, as sjiswrap gives the GameCube compiler (see
# tools/sjis_literals.py): a converted copy of each file with non-ASCII
# literals is compiled instead, with the original's directory on the include
# path so its "" includes still resolve.
execute_process(
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/sjis_literals.py --list ${GAME_SOURCES}
    OUTPUT_VARIABLE SJIS_SOURCES
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
string(REPLACE "\n" ";" SJIS_SOURCES "${SJIS_SOURCES}")
foreach (src IN LISTS SJIS_SOURCES)
    file(RELATIVE_PATH rel ${GAME_ROOT} ${src})
    set(out ${CMAKE_BINARY_DIR}/sjis/${rel})
    get_filename_component(src_dir ${src} DIRECTORY)
    add_custom_command(
        OUTPUT ${out}
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/sjis_literals.py ${src} ${out}
        DEPENDS ${src} ${CMAKE_CURRENT_SOURCE_DIR}/tools/sjis_literals.py
        COMMENT "Shift-JIS literals: ${rel}")
    set_source_files_properties(${out} PROPERTIES INCLUDE_DIRECTORIES ${src_dir})
    list(REMOVE_ITEM GAME_SOURCES ${src})
    list(APPEND GAME_SOURCES ${out})
endforeach ()

add_library(melee_game STATIC ${GAME_SOURCES})
target_include_directories(melee_game PRIVATE
    ${GAME_ROOT}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/aurora/include
    ${PORT_SRC_DIR}/compat    # <printf.h> stand-in, 4-byte bool
    ${PORT_SRC_DIR}           # <hsd_port/...>
    # Last: this tree carries a whole SDK dolphin/ that would otherwise shadow
    # Aurora's headers. Only the THP decoder's own header is wanted from it.
    ${GAME_ROOT}/libs/dolphin/include)
target_compile_definitions(melee_game PRIVATE
    TARGET_PC LINT VERSION_GALE01 BUILD_VERSION=0)   # bool is int via compat/stdbool.h
target_compile_options(melee_game PRIVATE
    -std=gnu99 -fno-strict-aliasing   # gnu99: M_PI and friends from libc
    -fgnu89-inline                    # plain `inline` emits a symbol, as CodeWarrior does
    -Wno-everything
    # Implicit declarations would give wrong wasm signatures (silent traps at the call).
    -Werror=implicit-function-declaration -Werror=implicit-int)
