/* Shared declarations for the port-only sources under port/src.
 *
 * These files are compiled without the game's compat headers: `bool` here is
 * the C99 one, matching Aurora's C++ side. */
#ifndef PORT_H
#define PORT_H

#include <stdint.h>

/* Print to the browser console (or stderr on the host) with a "[melee]" prefix. */
void port_log(const char* fmt, ...);

/* Give control back to the browser event loop (Asyncify). No-op on the host.
 * Any wait loop that expects a JS callback (disc read, WebGPU) must call this. */
void port_yield(void);

/* Ask the main loop to stop after the current frame. */
void port_request_exit(void);

/* The name of the disc file (its FST entry name, e.g. "GrCs.dat") whose first
 * byte the DVD layer most recently read into the buffer starting at `buf`;
 * NULL when no such read is on record. An archive parsed out of that buffer
 * has no name of its own (HSD_Archive::name is never set), and the archive
 * schema needs one to tell apart root symbols that mean a different struct in
 * every stage archive (port/src/hsd_endian/archive_swap.c). */
const char* port_disc_file_at(const void* buf);

#endif
