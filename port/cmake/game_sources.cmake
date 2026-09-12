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
    lb_01F8.c lbmthp.c                      # THP movie decoding (Task 22)
    fog.c pobj.c video.c lb_0195.c gmmain.c lbcardnew.c  # Aurora API gaps / entry point (Tasks 9-10)
)
foreach (x IN LISTS GAME_EXCLUDE)
    list(FILTER GAME_SOURCES EXCLUDE REGEX "/${x}$")
endforeach ()

add_library(melee_game STATIC ${GAME_SOURCES})
target_include_directories(melee_game PUBLIC
    ${GAME_ROOT}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/aurora/include
    ${PORT_SRC_DIR}/compat)   # <printf.h> stand-in
target_compile_definitions(melee_game PUBLIC
    TARGET_PC LINT VERSION_GALE01 BUILD_VERSION=0)   # bool is int via compat/stdbool.h
target_compile_options(melee_game PRIVATE
    -std=gnu99 -fno-strict-aliasing   # gnu99: M_PI and friends from libc
    -Wno-everything)
