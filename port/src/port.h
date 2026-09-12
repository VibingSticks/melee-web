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

#endif
