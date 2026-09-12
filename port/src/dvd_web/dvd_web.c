#include "dvd_web.h"

#include <dolphin/dvd.h>
#include <stdlib.h>
#include <string.h>

#include "../port.h"
#include "disc_io.h"
#include "fst.h"

static port_fst* g_fst;
static uint8_t* g_fst_bytes;

typedef struct pending {
    DVDFileInfo* fi;
    s32 result;
    struct pending* next;
} pending;

static pending* g_done_head;
static pending* g_done_tail;
static uint32_t g_in_flight;

int port_dvd_init(const uint8_t* fst_bytes, uint32_t n)
{
    port_fst_free(g_fst);
    free(g_fst_bytes);
    g_fst = NULL;
    g_fst_bytes = malloc(n);
    if (g_fst_bytes == NULL) {
        return -1;
    }
    memcpy(g_fst_bytes, fst_bytes, n);
    g_fst = port_fst_parse(g_fst_bytes, n);
    return g_fst != NULL ? 0 : -1;
}

uint32_t port_dvd_pending(void) { return g_in_flight; }

/* --- SDK entry points --- */

void DVDInit(void) {}

s32 DVDConvertPathToEntrynum(const char* path)
{
    s32 e = g_fst != NULL ? port_fst_lookup(g_fst, path) : -1;
    if (e < 0) {
        port_log("dvd: no such file on the disc: %s", path);
    }
    return e;
}

BOOL DVDFastOpen(s32 entry, DVDFileInfo* fi)
{
    if (g_fst == NULL || entry < 0 || entry >= port_fst_entry_count(g_fst) || port_fst_is_dir(g_fst, entry)) {
        return FALSE;
    }
    memset(fi, 0, sizeof *fi);
    fi->startAddr = port_fst_file_offset(g_fst, entry);
    fi->length = port_fst_file_length(g_fst, entry);
    fi->cb.state = DVD_STATE_END;
    return TRUE;
}

BOOL DVDOpen(const char* path, DVDFileInfo* fi)
{
    s32 e = DVDConvertPathToEntrynum(path);
    return e >= 0 && DVDFastOpen(e, fi);
}

BOOL DVDClose(DVDFileInfo* fi)
{
    fi->cb.state = DVD_STATE_END;
    return TRUE;
}

static void on_read_done(void* user, int status)
{
    DVDFileInfo* fi = user;
    pending* p = calloc(1, sizeof *p);
    if (p == NULL) {
        port_log("dvd_web: out of memory queueing a read completion");
        return;
    }
    p->fi = fi;
    p->result = status == 0 ? (s32) fi->cb.transferredSize : DVD_RESULT_FATAL_ERROR;
    if (g_done_tail != NULL) {
        g_done_tail->next = p;
    } else {
        g_done_head = p;
    }
    g_done_tail = p;
}

/* Like the hardware, a read may extend past the end of the file (the game
 * rounds sizes up to 32 bytes); it is only clamped at the end of the disc, and
 * any part beyond the disc is zero-filled. */
s32 DVDReadAsyncPrio(DVDFileInfo* fi, void* addr, s32 length, s32 offset, DVDCallback callback, s32 prio)
{
    (void) prio;
    if (length < 0 || offset < 0 || addr == NULL) {
        return 0;
    }
    uint32_t disc_off = fi->startAddr + (uint32_t) offset;
    uint32_t n = (uint32_t) length;
    uint32_t disc_size = port_disc_size();
    if (disc_off >= disc_size) {
        n = 0;
    } else if (disc_off + n > disc_size) {
        n = disc_size - disc_off;
    }
    if (n < (uint32_t) length) {
        memset((uint8_t*) addr + n, 0, (uint32_t) length - n);
    }
    fi->callback = callback;
    fi->cb.state = DVD_STATE_BUSY;
    fi->cb.addr = addr;
    fi->cb.offset = disc_off;
    fi->cb.length = (u32) length;
    fi->cb.transferredSize = (u32) length;
    g_in_flight++;
    if (n == 0) {
        on_read_done(fi, 0);
    } else {
        port_disc_read(disc_off, n, addr, on_read_done, fi);
    }
    return 1;
}

s32 DVDReadPrio(DVDFileInfo* fi, void* addr, s32 length, s32 offset, s32 prio)
{
    if (!DVDReadAsyncPrio(fi, addr, length, offset, NULL, prio)) {
        return DVD_RESULT_FATAL_ERROR;
    }
    while (fi->cb.state == DVD_STATE_BUSY) {
        port_yield();
        port_dvd_pump();
    }
    return fi->cb.state == DVD_STATE_END ? (s32) fi->cb.transferredSize : DVD_RESULT_FATAL_ERROR;
}

void ARQPumpCallbacks(void);  /* Aurora AR.cpp (port patch): deferred ARAM DMA completions */
void CARDPumpCallbacks(void); /* Aurora card.cpp (port patch): deferred memory-card completions */

void port_dvd_pump(void)
{
    /* DVD completions and ARAM DMA completions feed each other (HSD's DevCom
     * relays disc data through ARAM), so drain both until nothing is left. */
    for (;;) {
        int did = 0;
        while (g_done_head != NULL) {
            pending* p = g_done_head;
            g_done_head = p->next;
            if (g_done_head == NULL) {
                g_done_tail = NULL;
            }
            g_in_flight--;
            DVDFileInfo* fi = p->fi;
            s32 result = p->result;
            free(p);
            fi->cb.state = result < 0 ? DVD_STATE_FATAL_ERROR : DVD_STATE_END;
            if (fi->callback != NULL) {
                fi->callback(result, fi);
            }
            did = 1;
        }
        ARQPumpCallbacks();
        CARDPumpCallbacks();
        if (!did) {
            break;
        }
    }
}

s32 DVDGetCommandBlockStatus(const DVDCommandBlock* b) { return b->state; }
s32 DVDGetFileInfoStatus(const DVDFileInfo* fi) { return fi->cb.state; }
s32 DVDGetDriveStatus(void) { return DVD_STATE_END; }
BOOL DVDCheckDisk(void) { return TRUE; }

DVDDiskID* DVDGetCurrentDiskID(void)
{
    static DVDDiskID id = { { 'G', 'A', 'L', 'E' }, { '0', '1' }, 0, 2, 1, 0, { 0 } };
    return &id;
}

