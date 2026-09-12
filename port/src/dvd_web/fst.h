/* GameCube disc header and file system table (FST) parsing.
 *
 * Disc header (big-endian): game ID at 0x000 (6 bytes), FST offset at 0x424,
 * FST size at 0x428. FST: 12-byte entries {u8 is_dir; u24 name_offset;
 * u32 file_offset | parent_index; u32 file_length | next_index}, the root
 * entry's "next" being the total entry count, followed by the string table. */
#ifndef PORT_DVD_FST_H
#define PORT_DVD_FST_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t fst_offset;
    uint32_t fst_size;
    char game_id[7];
} port_disc_header;

/* Parse the first 0x440 bytes of the disc. Returns 0, or -1 if the buffer is
 * too short or the game ID is not GALE01. */
int port_disc_parse_header(const uint8_t* sector0, size_t n, port_disc_header* out);

typedef struct port_fst port_fst;

/* Wrap an FST image. The bytes are referenced, not copied, and stay big-endian.
 * Returns NULL if the table is malformed. */
port_fst* port_fst_parse(const uint8_t* fst_bytes, uint32_t size);
void port_fst_free(port_fst* fst);

int32_t port_fst_entry_count(const port_fst* fst);
/* Case-insensitive lookup of "dir/sub/file.ext" (leading '/' allowed) from the
 * root. Returns the entry index or -1. */
int32_t port_fst_lookup(const port_fst* fst, const char* path);
int port_fst_is_dir(const port_fst* fst, int32_t entry);
uint32_t port_fst_file_offset(const port_fst* fst, int32_t entry);
uint32_t port_fst_file_length(const port_fst* fst, int32_t entry);
const char* port_fst_entry_name(const port_fst* fst, int32_t entry);

#endif
