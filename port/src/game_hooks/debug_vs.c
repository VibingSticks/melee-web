/* Test hooks that drive the game's own scene machine from JavaScript.
 *
 * Melee already ships a debug VS mode (GM_DEBUG_VS) whose enter handler fills
 * the match setup with two human players and a random stage, so starting a
 * match needs no menu navigation: ask for that mode, then let the current
 * scene finish. Compiled into the game library because it uses game headers.
 *
 *   Module._port_debug_start_vs();   // then end the current scene (press Start)
 */
#include <emscripten.h>

#include <melee/gm/forward.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmscene.h>

#include <port.h>

/* Ask for the debug VS mode and end the scene that is running, so the mode
 * machine picks it up straight away instead of waiting for the player to
 * leave the current menu. */
EMSCRIPTEN_KEEPALIVE void port_debug_start_vs(void)
{
    port_log("debug: starting the debug VS match");
    gm_ChangeGameModeAfterCurrentScene(GM_DEBUG_VS);
    gm_801A4B60(); /* end the current scene */
}
