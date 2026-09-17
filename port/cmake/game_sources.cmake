# Game sources compiled into the `melee_game` static library.
#
# Every unit under src/melee and src/sysdolphin is included unless listed in
# GAME_EXCLUDE. Each exclusion names the reason and the plan task that removes
# it; port/docs/milestones.md mirrors this list.

file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS
    ${GAME_ROOT}/src/melee/*.c
    ${GAME_ROOT}/src/sysdolphin/*.c)

# The SDK's THP video decoder, which the intro movie needs. It compiles, but it
# does NOT decode on this target: 36 asm blocks sit behind __MWERKS__ and all
# but three of them are the hot path itself -- both inverse DCTs, the Huffman
# lookup, and the three DCT component readers. Only three prime the
# paired-single quantisation registers, and even those carry meaning (GQR6
# encodes the IDCT's level-shift and clamp). Movies are skipped in lbmthp.c
# until those routines are written in C.
list(APPEND GAME_SOURCES ${GAME_ROOT}/extern/dolphin/src/dolphin/thp/THPDec.c)

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
     ${PORT_SRC_DIR}/game_hooks/debug_vs.c
     ${PORT_SRC_DIR}/hsd_endian/archive_swap.c ${PORT_SRC_DIR}/hsd_endian/formats.c
     ${SCHEMA_TABLES})   # generated port_roots[] + port_type tables

add_library(melee_game STATIC ${GAME_SOURCES})
target_include_directories(melee_game PRIVATE
    ${GAME_ROOT}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/aurora/include
    ${PORT_SRC_DIR}/compat    # <printf.h> stand-in, 4-byte bool
    ${PORT_SRC_DIR}           # <hsd_port/...>
    # Last: this tree carries a whole SDK dolphin/ that would otherwise shadow
    # Aurora's headers. Only the THP decoder's own header is wanted from it.
    ${GAME_ROOT}/extern/dolphin/include)
target_compile_definitions(melee_game PRIVATE
    TARGET_PC LINT VERSION_GALE01 BUILD_VERSION=0)   # bool is int via compat/stdbool.h
target_compile_options(melee_game PRIVATE
    -std=gnu99 -fno-strict-aliasing   # gnu99: M_PI and friends from libc
    -fgnu89-inline                    # plain `inline` emits a symbol, as CodeWarrior does
    -Wno-everything
    # Implicit declarations would give wrong wasm signatures (silent traps at the call).
    -Werror=implicit-function-declaration -Werror=implicit-int)
