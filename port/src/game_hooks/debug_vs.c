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

/* The stage the debug VS mode's enter handler (gmvsmode.c onEnterDebugVs)
 * should put in the rules instead of its default; -1 leaves the default.
 * Lets a test load one particular stage archive on demand. */
int port_debug_vs_stkind = -1;

/* Set while a test wants the next mode to be the debug VS mode whatever the
 * exiting mode's handler asked for; runGameMode (gm_1A3F.c) consumes it. The
 * title screen, for one, always routes to the menu or the attract demo on
 * exit, so gm_ChangeGameModeAfterCurrentScene alone is not enough there. */
int port_debug_vs_requested = 0;

/* The fighters the debug VS mode should give players 1 and 2 (CharacterKind);
 * -1 keeps its defaults, Link and Mario. */
int port_debug_vs_ckind[2] = { -1, -1 };

/* Player 2 as a CPU of this level (1..9); -1 keeps it a second human. */
int port_debug_vs_cpu_level = -1;

/* The match's time limit in seconds; 0 keeps the mode's default. */
int port_debug_vs_time_limit = 0;

/*   Module._port_debug_set_vs_time_limit(seconds);   // before starting */
EMSCRIPTEN_KEEPALIVE void port_debug_set_vs_time_limit(int seconds)
{
    port_debug_vs_time_limit = seconds;
}

/*   Module._port_debug_start_vs_stage(stkind);   // StKind, e.g. 4 = Castle */
EMSCRIPTEN_KEEPALIVE void port_debug_start_vs_stage(int stkind)
{
    port_debug_vs_stkind = stkind;
    port_debug_vs_requested = 1;
    port_log("debug: starting the debug VS match on stage %d", stkind);
    gm_ChangeGameModeAfterCurrentScene(GM_DEBUG_VS);
    gm_801A4B60(); /* end the current scene */
}

/*   Module._port_debug_start_vs_match(stkind, p1_ckind, p2_ckind);
 * Same, naming both fighters (CharacterKind; -1 keeps the default). */
EMSCRIPTEN_KEEPALIVE void port_debug_start_vs_match(int stkind, int p1, int p2)
{
    port_debug_vs_ckind[0] = p1;
    port_debug_vs_ckind[1] = p2;
    port_debug_start_vs_stage(stkind);
}

/*   Module._port_debug_start_vs_cpu(stkind, p1_ckind, p2_ckind, level);
 * Same, with player 2 a computer player of `level` (1..9). */
EMSCRIPTEN_KEEPALIVE void port_debug_start_vs_cpu(int stkind, int p1, int p2, int level)
{
    port_debug_vs_cpu_level = level;
    port_debug_start_vs_match(stkind, p1, p2);
}

/*   Module._port_debug_spawn_item(kind, x, y);
 * Drops an item of ItemKind `kind` at stage position (x, y), as the item
 * spawner does (it_8026D258). For reproducing item bugs without waiting for
 * the random spawner. Returns 1 if the item limit allowed it. */
#include <melee/it/itspawn.h>
EMSCRIPTEN_KEEPALIVE int port_debug_spawn_item(int kind, float x, float y)
{
    Vec3 pos = { x, y, 0.0f };
    port_log("debug: spawning item kind %d at (%.1f, %.1f)", kind, x, y);
    return it_8026D258(&pos, (ItemKind) kind) ? 1 : 0;
}

/*   Module._port_debug_spawn_item_at_player(kind, slot);
 * Same, at the feet of the fighter in player slot `slot`, so a scripted
 * probe can pick the item up with A and throw it. */
#include <melee/pl/player.h>
#include <melee/ft/ftlib.h>
EMSCRIPTEN_KEEPALIVE int port_debug_spawn_item_at_player(int kind, int slot)
{
    HSD_GObj* fighter = Player_GetEntity(slot);
    Vec3 pos;
    if (fighter == NULL) {
        return 0;
    }
    ftLib_80086644(fighter, &pos);
    pos.y += 2.0f;
    pos.z = 0.0f;
    port_log("debug: spawning item kind %d at player %d (%.1f, %.1f)", kind, slot, pos.x, pos.y);
    return it_8026D258(&pos, (ItemKind) kind) ? 1 : 0;
}

/*   Module._port_debug_set_pokemon(n);
 * The Pokemon the next Poke Ball releases, through the game's own debug item
 * menu variable (it_8027AB64 reads db_GetCurrentlySelectedPokemon): n is
 * 1 + (kind - It_PKind_Start); 0 restores the random choice. */
void port_debug_db_set_pokemon(int n); /* dbitem.c: the variable is file-local */
EMSCRIPTEN_KEEPALIVE void port_debug_set_pokemon(int n)
{
    port_debug_db_set_pokemon(n);
}
