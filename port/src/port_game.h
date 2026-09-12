/* Port entry points that game code calls from inside #ifdef TARGET_PC blocks.
 * Plain C, no bool, includable with either the game's or Aurora's headers. */
#ifndef PORT_GAME_H
#define PORT_GAME_H

/* One virtual video retrace: present the frame drawn so far, pace to 60 Hz
 * while yielding to the browser, run the VI pre/post retrace callbacks, and
 * begin the next frame. This is the port's only per-frame pacing point. */
void port_vblank(void);

/* Yield to the browser without advancing the retrace clock (used inside
 * busy-waits such as disc loads). */
void port_yield(void);

/* Disc reads issued but not yet completed. */
unsigned port_dvd_pending(void);

/* On hardware the game tells ARAM addresses (small offsets) from main memory
 * pointers (>= 0x80000000). On the port main memory is Aurora's MEM1 block and
 * ARAM is a separate buffer addressed by offset, so the test is a range check. */
int port_is_aram_address(unsigned long addr);

/* Browser console, "[melee]" prefix. */
void port_log(const char* fmt, ...);

#endif
