#include "save_web.h"

#include <emscripten.h>

#include "port.h"

/* IndexedDB is asynchronous and the game is not, so every operation here sets
 * a JS global that this side polls while yielding. 0 pending, 1 done, -1
 * failed. */
#define SAVE_STATE "(window.__meleeSave|0)"

static int g_available;
static int g_flush_in_flight;
static unsigned g_dirty_since; /* frame the card last looked changed, 0 if clean */
static unsigned g_frames;

/* A burst of CARD writes should cost one store, not one per write, so wait
 * this many frames after the last change before writing back. */
#define SAVE_DEBOUNCE_FRAMES 45

int port_save_mount(void)
{
    /* Ask the browser to keep this origin's storage. Without it IndexedDB is
     * "best effort" and may be evicted under storage pressure, which for the
     * player means their save quietly disappearing. Chrome decides from site
     * engagement rather than prompting, so this is a request, not a
     * guarantee -- log what we were given. */
    EM_ASM({
        if (navigator.storage && navigator.storage.persist) {
            navigator.storage.persisted().then(function(already) {
                if (already) {
                    console.log('[melee] saves: storage is already persistent');
                    return;
                }
                navigator.storage.persist().then(function(granted) {
                    console.log(granted ? '[melee] saves: storage is now persistent'
                                        : '[melee] saves: the browser may evict saves under storage pressure');
                });
            }).catch(function(e) { console.warn('[melee] saves: storage policy unknown:', e); });
        }
    });

    EM_ASM({
        window.__meleeSave = 0;
        try {
            try {
                FS.mkdir(UTF8ToString($0));
            } catch (e) {
                if (!e || e.errno !== 20) { /* EEXIST is fine */
                }
            }
            FS.mount(IDBFS, {}, UTF8ToString($0));
            /* true = read IndexedDB into the in-memory filesystem. */
            FS.syncfs(true, function(err) {
                if (err) {
                    console.error('[melee] saves: could not read stored data:', err);
                    window.__meleeSave = -1;
                } else {
                    window.__meleeSave = 1;
                }
            });
        } catch (e) {
            console.error('[melee] saves: IndexedDB is not usable here:', e);
            window.__meleeSave = -1;
        }
    }, PORT_SAVE_DIR);

    /* Aurora opens the card the moment we return, and formats a fresh one if
     * the file is not there yet -- so this has to finish first or the player's
     * save is overwritten by an empty card. */
    int spins = 0;
    while (emscripten_run_script_int(SAVE_STATE) == 0) {
        emscripten_sleep(8);
        if (++spins > 1250) { /* ~10 s */
            port_log("saves: IndexedDB did not answer; this session will not be remembered");
            return 0;
        }
    }

    g_available = emscripten_run_script_int(SAVE_STATE) == 1;
    port_log("saves: %s", g_available ? "IndexedDB mounted at " PORT_SAVE_DIR : "unavailable; progress will not be kept");
    return g_available;
}

int port_save_available(void)
{
    return g_available;
}

/* Cheap change check: the card is one or two files, so a signature over their
 * sizes and modification times is enough to notice a write without hashing
 * the image every frame. */
static int card_changed(void)
{
    return EM_ASM_INT({
        var dir = UTF8ToString($0);
        var sig = 0;
        try {
            /* Recursive: Aurora keeps the card in a folder of its own
               (/saves/<region>/Card A/*.gci), and a directory's own mtime does
               not move when a file deeper inside it is written. Stat'ing only
               the top level would miss every real save. */
            var walk = function(d) {
                var names = FS.readdir(d);
                for (var i = 0; i < names.length; i++) {
                    if (names[i] === '.' || names[i] === '..') continue;
                    var full = d + '/' + names[i];
                    var st = FS.stat(full);
                    if (FS.isDir(st.mode)) {
                        walk(full);
                        continue;
                    }
                    /* |0 keeps this in int range; collisions only delay a store
                       to the next change, they cannot lose one. */
                    sig = (sig * 31 + st.size + (st.mtime ? st.mtime.getTime() : 0)) | 0;
                    for (var k = 0; k < full.length; k++) {
                        sig = (sig * 31 + full.charCodeAt(k)) | 0;
                    }
                }
            };
            walk(dir);
        } catch (e) {
            return 0;
        }
        if (window.__meleeSaveSig === undefined) {
            window.__meleeSaveSig = sig;
            return 0;
        }
        if (window.__meleeSaveSig !== sig) {
            window.__meleeSaveSig = sig;
            return 1;
        }
        return 0;
    }, PORT_SAVE_DIR);
}

void port_save_flush(void)
{
    if (!g_available || g_flush_in_flight) {
        return;
    }
    g_flush_in_flight = 1;
    g_dirty_since = 0;
    EM_ASM({
        /* false = write the in-memory filesystem back to IndexedDB. */
        FS.syncfs(false, function(err) {
            if (err) {
                console.error('[melee] saves: could not store:', err);
            }
            window.__meleeSaveFlush = 1;
        });
    });
}

void port_save_tick(void)
{
    if (!g_available) {
        return;
    }
    g_frames++;

    if (g_flush_in_flight) {
        if (emscripten_run_script_int("(window.__meleeSaveFlush|0)") == 1) {
            EM_ASM({ window.__meleeSaveFlush = 0; });
            g_flush_in_flight = 0;
            port_log("saves: stored");
        }
        return;
    }

    /* Checking every frame would stat the directory 60 times a second for no
     * reason; a quarter-second is far inside a human's save-and-reload. */
    if (g_frames % 15 == 0 && card_changed()) {
        g_dirty_since = g_frames;
    }
    if (g_dirty_since != 0 && g_frames - g_dirty_since >= SAVE_DEBOUNCE_FRAMES) {
        port_save_flush();
    }
}
