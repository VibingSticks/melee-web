/* The byte source behind the DVD layer.
 *
 * On the web these are implemented in JavaScript (port/web/js/imports.js) over
 * the user's disc image; host tests provide fakes. Reads complete
 * asynchronously: `cb` is invoked exactly once, from the event loop, with
 * status 0 on success or -1 on failure. */
#ifndef PORT_DISC_IO_H
#define PORT_DISC_IO_H

#include <stdint.h>

typedef void (*port_disc_read_cb)(void* user, int status);

void port_disc_read(uint32_t offset, uint32_t length, void* dst, port_disc_read_cb cb, void* user);
uint32_t port_disc_size(void);

#endif
