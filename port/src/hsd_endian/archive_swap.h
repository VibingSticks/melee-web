/* Big-endian HSD archive (.dat/.usd) fixup for little-endian hosts.
 *
 * Step 1 of spec D2: swap the header, relocation table and symbol tables, then
 * swap and relocate every pointer slot the relocation table names. After this
 * every pointer inside the archive is a valid native pointer and the header is
 * native; non-pointer scalars are still big-endian and are converted by the
 * schema walker (port_archive_swap_roots, plan Task 14).
 *
 * Compiled as part of the game library (uses the game's archive.h). */
#ifndef PORT_HSD_ENDIAN_ARCHIVE_SWAP_H
#define PORT_HSD_ENDIAN_ARCHIVE_SWAP_H

#include <stdint.h>

#include <sysdolphin/baselib/archive.h>

#include "schema.h"

typedef struct {
    uint32_t file_size, data_size, nb_reloc, nb_public, nb_extern;
} port_archive_hdr;

/* Convert `file` (file_size bytes) in place. Fills `hdr` with the native header
 * and returns a malloc'd sorted array of the relocation offsets (relative to
 * the data section) in *reloc_set_out / *reloc_count_out; the caller frees it.
 * Returns 0 when converted, 1 when the archive was already native (nothing
 * done, the reloc set is still produced), -1 when malformed. */
int port_archive_fixup(uint8_t* file, uint32_t file_size, port_archive_hdr* hdr, uint32_t** reloc_set_out,
                       uint32_t* reloc_count_out);

/* Step 2: walk every public root with its schema and swap scalar fields.
 * Implemented by plan Task 14; until then a no-op returning 0. */
int port_archive_swap_roots(HSD_Archive* archive, const uint32_t* reloc_set, uint32_t reloc_count);

/* The bytecode the walk reached but cannot describe (item scripts behind the
 * ItemStateDesc rows it visited): converted after the roots, with the pointer
 * words it leaves in place recorded in `ctx` as reached. port_archive_swap_roots
 * does this itself; a caller that drives port_walk directly (the coverage tool)
 * calls it before reading the visited set. */
struct port_walk_ctx_s;
void port_archive_convert_scripts(struct port_walk_ctx_s* ctx, const char* archive_name);

/* Replacement for the tail of HSD_ArchiveLocateExtern: the in-data chain of
 * reference sites is big-endian and not covered by the relocation table. */
uint32_t port_archive_read_be32(const void* p);

#endif
