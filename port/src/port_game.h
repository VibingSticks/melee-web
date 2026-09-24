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

/* Diagnostics: yields since the last vblank, and the milliseconds they took. */
unsigned port_yield_count(void);
double port_yield_ms(void);

/* On hardware the game tells ARAM addresses (small offsets) from main memory
 * pointers (>= 0x80000000). On the port main memory is Aurora's MEM1 block and
 * ARAM is a separate buffer addressed by offset, so the test is a range check. */
int port_is_aram_address(unsigned long addr);

/* Browser console, "[melee]" prefix. */
void port_log(const char* fmt, ...);

/* Converts a fighter's item Article (ftData x48, big-endian until now) when
 * it_8026B3F8 registers it; `slot` is its kind relative to It_Kind_Kuriboh. */
void port_swap_ft_article(void* article, int slot);

/* Converts one of the models a fighter's code loads from ftData x48 past
 * its Articles: kind 0 an HSD_Joint, 1 an HSD_AnimJoint, 2 an
 * HSD_MatAnimJoint. Safe to call again for the same object. */
enum { PORT_FT_JOINT, PORT_FT_ANIMJOINT, PORT_FT_MATANIMJOINT };
void port_swap_ft_model(void* obj, int kind);

/* 1 when `p` lies inside the wasm heap (NULL and wild values are 0). */
int port_ptr_ok(const void* p);

/* Aurora (port patch): deliver deferred memory-card operation callbacks. */
void CARDPumpCallbacks(void);

#endif
