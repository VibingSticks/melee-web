#include "check.h"
#include "dvd_web/disc_io.h"
#include "dvd_web/dvd_web.h"

#include <dolphin/dvd.h>

/* --- fake disc: reads are queued and delivered by the test --- */
static uint8_t disc[0x2000];
static struct {
    port_disc_read_cb cb;
    void* user;
    uint32_t off, len;
    void* dst;
    int pending;
    int fail;
} q;

void port_disc_read(uint32_t off, uint32_t len, void* dst, port_disc_read_cb cb, void* user)
{
    CHECK(!q.pending); /* the layer must not issue overlapping reads for one file */
    q.cb = cb; q.user = user; q.off = off; q.len = len; q.dst = dst; q.pending = 1;
}

uint32_t port_disc_size(void) { return sizeof disc; }

static void deliver(void)
{
    CHECK(q.pending);
    q.pending = 0;
    if (!q.fail) {
        memcpy(q.dst, disc + q.off, q.len);
    }
    q.cb(q.user, q.fail ? -1 : 0);
}

static int done_result = -99;
static DVDFileInfo* done_fi;
static void cb(s32 result, DVDFileInfo* fi) { done_result = result; done_fi = fi; }

static void be32(uint8_t* p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

static void run(void)
{
    /* FST: root(next=2), "a.dat" at 0x1000 len 16 */
    uint8_t fst[12 * 2 + 8] = {0};
    be32(fst + 0, 0x01000000u); be32(fst + 8, 2);
    be32(fst + 12, 0x00000001u); be32(fst + 16, 0x1000); be32(fst + 20, 16);
    memcpy(fst + 24, "\0a.dat\0", 7);
    memcpy(disc + 0x1000, "0123456789abcdefGHIJKLMNOPQRSTUV", 32);

    CHECK_EQ_U32(port_dvd_init(fst, sizeof fst), 0);
    CHECK_EQ_U32(DVDConvertPathToEntrynum("a.dat"), 1);
    CHECK_EQ_U32(DVDConvertPathToEntrynum("nope.dat"), (uint32_t) -1);

    DVDFileInfo fi;
    CHECK(DVDFastOpen(1, &fi));
    CHECK_EQ_U32(fi.length, 16);
    CHECK_EQ_U32(fi.startAddr, 0x1000);
    CHECK(!DVDFastOpen(0, &fi)); /* directory */
    CHECK(!DVDFastOpen(7, &fi)); /* out of range */
    CHECK(DVDFastOpen(1, &fi));

    /* async read of 32 bytes (rounded up past the 16-byte file, like the game does) */
    uint8_t out[32];
    memset(out, 0xAA, sizeof out);
    done_result = -99;
    CHECK_EQ_U32(DVDReadAsyncPrio(&fi, out, 32, 0, cb, 2), 1);
    CHECK_EQ_U32(DVDGetCommandBlockStatus(&fi.cb), DVD_STATE_BUSY);
    CHECK_EQ_U32(port_dvd_pending(), 1);
    CHECK_EQ_U32(q.off, 0x1000);
    CHECK_EQ_U32(q.len, 32);
    port_dvd_pump();
    CHECK_EQ_U32(done_result, (uint32_t) -99); /* nothing delivered yet */
    deliver();
    CHECK_EQ_U32(done_result, (uint32_t) -99); /* callback deferred to the pump */
    CHECK_EQ_U32(DVDGetCommandBlockStatus(&fi.cb), DVD_STATE_BUSY);
    port_dvd_pump();
    CHECK_EQ_U32(done_result, 32);
    CHECK(done_fi == &fi);
    CHECK_MEM_EQ(out, "0123456789abcdefGHIJKLMNOPQRSTUV", 32);
    CHECK_EQ_U32(DVDGetCommandBlockStatus(&fi.cb), DVD_STATE_END);
    CHECK_EQ_U32(port_dvd_pending(), 0);

    /* read with an offset inside the file */
    done_result = -99;
    CHECK_EQ_U32(DVDReadAsyncPrio(&fi, out, 8, 4, cb, 2), 1);
    deliver();
    port_dvd_pump();
    CHECK_EQ_U32(done_result, 8);
    CHECK_MEM_EQ(out, "456789ab", 8);

    /* read running past the end of the disc is clamped and zero-filled */
    DVDFileInfo tail;
    CHECK(DVDFastOpen(1, &tail));
    tail.startAddr = 0x2000 - 8; /* pretend the file sits at the very end */
    memset(out, 0xAA, sizeof out);
    done_result = -99;
    CHECK_EQ_U32(DVDReadAsyncPrio(&tail, out, 32, 0, cb, 2), 1);
    CHECK_EQ_U32(q.len, 8);
    deliver();
    port_dvd_pump();
    CHECK_EQ_U32(done_result, 32);
    CHECK_EQ_U32(out[8], 0);
    CHECK_EQ_U32(out[31], 0);

    /* a failed read reports a fatal error state */
    done_result = -99;
    q.fail = 1;
    CHECK_EQ_U32(DVDReadAsyncPrio(&fi, out, 16, 0, cb, 2), 1);
    deliver();
    port_dvd_pump();
    q.fail = 0;
    CHECK_EQ_U32(done_result, (uint32_t) DVD_RESULT_FATAL_ERROR);
    CHECK_EQ_U32(DVDGetCommandBlockStatus(&fi.cb), DVD_STATE_FATAL_ERROR);

    /* invalid arguments are rejected without touching the disc */
    CHECK_EQ_U32(DVDReadAsyncPrio(&fi, out, -1, 0, cb, 2), 0);
    CHECK_EQ_U32(DVDReadAsyncPrio(&fi, NULL, 16, 0, cb, 2), 0);
    CHECK(!q.pending);

    /* the callback is optional */
    CHECK_EQ_U32(DVDReadAsyncPrio(&fi, out, 16, 0, NULL, 2), 1);
    deliver();
    port_dvd_pump();
    CHECK_EQ_U32(DVDGetCommandBlockStatus(&fi.cb), DVD_STATE_END);

    CHECK(DVDClose(&fi));
    CHECK_EQ_U32(DVDGetDriveStatus(), DVD_STATE_END);
    CHECK(DVDCheckDisk());
    CHECK_MEM_EQ(DVDGetCurrentDiskID()->gameName, "GALE", 4);
}

TEST_MAIN(run)
