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
    F_OPAQUE,    /* raw data (textures, display lists, vertex buffers): left big-endian */
    F_WORD,      /* 4-byte slot: a pointer when the relocation table names it (followed if
                    `type` is set), otherwise a u32/f32 scalar to swap */
    F_PTR_LIST   /* pointer to an inline list of `len` pointers whose targets differ by index:
                    element i is followed as disc_types[i] (NULL: left alone). For tables whose
                    rows share a struct but not its payload (each item kind's attribute block). */
} port_fkind;

typedef enum {
    LEN_CONST,     /* len = element count */
    LEN_FIELD_U32, /* len = byte offset in the parent of a u32 count (already converted) */
    LEN_FIELD_U16, /* ... u16 count */
    LEN_FIELD_U8,  /* ... u8 count */
    LEN_NULL_TERM, /* elements until one whose first pointer-sized word is 0 */
    LEN_TERM_VALUE, /* elements until one whose first (big-endian) word equals len; the terminator is
                       converted too so the game can compare it natively */
    LEN_RELOC_RUN,  /* elements while each one still looks like an element: every pointer field is
                       either null or a slot the relocation table named. Used for tables whose
                       length lives nowhere in the data (the effect descriptor tables). */
    LEN_OBJECT_RUN  /* elements until the start of the next object: the first offset past the array
                       that some relocated pointer in the archive targets. An element type that
                       holds pointers must also look like one (as LEN_RELOC_RUN). For arrays the
                       archive tool packed back to back with whatever it wrote next, and whose
                       length lives nowhere (an item's state table, its attribute block). */
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
    uint32_t disc_offset;               /* F_UNION: byte offset of the discriminator in the parent */
    const uint32_t* disc_values;        /* F_UNION: case values (compared after masking) */
    const port_type* const* disc_types; /* F_UNION: case types; F_PTR_LIST: per-index targets */
    uint8_t ncases;
    uint8_t disc_size;                  /* F_UNION: 1, 2 or 4 bytes (0 means 4) */
    uint8_t disc_be;                    /* F_UNION: discriminator is still big-endian (it lies inside the union) */
    uint32_t disc_mask;                 /* F_UNION: bits compared (0 means all) */
    const char* name;                   /* field name, for diagnostics (may be NULL) */
} port_field;

struct port_type {
    const char* name;
    uint32_t size;
    const port_field* fields;
    uint32_t nfields;
};

typedef struct {
    const char* prefix; /* public symbol prefix (may be NULL) ... */
    const char* suffix; /* ... and/or suffix; the longest matching prefix+suffix wins */
    const port_type* type;
} port_root;

extern const port_root port_roots[]; /* null-terminated; from schema_tables.c */

#endif
