/* Type descriptors that drive the big-endian -> little-endian conversion of
 * game data (spec D2). Tables are generated from the game's headers by
 * port/tools/gen_schema.py; walker.c interprets them. */
#ifndef PORT_HSD_ENDIAN_SCHEMA_H
#define PORT_HSD_ENDIAN_SCHEMA_H

#include <stdint.h>

typedef enum {
    F_U8,        /* 1-byte scalar: nothing to do (also used for padding) */
    F_U16,       /* 2-byte scalar */
    F_U32,       /* 4-byte scalar */
    F_U64,       /* 8-byte scalar */
    F_F32,       /* float */
    F_F64,       /* double */
    F_PTR,       /* pointer, already relocated; followed if `type` is set */
    F_STRUCT,    /* inline struct of `type` */
    F_ARRAY,     /* inline array of `type`, length per len_kind/len */
    F_PTR_ARRAY, /* pointer to an array of `type`, length per len_kind/len */
    F_BITS,      /* a bitfield storage unit: repacked MSB-first -> LSB-first */
    F_UNION,     /* discriminated union: case chosen by a sibling field */
    F_OPAQUE     /* raw data (textures, display lists, vertex buffers): left big-endian */
} port_fkind;

typedef enum {
    LEN_CONST,     /* len = element count */
    LEN_FIELD_U32, /* len = byte offset in the parent of a u32 count (already converted) */
    LEN_FIELD_U16, /* ... u16 count */
    LEN_FIELD_U8,  /* ... u8 count */
    LEN_NULL_TERM  /* elements until one whose first pointer-sized word is 0 */
} port_lenkind;

typedef struct port_type port_type;

typedef struct {
    port_fkind kind;
    uint32_t offset;                    /* byte offset in the parent */
    const port_type* type;              /* element / target / inline type */
    port_lenkind len_kind;
    uint32_t len;
    uint8_t bits_storage;               /* F_BITS: 1, 2 or 4 bytes */
    const uint8_t* bit_widths;          /* F_BITS: declared widths in declaration order */
    uint8_t nbits;                      /* F_BITS: number of widths */
    uint32_t disc_offset;               /* F_UNION: byte offset of the discriminator (u32) in the parent */
    const uint32_t* disc_values;        /* F_UNION: case values */
    const port_type* const* disc_types; /* F_UNION: case types */
    uint8_t ncases;
} port_field;

struct port_type {
    const char* name;
    uint32_t size;
    const port_field* fields;
    uint32_t nfields;
};

typedef struct {
    const char* prefix; /* public symbol prefix, longest match wins */
    const port_type* type;
} port_root;

extern const port_root port_roots[]; /* null-terminated; from schema_tables.c */

#endif
