/* The port's side of the AX mixer (src/ax_hle/ax_hle.c). */
#ifndef PORT_AX_HLE_H
#define PORT_AX_HLE_H

/* Opens the output stream. Call once, after SDL is up. */
void port_ax_init(void);

/* Runs the AX frames due since the last call: each one invokes the game's
 * registered AX callback, mixes 160 samples and queues them. Call once per
 * video frame with from_frame non-zero, and from port_yield with it zero: a
 * yield happens in the middle of scene code, so audio keeps flowing but the
 * game is not re-entered. */
void port_ax_pump(int from_frame);

#endif
