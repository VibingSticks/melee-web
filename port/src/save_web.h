/* Memory-card persistence for the web build.
 *
 * Aurora's CARD implementation is a real one, but it keeps the card image on
 * the filesystem -- which under Emscripten is MEMFS, in memory, discarded when
 * the tab closes. A save written there survives exactly until the page
 * reloads, which is the same as no saves at all.
 *
 * So the card directory is an IDBFS mount instead: the browser's IndexedDB
 * backs it, the contents are read in before Aurora opens the card, and changes
 * are written back after the game finishes writing. */
#ifndef PORT_SAVE_WEB_H
#define PORT_SAVE_WEB_H

/* The directory Aurora is told to keep its card image in (cfg.userPath). */
#define PORT_SAVE_DIR "/saves"

/* Mount IDBFS and read the stored card in. Blocks (through Asyncify) until the
 * read completes, because Aurora opens the card immediately afterwards and
 * would otherwise format an empty one over the player's save. Safe to call
 * once, before aurora_initialize. Returns 1 on success, 0 if persistence is
 * unavailable -- the game still runs, it just will not remember. */
int port_save_mount(void);

/* Called once per frame. Writes the card back to IndexedDB when it has
 * changed, debounced so a burst of writes costs one store. */
void port_save_tick(void);

/* Write back now, if anything is pending. */
void port_save_flush(void);

/* Whether the mount succeeded, for the status line and the tests. */
int port_save_available(void);

#endif
