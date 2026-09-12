/**
 * Card work area .bss/.sbss used by the card functions in hsd_3A94.c
 * (and the JPEG decoder in hsd_3B34.c). Kept in its own TU: the card
 * functions only match when this data is referenced as extern, so it
 * cannot be defined alongside them.
 */

#include <Runtime/platform.h>

#ifdef TARGET_PC
/* The card code addresses the command ring (base + 0x10) and the request
 * queue (base + 0x1210) relative to hsd_804D1138, relying on the original
 * link order of the three objects below. Keep them in one buffer; the
 * symbols are macros over it (hsd_3A94.h). */
u8 port_card_work[0x1510];
#else
/* 4D2348 */ u8 hsd_804D2348[0x300];
/* 4D1148 */ u32 hsd_804D1148[0x80][0x9];
/* 4D1138 */ u8 hsd_804D1138[0x10];
#endif

/* 4D799C */ s32 hsd_804D799C;
/* 4D7998 */ s32 hsd_804D7998;
/* 4D7994 */ s32 hsd_804D7994;
/* 4D7990 */ s32 hsd_804D7990;
/* 4D798C */ s32 hsd_804D798C;
/* 4D7988 */ s32 hsd_804D7988;
/* 4D7984 */ volatile s32 hsd_804D7984;
/* 4D7980 */ volatile s32 hsd_804D7980;
