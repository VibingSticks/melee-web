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

/* Step 2: walk every public root with its schema and swap scalar fields. The
 * archive's file name comes from HSD_Archive::name when set, else from the DVD
 * layer's record of the buffer (port_disc_file_at). */
/* What a converting parse keeps of its walk when the game registers objects
 * out of the archive later: the fighters' item Articles hang off ftData x48, a
 * pointer list with no count that the schema cannot follow, and each fighter's
 * load code hands them to the item tables one by one (it_8026B3F8). Keeping the
 * walk's context lets those be converted then with the same visited and
 * converted-byte bookkeeping, so an object the roots already reached is not
 * swapped twice. Takes ownership of `reloc_set`. Returns the kept context (only
 * for archives with an ftData root) or NULL; hand it to port_archive_note_parse. */
typedef struct port_archive_keep_s port_archive_keep;
port_archive_keep* port_archive_swap_roots(HSD_Archive* archive, uint32_t* reloc_set, uint32_t reloc_count);

/* Convert `obj` of `type`, and what it reaches, inside the parsed archive that
 * holds it, using that archive's kept context (see port_archive_swap_roots);
 * item scripts behind rows first reached now are converted too. Returns 0, or
 * -1 when the object is in no archive with a kept context (nothing done). */
int port_archive_swap_object(void* obj, const port_type* type, const char* what);

/* The root rule for public symbol `sym` in the archive named `archive` (a
 * file name such as "GrCs.dat", or NULL when unknown): the longest
 * prefix+suffix match, an archive-scoped rule beating any unscoped one. NULL
 * when nothing matches. */
const port_root* port_archive_find_root(const char* sym, const char* archive);

/* Records whether the parse of the archive whose data starts at `data`
 * converted it (`fresh`), or found it native already (a buffer the preload
 * cache handed back). port_archive_take_fresh answers for a pointer into some
 * archive's data: 1 the first time it is asked after a converting parse, 0
 * after that (the same parsed archive handed back by the cache), so a table
 * the game converts itself on its load path is converted exactly once. */
void port_archive_note_parse(const void* data, uint32_t size, int fresh, port_archive_keep* keep);
int port_archive_take_fresh(const void* p);

/* The bytecode the walk reached but cannot describe (item scripts behind the
 * ItemStateDesc rows it visited): converted after the roots, with the pointer
 * words it leaves in place recorded in `ctx` as reached. port_archive_swap_roots
 * does this itself; a caller that drives port_walk directly (the coverage tool)
 * calls it before reading the visited set. */
struct port_walk_ctx_s;
void port_archive_convert_scripts(struct port_walk_ctx_s* ctx, const HSD_ArchivePublicInfo* pub,
                                  uint32_t nb_public, const char* syms, const char* archive_name);

/* Replacement for the tail of HSD_ArchiveLocateExtern: the in-data chain of
 * reference sites is big-endian and not covered by the relocation table. */
uint32_t port_archive_read_be32(const void* p);

#endif
