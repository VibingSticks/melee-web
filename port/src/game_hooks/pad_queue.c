/* The frame pump's view of the game's pad queue: when it is full, the next
 * pad sample merges into the oldest one (HSD_PadRenewRawStatus), so that
 * vblank runs no logic frame of its own. */
#include <sysdolphin/baselib/controller.h>

#include <port_game.h>

int port_pad_queue_full(void)
{
    return HSD_PadLibData.qnum != 0 && HSD_PadLibData.qcount >= HSD_PadLibData.qnum;
}
