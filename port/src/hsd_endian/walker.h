/* Graph walker: converts the scalar fields of an object graph inside one
 * archive from big-endian to native, guided by port_type descriptors.
 *
 * Rules:
 *  - every (address, type) pair is visited once, so shared sub-objects are
 *    converted exactly once;
 *  - pointer slots (those in the relocation set) are never swapped here, they
 *    were relocated by port_archive_fixup; in strict mode a scalar descriptor
 *    landing on a relocation slot is an error;
 *  - pointers are followed only when they point inside the archive data;
 *  - F_OPAQUE fields are skipped;
 *  - F_BITS repacks a storage unit from MSB-first to LSB-first field order. */
#ifndef PORT_HSD_ENDIAN_WALKER_H
#define PORT_HSD_ENDIAN_WALKER_H

#include <stdint.h>

#include "schema.h"

typedef struct {
    const uint8_t* base;
    uint32_t size;
    const uint32_t* reloc_set; /* sorted offsets relative to base */
    uint32_t reloc_count;
    int strict;
    void* visited;
    const char* error; /* description of the first strict-mode violation */
    const port_type* cur_type;   /* diagnostics: where the walker is */
    const port_field* cur_field;
    unsigned nlogged;            /* violations logged so far (capped) */
} port_walk_ctx;

int port_walk_ctx_init(port_walk_ctx* ctx, const uint8_t* base, uint32_t size, const uint32_t* reloc_set,
                       uint32_t reloc_count, int strict);
void port_walk_ctx_free(port_walk_ctx* ctx);

/* Convert `obj` (of type `type`) and everything reachable from it.
 * Returns 0, or -1 on a strict-mode violation (ctx->error says which). */
int port_walk(port_walk_ctx* ctx, const port_type* type, void* obj);

/* Exposed for tests: repack one bitfield storage unit in place. */
void port_repack_bits(uint8_t* unit, uint8_t storage_bytes, const uint8_t* widths, uint8_t nwidths);

#endif
