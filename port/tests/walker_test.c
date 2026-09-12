#include "check.h"
#include "hsd_endian/walker.h"

/* Leaf: { u16 a; u16 b; float f; }  (8 bytes) */
static const port_field leaf_fields[] = { { F_U16, 0 }, { F_U16, 2 }, { F_F32, 4 } };
static const port_type Leaf_t = { "Leaf", 8, leaf_fields, 3 };

/* Root: { u32 n; Leaf* items; Leaf* shared; u32 flags:3,5,24; Leaf inl; u8 tag; u8 pad[3]; u32 dbl_hi_lo... }
 * layout: 0 n, 4 items, 8 shared, 12 flags(bits), 16 inl(8), 24 tag, 25 pad, 28 opaque u32, 32 ptrlist -> null-terminated list of Leaf* */
static const uint8_t root_bits[] = { 3, 5, 24 };
static const port_field ptr_fields[] = { { F_PTR, 0, &Leaf_t } };
static const port_type LeafPtr_t = { "Leaf*", 4, ptr_fields, 1 };
static const port_field root_fields[] = {
    { F_U32, 0 },
    { F_PTR_ARRAY, 4, &Leaf_t, LEN_FIELD_U32, 0 },
    { F_PTR, 8, &Leaf_t },
    { F_BITS, 12, NULL, LEN_CONST, 0, 4, root_bits, 3 },
    { F_STRUCT, 16, &Leaf_t },
    { F_U8, 24 },
    { F_OPAQUE, 28 },
    { F_PTR_ARRAY, 32, &LeafPtr_t, LEN_NULL_TERM, 0 },
};
static const port_type Root_t = { "Root", 36, root_fields, 8 };

static void be16(uint8_t* p, uint16_t v) { p[0] = (uint8_t) (v >> 8); p[1] = (uint8_t) v; }
static void be32(uint8_t* p, uint32_t v) { p[0] = (uint8_t) (v >> 24); p[1] = (uint8_t) (v >> 16); p[2] = (uint8_t) (v >> 8); p[3] = (uint8_t) v; }
static void native_ptr(uint8_t* p, const void* q) { uint32_t v = (uint32_t) (uintptr_t) q; memcpy(p, &v, 4); }

static void run(void)
{
    /* Archive data: Root at 0 (36 bytes, padded to 40), items[2] at 40..55, ptr list at 56: [&items[1], &items[0], NULL] */
    uint8_t buf[72];
    memset(buf, 0, sizeof buf);
    uint8_t* items = buf + 40;
    uint8_t* list = buf + 56;
    be32(buf + 0, 2);
    native_ptr(buf + 4, items);
    native_ptr(buf + 8, items + 8);                      /* shared -> items[1] */
    be32(buf + 12, (0x5u << 29) | (0x1Fu << 24) | 0x123456u); /* MSB-first: a=5, b=31, c=0x123456 */
    be16(buf + 16, 0x0A0B); be16(buf + 18, 0x0C0D); be32(buf + 20, 0x3F800000u); /* inline leaf */
    buf[24] = 0x7F;
    be32(buf + 28, 0xDEADBEEFu);                          /* opaque: must stay */
    native_ptr(buf + 32, list);
    be16(items + 0, 0x1234); be16(items + 2, 0xABCD); be32(items + 4, 0x3F800000u);
    be16(items + 8, 0x0001); be16(items + 10, 0x0002); be32(items + 12, 0x40000000u);
    native_ptr(list + 0, items + 8);
    native_ptr(list + 4, items + 0);
    native_ptr(list + 8, NULL);
    uint32_t relocs[] = { 4, 8, 32, 56, 60 };

    port_walk_ctx c;
    CHECK_EQ_U32(port_walk_ctx_init(&c, buf, sizeof buf, relocs, 5, 1), 0);
    CHECK_EQ_U32(port_walk(&c, &Root_t, buf), 0);
    CHECK(c.error == NULL);

    uint32_t n, flags, opaque;
    memcpy(&n, buf, 4);
    memcpy(&flags, buf + 12, 4);
    memcpy(&opaque, buf + 28, 4);
    CHECK_EQ_U32(n, 2);
    CHECK_EQ_U32(flags & 7u, 5);
    CHECK_EQ_U32((flags >> 3) & 31u, 31);
    CHECK_EQ_U32(flags >> 8, 0x123456u);
    CHECK_EQ_U32(opaque, 0xEFBEADDEu); /* bytes unchanged (read as native LE) */
    uint16_t a, b;
    float f;
    memcpy(&a, items, 2); memcpy(&b, items + 2, 2); memcpy(&f, items + 4, 4);
    CHECK_EQ_U32(a, 0x1234); CHECK_EQ_U32(b, 0xABCD); CHECK(f == 1.0f);
    memcpy(&a, items + 8, 2); memcpy(&b, items + 10, 2); memcpy(&f, items + 12, 4);
    CHECK_EQ_U32(a, 1); CHECK_EQ_U32(b, 2); CHECK(f == 2.0f); /* reached 3 ways, converted once */
    memcpy(&a, buf + 16, 2); memcpy(&f, buf + 20, 4);
    CHECK_EQ_U32(a, 0x0A0B); CHECK(f == 1.0f);
    CHECK_EQ_U32(buf[24], 0x7F);
    port_walk_ctx_free(&c);

    /* walking again must be a no-op for already-visited objects (fresh ctx would double-swap; same ctx does not) */
    CHECK_EQ_U32(port_walk_ctx_init(&c, buf, sizeof buf, relocs, 5, 1), 0);
    CHECK_EQ_U32(port_walk(&c, &Leaf_t, items), 0);
    CHECK_EQ_U32(port_walk(&c, &Leaf_t, items), 0);
    memcpy(&a, items, 2);
    CHECK_EQ_U32(a, 0x3412); /* swapped once more by the new context, not twice */
    port_walk_ctx_free(&c);

    /* strict mode: a scalar on a relocation slot, a pointer outside the archive */
    static const port_field bad_fields[] = { { F_U32, 4 } };
    static const port_type Bad_t = { "Bad", 36, bad_fields, 1 };
    CHECK_EQ_U32(port_walk_ctx_init(&c, buf, sizeof buf, relocs, 5, 1), 0);
    CHECK_EQ_U32(port_walk(&c, &Bad_t, buf), (uint32_t) -1);
    CHECK(c.error != NULL);
    port_walk_ctx_free(&c);
    uint8_t outside[8] = { 0 };
    native_ptr(buf + 8, outside);
    CHECK_EQ_U32(port_walk_ctx_init(&c, buf, sizeof buf, relocs, 5, 1), 0);
    CHECK_EQ_U32(port_walk(&c, &Root_t, buf), (uint32_t) -1);
    port_walk_ctx_free(&c);
    /* lenient mode reports but continues */
    CHECK_EQ_U32(port_walk_ctx_init(&c, buf, sizeof buf, relocs, 5, 0), 0);
    CHECK_EQ_U32(port_walk(&c, &Root_t, buf), 0);
    CHECK(c.error != NULL);
    port_walk_ctx_free(&c);

    /* unions: case picked by a (converted) discriminator */
    static const port_field ua_fields[] = { { F_U32, 0 } };
    static const port_type UA_t = { "UA", 4, ua_fields, 1 };
    static const port_field ub_fields[] = { { F_U16, 0 }, { F_U16, 2 } };
    static const port_type UB_t = { "UB", 4, ub_fields, 2 };
    static const uint32_t disc_vals[] = { 1, 2 };
    static const port_type* const disc_types[] = { &UA_t, &UB_t };
    static const port_field un_fields[] = { { F_U32, 0 }, { F_UNION, 4, NULL, LEN_CONST, 0, 0, NULL, 0, 0, disc_vals, disc_types, 2 } };
    static const port_type Un_t = { "Un", 8, un_fields, 2 };
    uint8_t u[8];
    be32(u, 2); be16(u + 4, 0x1122); be16(u + 6, 0x3344);
    CHECK_EQ_U32(port_walk_ctx_init(&c, u, sizeof u, NULL, 0, 1), 0);
    CHECK_EQ_U32(port_walk(&c, &Un_t, u), 0);
    memcpy(&a, u + 4, 2); memcpy(&b, u + 6, 2);
    CHECK_EQ_U32(a, 0x1122); CHECK_EQ_U32(b, 0x3344);
    port_walk_ctx_free(&c);
    be32(u, 9);
    CHECK_EQ_U32(port_walk_ctx_init(&c, u, sizeof u, NULL, 0, 1), 0);
    CHECK_EQ_U32(port_walk(&c, &Un_t, u), (uint32_t) -1); /* no such case */
    port_walk_ctx_free(&c);

    /* bitfield repacking: 16-bit unit with padding, and a full 8-bit unit */
    uint8_t w16[2];
    be16(w16, (0x3u << 14) | (0x2Au << 8) | 0x00FF); /* fields 2,6 then 8 unused bits */
    static const uint8_t w16_widths[] = { 2, 6 };
    port_repack_bits(w16, 2, w16_widths, 2);
    uint16_t v16; memcpy(&v16, w16, 2);
    CHECK_EQ_U32(v16 & 3u, 3);
    CHECK_EQ_U32((v16 >> 2) & 63u, 0x2A);
    CHECK_EQ_U32(v16 >> 8, 0xFF); /* padding bits preserved at the top */
    uint8_t w8 = 0xB4; /* 1011 0100: widths 1,3,4 -> 1, 0b011, 0b0100 */
    static const uint8_t w8_widths[] = { 1, 3, 4 };
    port_repack_bits(&w8, 1, w8_widths, 3);
    CHECK_EQ_U32(w8 & 1u, 1);
    CHECK_EQ_U32((w8 >> 1) & 7u, 3);
    CHECK_EQ_U32(w8 >> 4, 4);
}

TEST_MAIN(run)
