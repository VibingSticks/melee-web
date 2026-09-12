# Game sources compiled into the `melee_game` static library.
#
# Every unit under src/melee and src/sysdolphin is included unless listed in
# GAME_EXCLUDE. Each exclusion names the reason and the plan task that removes
# it; port/docs/milestones.md mirrors this list.

file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS
    ${GAME_ROOT}/src/melee/*.c
    ${GAME_ROOT}/src/sysdolphin/*.c)

set(GAME_EXCLUDE
    dberror.c debug.c debugconsole_main.c   # PPC register dumps / debug console thread (never)
    hsd_3915.c sislib_font.c                # font atlases extracted from the DOL (Task 17)

)
foreach (x IN LISTS GAME_EXCLUDE)
    list(FILTER GAME_SOURCES EXCLUDE REGEX "/${x}$")
endforeach ()

# MSL pieces the game references directly (an infinity constant).
list(APPEND GAME_SOURCES ${GAME_ROOT}/src/MSL/float.c)

# Port helpers that must see the game's headers and 4-byte bool.
list(APPEND GAME_SOURCES ${PORT_SRC_DIR}/hsd_port/vtx_arrays.c ${PORT_SRC_DIR}/hsd_port/font_atlas_stub.c)

add_library(melee_game STATIC ${GAME_SOURCES})
target_include_directories(melee_game PRIVATE
    ${GAME_ROOT}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/aurora/include
    ${PORT_SRC_DIR}/compat    # <printf.h> stand-in, 4-byte bool
    ${PORT_SRC_DIR})          # <hsd_port/...>
target_compile_definitions(melee_game PRIVATE
    TARGET_PC LINT VERSION_GALE01 BUILD_VERSION=0)   # bool is int via compat/stdbool.h
target_compile_options(melee_game PRIVATE
    -std=gnu99 -fno-strict-aliasing   # gnu99: M_PI and friends from libc
    -fgnu89-inline                    # plain `inline` emits a symbol, as CodeWarrior does
    -Wno-everything
    # Implicit declarations would give wrong wasm signatures (silent traps at the call).
    -Werror=implicit-function-declaration -Werror=implicit-int)
