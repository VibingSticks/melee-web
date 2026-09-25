/* Graph walker: converts the scalar fields of an object graph inside one
 * archive from big-endian to native, guided by port_type descriptors.
 *
 * Rules:
 *  - every (address, type) pair is visited once, so shared sub-objects are
 *    walked exactly once, and every byte is swapped at most once even when two
 *    descriptors reach it by different paths;
 *  - pointer slots (those in the relocation set) are never swapped here, they
 *    were relocated by port_archive_fixup; in strict mode a scalar descriptor
 *    landing on a relocation slot is an error;
 *  - pointers are followed only when they point inside the archive data;
 *  - F_OPAQUE fields are skipped;
 *  - LEN_OBJECT_RUN arrays end where the next object begins: one a relocated pointer
 *    targets, or a root symbol names (port_walk_ctx_set_object_starts);
 *  - F_BITS repacks a storage unit from MSB-first to LSB-first field order. */
#ifndef PORT_HSD_ENDIAN_WALKER_H
#define PORT_HSD_ENDIAN_WALKER_H

#include <stdint.h>

#include "schema.h"

typedef struct port_walk_ctx_s {
    const uint8_t* base;
    uint32_t size;
    const uint32_t* reloc_set; /* sorted offsets relative to base */
    uint32_t reloc_count;
    int strict;
    void* visited;
    uint8_t* converted; /* one bit per byte of `base`: scalars are swapped once */
    const char* error; /* description of the first strict-mode violation */
    uint32_t* targets;           /* sorted offsets the relocated pointers point at (built on first use) */
    uint32_t ntargets;
    uint32_t* object_starts;     /* more object starts: the root symbols (port_walk_ctx_set_object_starts) */
    uint32_t n_object_starts;
    const port_type* cur_type;   /* diagnostics: where the walker is */
    const port_field* cur_field;
    unsigned nlogged;            /* violations logged so far (capped) */
    int fill_gaps;               /* a scalar on a pointer slot is skipped, not a violation: a pass that
                                    sweeps a block for words no typed walk converted */
    uint32_t* externs;           /* sorted offsets of extern reference slots (port_walk_ctx_set_externs) */
    uint32_t nexterns;
} port_walk_ctx;

int port_walk_ctx_init(port_walk_ctx* ctx, const uint8_t* base, uint32_t size, const uint32_t* reloc_set,
                       uint32_t reloc_count, int strict);
void port_walk_ctx_free(port_walk_ctx* ctx);

/* Names more offsets where objects begin: the archive's public (root) symbols,
 * which no relocated pointer need point at. An object run (LEN_OBJECT_RUN)
 * ends at one of them as it does at a pointer target -- seven stage archives
 * pack map_head right after the table it sizes this way. Copies `offsets`;
 * call before the first walk. Returns -1 when out of memory. */
int port_walk_ctx_set_object_starts(port_walk_ctx* ctx, const uint32_t* offsets, uint32_t n);

/* Names the archive's extern reference slots: words that hold, big-endian,
 * the offset of the next slot referring to the same external symbol, until
 * HSD_ArchiveLocateExtern walks the chain at load and writes the resolved
 * address (NULL for most) into each. They are pointer slots the relocation
 * table does not list, and they must stay as the loader expects: the walk
 * never converts them, and a type whose pointer lands on one still matches.
 * Copies `offsets`; call before the first walk. Returns -1 when out of memory. */
int port_walk_ctx_set_externs(port_walk_ctx* ctx, const uint32_t* offsets, uint32_t n);

/* Convert `obj` (of type `type`) and everything reachable from it.
 * Returns 0, or -1 on a strict-mode violation (ctx->error says which). */
int port_walk(port_walk_ctx* ctx, const port_type* type, void* obj);

/* Records a 4-byte relocated pointer slot that a converter outside the walk
 * (a bytecode pass) resolved and left in place, so the visited set -- and a
 * coverage walk over it -- counts the slot as reached. */
int port_walk_mark_slot(port_walk_ctx* ctx, const void* slot);

/* Coverage: call `fn` for every (object, type) the walk visited. */
void port_walk_visited_foreach(const port_walk_ctx* ctx, void (*fn)(void* user, const uint8_t* obj, const port_type* type),
                               void* user);

/* Exposed for tests: repack one bitfield storage unit in place. */
void port_repack_bits(uint8_t* unit, uint8_t storage_bytes, const uint8_t* widths, uint8_t nwidths);

#endif
