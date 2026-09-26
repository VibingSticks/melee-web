/* Baseline JPEG decoding for Melee's movie frames; see thp_jpeg.h.
 *
 * THP frames are ordinary baseline JPEG (SOI, DQT, DHT, SOF0, optional DRI,
 * SOS) with three quirks: the chroma is always 4:2:0 (Y 2x2, U 1x1, V 1x1),
 * the entropy-coded data is not byte-stuffed (a 0xFF in it is just data), and
 * a restart only realigns to the next byte. The inverse DCT is the usual
 * separable integer form (the IJG "islow" factorisation, 12-bit constants). */
#include "thp_jpeg.h"

#include <string.h>

typedef struct {
    uint8_t bits[17];      /* codes of each length 1..16 */
    uint8_t vals[256];
    int32_t mincode[17];
    int32_t maxcode[18];   /* -1 when no code has that length */
    int32_t valptr[17];
    uint8_t fast[512];     /* 9-bit lookup: symbol index + 1, or 0 */
    uint8_t fastlen[512];
    int present;
} Huff;

typedef struct {
    const uint8_t* p;
    const uint8_t* end;
    uint32_t buf;
    int cnt;
    int stuffed;
    int overrun;
} Bits;

typedef struct {
    int id, h, v, tq, td, ta;
    int pred;
} Comp;

typedef struct {
    uint16_t q[4][64];
    Huff dc[4], ac[4];
    Comp comp[3];
    int ncomp;
    int w, h;
    int restart;
} Ctx;

static const uint8_t kZigzag[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

static void build_huff(Huff* h)
{
    int code = 0, k = 0;
    memset(h->fast, 0, sizeof h->fast);
    for (int l = 1; l <= 16; l++) {
        h->valptr[l] = k;
        h->mincode[l] = code;
        if (h->bits[l] != 0) {
            for (int i = 0; i < h->bits[l]; i++, k++, code++) {
                if (l <= 9) {
                    int shift = 9 - l;
                    for (int j = 0; j < (1 << shift); j++) {
                        h->fast[(code << shift) | j] = (uint8_t) (k + 1);
                        h->fastlen[(code << shift) | j] = (uint8_t) l;
                    }
                }
            }
            h->maxcode[l] = code - 1;
        } else {
            h->maxcode[l] = -1;
        }
        code <<= 1;
    }
    h->maxcode[17] = 0x7FFFFFFF;
    h->present = 1;
}

static void fill(Bits* b)
{
    while (b->cnt <= 24) {
        uint32_t c = 0;
        if (b->p < b->end) {
            c = *b->p;
            if (b->stuffed && c == 0xFF) {
                uint8_t n = b->p + 1 < b->end ? b->p[1] : 0;
                if (n == 0x00) {
                    b->p += 2;
                } else {
                    c = 0; /* a marker: feed zeros, stay put */
                    b->overrun++;
                }
            } else {
                b->p++;
            }
        } else {
            b->overrun++;
        }
        b->buf |= c << (24 - b->cnt);
        b->cnt += 8;
    }
}

static inline int get_bits(Bits* b, int n)
{
    if (n == 0) {
        return 0;
    }
    if (b->cnt < n) {
        fill(b);
    }
    int v = (int) (b->buf >> (32 - n));
    b->buf <<= n;
    b->cnt -= n;
    return v;
}

static inline int extend(int v, int n)
{
    return v < (1 << (n - 1)) ? v - (1 << n) + 1 : v;
}

static int decode_sym(Bits* b, const Huff* h)
{
    if (b->cnt < 16) {
        fill(b);
    }
    int peek = (int) (b->buf >> 23);
    int k = h->fast[peek];
    if (k != 0) {
        int l = h->fastlen[peek];
        b->buf <<= l;
        b->cnt -= l;
        return h->vals[k - 1];
    }
    int code = 0;
    for (int l = 1; l <= 16; l++) {
        code = (code << 1) | (int) (b->buf >> 31);
        b->buf <<= 1;
        b->cnt--;
        if (h->maxcode[l] >= 0 && code <= h->maxcode[l]) {
            return h->vals[h->valptr[l] + code - h->mincode[l]];
        }
    }
    return -1;
}

static int decode_block(Bits* b, Comp* c, const Ctx* x, int16_t out[64])
{
    const Huff* dc = &x->dc[c->td];
    const Huff* ac = &x->ac[c->ta];
    const uint16_t* q = x->q[c->tq];
    memset(out, 0, 64 * sizeof(int16_t));
    int t = decode_sym(b, dc);
    if (t < 0 || t > 11) {
        return 1;
    }
    int diff = t ? extend(get_bits(b, t), t) : 0;
    c->pred += diff;
    out[0] = (int16_t) (c->pred * q[0]);
    for (int k = 1; k < 64;) {
        int rs = decode_sym(b, ac);
        if (rs < 0) {
            return 2;
        }
        int r = rs >> 4, s = rs & 15;
        if (s == 0) {
            if (r != 15) {
                break; /* end of block */
            }
            k += 16;
            continue;
        }
        k += r;
        if (k > 63) {
            return 3;
        }
        out[kZigzag[k]] = (int16_t) (extend(get_bits(b, s), s) * q[k]);
        k++;
    }
    return 0;
}

/* --- inverse DCT --------------------------------------------------------- */

#define F2F(x) ((int) ((x) * 4096 + 0.5))
#define FSH(x) ((x) * 4096)

#define IDCT_1D(s0, s1, s2, s3, s4, s5, s6, s7)                                                       \
    int t0, t1, t2, t3, p1, p2, p3, p4, p5, x0, x1, x2, x3;                                           \
    p2 = s2;                                                                                         \
    p3 = s6;                                                                                         \
    p1 = (p2 + p3) * F2F(0.5411961f);                                                                \
    t2 = p1 + p3 * F2F(-1.847759065f);                                                               \
    t3 = p1 + p2 * F2F(0.765366865f);                                                                \
    p2 = s0;                                                                                         \
    p3 = s4;                                                                                         \
    t0 = FSH(p2 + p3);                                                                               \
    t1 = FSH(p2 - p3);                                                                               \
    x0 = t0 + t3;                                                                                    \
    x3 = t0 - t3;                                                                                    \
    x1 = t1 + t2;                                                                                    \
    x2 = t1 - t2;                                                                                    \
    t0 = s7;                                                                                         \
    t1 = s5;                                                                                         \
    t2 = s3;                                                                                         \
    t3 = s1;                                                                                         \
    p3 = t0 + t2;                                                                                    \
    p4 = t1 + t3;                                                                                    \
    p1 = t0 + t3;                                                                                    \
    p2 = t1 + t2;                                                                                    \
    p5 = (p3 + p4) * F2F(1.175875602f);                                                              \
    t0 = t0 * F2F(0.298631336f);                                                                     \
    t1 = t1 * F2F(2.053119869f);                                                                     \
    t2 = t2 * F2F(3.072711026f);                                                                     \
    t3 = t3 * F2F(1.501321110f);                                                                     \
    p1 = p5 + p1 * F2F(-0.899976223f);                                                               \
    p2 = p5 + p2 * F2F(-2.562915447f);                                                               \
    p3 = p3 * F2F(-1.961570560f);                                                                    \
    p4 = p4 * F2F(-0.390180644f);                                                                    \
    t3 += p1 + p4;                                                                                   \
    t2 += p2 + p3;                                                                                   \
    t1 += p2 + p4;                                                                                   \
    t0 += p1 + p3;

static inline uint8_t clamp8(int x)
{
    return (uint8_t) (x < 0 ? 0 : x > 255 ? 255 : x);
}

static void idct_block(uint8_t* out, int stride, const int16_t* d)
{
    int val[64];
    int* v = val;
    for (int i = 0; i < 8; i++, d++, v++) {
        if (d[8] == 0 && d[16] == 0 && d[24] == 0 && d[32] == 0 && d[40] == 0 && d[48] == 0 && d[56] == 0) {
            int dc = d[0] * 4;
            v[0] = v[8] = v[16] = v[24] = v[32] = v[40] = v[48] = v[56] = dc;
        } else {
            IDCT_1D(d[0], d[8], d[16], d[24], d[32], d[40], d[48], d[56])
            x0 += 512;
            x1 += 512;
            x2 += 512;
            x3 += 512;
            v[0] = (x0 + t3) >> 10;
            v[56] = (x0 - t3) >> 10;
            v[8] = (x1 + t2) >> 10;
            v[48] = (x1 - t2) >> 10;
            v[16] = (x2 + t1) >> 10;
            v[40] = (x2 - t1) >> 10;
            v[24] = (x3 + t0) >> 10;
            v[32] = (x3 - t0) >> 10;
        }
    }
    v = val;
    for (int i = 0; i < 8; i++, v += 8, out += stride) {
        IDCT_1D(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7])
        x0 += 65536 + (128 << 17);
        x1 += 65536 + (128 << 17);
        x2 += 65536 + (128 << 17);
        x3 += 65536 + (128 << 17);
        out[0] = clamp8((x0 + t3) >> 17);
        out[7] = clamp8((x0 - t3) >> 17);
        out[1] = clamp8((x1 + t2) >> 17);
        out[6] = clamp8((x1 - t2) >> 17);
        out[2] = clamp8((x2 + t1) >> 17);
        out[5] = clamp8((x2 - t1) >> 17);
        out[3] = clamp8((x3 + t0) >> 17);
        out[4] = clamp8((x3 - t0) >> 17);
    }
}

/* --- markers ------------------------------------------------------------- */

static inline int be16(const uint8_t* p)
{
    return (p[0] << 8) | p[1];
}

/* Parse the headers up to and including SOS; returns the entropy data start
 * or NULL. */
static const uint8_t* parse_headers(Ctx* x, const uint8_t* p, const uint8_t* end)
{
    if (end - p < 2 || p[0] != 0xFF || p[1] != 0xD8) {
        return NULL;
    }
    p += 2;
    while (p + 4 <= end) {
        if (p[0] != 0xFF) {
            return NULL;
        }
        while (p < end && *p == 0xFF) {
            p++;
        }
        if (p >= end) {
            return NULL;
        }
        int m = *p++;
        if (m == 0xD8 || (m >= 0xD0 && m <= 0xD7)) {
            continue;
        }
        if (p + 2 > end) {
            return NULL;
        }
        int len = be16(p);
        const uint8_t* seg = p + 2;
        const uint8_t* next = p + len;
        if (len < 2 || next > end) {
            return NULL;
        }
        switch (m) {
        case 0xDB: /* DQT */
            while (seg < next) {
                int pq = seg[0] >> 4, tq = seg[0] & 3;
                seg++;
                for (int i = 0; i < 64; i++) {
                    x->q[tq][i] = pq ? (uint16_t) be16(seg + 2 * i) : seg[i];
                }
                seg += pq ? 128 : 64;
            }
            break;
        case 0xC4: /* DHT */
            while (seg < next) {
                int tc = seg[0] >> 4, th = seg[0] & 3;
                Huff* h = tc ? &x->ac[th] : &x->dc[th];
                int total = 0;
                seg++;
                h->bits[0] = 0;
                for (int i = 1; i <= 16; i++) {
                    h->bits[i] = seg[i - 1];
                    total += h->bits[i];
                }
                seg += 16;
                if (total > 256) {
                    return NULL;
                }
                memcpy(h->vals, seg, (size_t) total);
                seg += total;
                build_huff(h);
            }
            break;
        case 0xC0: /* SOF0 */
        case 0xC1:
            x->h = be16(seg + 1);
            x->w = be16(seg + 3);
            x->ncomp = seg[5];
            if (x->ncomp != 3) {
                return NULL;
            }
            for (int i = 0; i < 3; i++) {
                x->comp[i].id = seg[6 + 3 * i];
                x->comp[i].h = seg[7 + 3 * i] >> 4;
                x->comp[i].v = seg[7 + 3 * i] & 15;
                x->comp[i].tq = seg[8 + 3 * i] & 3;
            }
            break;
        case 0xDD: /* DRI */
            x->restart = be16(seg);
            break;
        case 0xDA: { /* SOS */
            int n = seg[0];
            for (int i = 0; i < n && i < 3; i++) {
                int id = seg[1 + 2 * i];
                for (int c = 0; c < 3; c++) {
                    if (x->comp[c].id == id) {
                        x->comp[c].td = seg[2 + 2 * i] >> 4;
                        x->comp[c].ta = seg[2 + 2 * i] & 3;
                    }
                }
            }
            return next;
        }
        default: /* APPn, COM and anything else: skip */
            break;
        }
        p = next;
    }
    return NULL;
}

int port_thp_frame_size(const uint8_t* data, size_t avail, int* w, int* h)
{
    Ctx x;
    memset(&x, 0, sizeof x);
    if (parse_headers(&x, data, data + avail) == NULL) {
        return 0;
    }
    *w = x.w;
    *h = x.h;
    return 1;
}

static int decode_scan(Ctx* x, const uint8_t* scan, const uint8_t* end, int stuffed, uint8_t* y, uint8_t* u,
                       uint8_t* v, int w, int h)
{
    Bits b = { scan, end, 0, 0, stuffed, 0 };
    int16_t blk[64];
    int mcux = (x->w + 15) / 16, mcuy = (x->h + 15) / 16;
    int todo = x->restart;
    for (int c = 0; c < 3; c++) {
        x->comp[c].pred = 0;
    }
    for (int my = 0; my < mcuy; my++) {
        for (int mx = 0; mx < mcux; mx++) {
            if (x->restart && todo == 0) {
                /* realign to a byte, then step over an RSTn if there is one */
                if (!stuffed) {
                    b.cnt -= b.cnt & 7;
                    b.p -= b.cnt / 8;
                    b.buf = 0;
                    b.cnt = 0;
                } else {
                    b.buf = 0;
                    b.cnt = 0;
                }
                if (b.p + 1 < b.end && b.p[0] == 0xFF && b.p[1] >= 0xD0 && b.p[1] <= 0xD7) {
                    b.p += 2;
                }
                for (int c = 0; c < 3; c++) {
                    x->comp[c].pred = 0;
                }
                todo = x->restart;
            }
            for (int k = 0; k < 4; k++) {
                if (decode_block(&b, &x->comp[0], x, blk) != 0) {
                    return 10;
                }
                int px = mx * 16 + (k & 1) * 8, py = my * 16 + (k >> 1) * 8;
                if (px + 8 <= w && py + 8 <= h) {
                    idct_block(y + py * w + px, w, blk);
                }
            }
            for (int c = 1; c < 3; c++) {
                if (decode_block(&b, &x->comp[c], x, blk) != 0) {
                    return 11;
                }
                int px = mx * 8, py = my * 8, cw = w / 2;
                if (px + 8 <= cw && py + 8 <= h / 2) {
                    idct_block((c == 1 ? u : v) + py * cw + px, cw, blk);
                }
            }
            if (x->restart) {
                todo--;
            }
        }
    }
    return b.overrun > 8 ? 12 : 0;
}

int port_thp_decode(const uint8_t* data, size_t avail, uint8_t* y, uint8_t* u, uint8_t* v, int w, int h)
{
    static Ctx x;
    memset(&x, 0, sizeof x);
    const uint8_t* end = data + avail;
    const uint8_t* scan = parse_headers(&x, data, end);
    if (scan == NULL) {
        return 1;
    }
    if (x.comp[0].h != 2 || x.comp[0].v != 2 || x.comp[1].h != 1 || x.comp[2].h != 1) {
        return 2; /* not 4:2:0 */
    }
    if (x.w != w || x.h != h) {
        return 3;
    }
    /* THP writes its entropy data unstuffed; fall back to standard JPEG
     * stuffing if a frame does not decode that way. */
    if (decode_scan(&x, scan, end, 0, y, u, v, w, h) == 0) {
        return 0;
    }
    return decode_scan(&x, scan, end, 1, y, u, v, w, h) == 0 ? 0 : 4;
}

void port_thp_tile_i8(const uint8_t* src, uint8_t* dst, int w, int h)
{
    int tiles_x = w / 8;
    for (int ty = 0; ty < h / 4; ty++) {
        for (int tx = 0; tx < tiles_x; tx++) {
            uint8_t* t = dst + ((size_t) ty * tiles_x + tx) * 32;
            for (int r = 0; r < 4; r++) {
                memcpy(t + r * 8, src + (size_t) (ty * 4 + r) * w + tx * 8, 8);
            }
        }
    }
}
