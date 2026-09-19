/* Dolphin DVD API over the JS disc byte source.
 *
 * Only the entry points the game uses are implemented (see dvd_web.c). Reads
 * are issued to port_disc_read(); completions are queued and their DVD
 * callbacks run from port_dvd_pump(), which the main loop calls once per frame
 * and port_yield() calls before yielding. That preserves the game's
 * expectation that DVD callbacks run outside the code that issued the read. */
#ifndef PORT_DVD_WEB_H
#define PORT_DVD_WEB_H

#include <stdint.h>

/* Install the FST (big-endian bytes, copied). Call before the game's DVDInit. */
int port_dvd_init(const uint8_t* fst_bytes, uint32_t fst_size);

/* Run the callbacks of every completed read. */
void port_dvd_pump(void);

/* Number of reads issued but not yet completed (for tests and diagnostics). */
uint32_t port_dvd_pending(void);

/* Reads completed since the last reset: how many, how many bytes, and the
 * wall time from issue to completion (summed, and the single longest). */
void port_dvd_stats(unsigned* reads, unsigned* bytes, double* wait_ms, double* max_ms);
void port_dvd_stats_reset(void);

#endif
