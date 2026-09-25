# Link settings for the wasm executable. The WebGPU and SDL3 ports are attached
# through Aurora's patched CMake (see extern/aurora-patches).
set(PORT_LINK_FLAGS
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=256MB
    -sMAXIMUM_MEMORY=2048MB
    -sSTACK_SIZE=4MB                   # PAD_STACK arrays and large locals
    -sNO_EXIT_RUNTIME=1
    # Memory cards live on an IDBFS mount so saves outlive the tab
    # (see port/src/save_web.c).
    -lidbfs.js
    # A factory rather than a self-starting script: boot.js needs the disc before
    # the runtime starts, and the packed offline file has no separate melee.js to
    # append (see tools/pack_single_html.py).
    -sMODULARIZE=1
    -sEXPORTED_FUNCTIONS=_main,_port_dvd_init,_port_pad_virtual,_port_pad_virtual_clear,_port_debug_start_vs,_port_scene_state,_port_menu_state,_port_css_slot,_port_css_stage,_port_frame_count,_port_css_live_slot,_port_css_live_status,_port_css_cursor,_port_css_cursor_count,_port_css_icon_count,_port_css_icon_pos,_port_css_icon_info,_malloc,_free
    -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,callMain,HEAPU8,HEAPF32,FS
    --js-library ${CMAKE_CURRENT_SOURCE_DIR}/web/js/imports.js
    -Wl,--error-limit=0
)
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    list(APPEND PORT_LINK_FLAGS -sASSERTIONS=1 -g2 --profiling-funcs)
else ()
    # Keep function names in release too. Without the name section a wasm trap
    # reports "memory access out of bounds" and frames like wasm-function[9090],
    # which is nothing to go on -- a crash report from someone else's machine
    # has to name the function to be worth sending. The names live in a custom
    # section the engine never loads into linear memory, so the cost is download
    # size, not the game's heap.
    list(APPEND PORT_LINK_FLAGS -sASSERTIONS=0 --profiling-funcs)
endif ()
if (PORT_SINGLE_FILE)
    list(APPEND PORT_LINK_FLAGS -sSINGLE_FILE=1)
endif ()

# Two engines from one compile: suspending at a yield is a link-time choice.
#   melee.js       Asyncify: every function a yield can be reached from is
#                  rewritten to unwind and rewind its frame. Runs everywhere.
#   melee-jspi.js  JavaScript Promise Integration: the engine suspends the wasm
#                  stack itself, so nothing is rewritten -- a third smaller and
#                  faster on game code. Needs WebAssembly.Suspending (Chrome
#                  137+); boot.js picks it when the browser has it.
set(PORT_LINK_FLAGS_ASYNCIFY
    -sASYNCIFY
    -sASYNCIFY_STACK_SIZE=1048576      # the game yields deep inside scene code
    -sEXPORT_NAME=createMelee)
set(PORT_LINK_FLAGS_JSPI
    -sJSPI
    -sEXPORT_NAME=createMeleeJspi)
