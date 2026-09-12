#ifndef PORT_VI_SHIM_H
#define PORT_VI_SHIM_H

#include <stdint.h>

/* Run the game's VI pre- and post-retrace callbacks once. */
void port_vi_retrace(void);
uint32_t port_vi_retrace_count(void);

#endif
