// A GameCube disc image backed by a File the user picked. Reads are ranged so
// the image is never loaded into memory as a whole.
//
// Ranged reads go through a block cache, because the cost of a read here is
// almost entirely per-call rather than per-byte: every File.slice().arrayBuffer()
// is a round trip to the browser's file backend, and the game does not read in
// big convenient runs. HSD relays ARAM-bound data in 16 KB chunks and reads
// headers a few dozen bytes at a time, so a scene load is thousands of calls.
// Uncached, the profiler showed a sub-kilobyte read costing 314 ms while a
// 3 MB bulk read cost 144 ms -- the small read was not moving less data more
// slowly, it was paying the same round trip for nothing.
//
// Disc data never changes, so a cache needs no invalidation. Blocks are large
// and aligned, which also turns the game's many small sequential reads into
// one read of the block that contains them all.

const BLOCK_SHIFT = 20;                     // 1 MiB blocks
const BLOCK_SIZE = 1 << BLOCK_SHIFT;
const CACHE_BYTES = 64 << 20;               // ~64 blocks kept, evicted oldest-used first

export class DiscSource {
  constructor(file) {
    this.file = file;
    this.size = file.size;
    /** @type {Map<number, Uint8Array>} block index -> bytes. Map keeps insertion
     * order, which is what makes the LRU below a re-insert and a shift. */
    this.blocks = new Map();
    this.cachedBytes = 0;
    this.stats = { calls: 0, hits: 0, misses: 0, bytesRead: 0 };
  }

  /** Fetch blocks [first, last] that are missing, coalescing a contiguous run
   * of misses into a single slice so a cold sequential read costs one call.
   *
   * Returns the bytes for every block in the range. The caller must assemble
   * its result from these references and NOT from the cache: `await` gives
   * other reads a turn, and a big request can push its own earlier blocks out
   * while its later ones are still being fetched, so a block present when the
   * fetch began need not still be resident when the copy runs. Holding the
   * references keeps the data alive whatever the cache does with it. */
  async #fill(first, last) {
    const parts = new Map();
    for (let b = first; b <= last; b++) {
      if (this.blocks.has(b)) parts.set(b, this.#touch(b));
    }
    let run = -1;
    for (let b = first; b <= last + 1; b++) {
      const missing = b <= last && !parts.has(b);
      if (missing && run < 0) {
        run = b;
      } else if (!missing && run >= 0) {
        const start = run * BLOCK_SIZE;
        const end = Math.min(b * BLOCK_SIZE, this.size);
        const buf = new Uint8Array(await this.file.slice(start, end).arrayBuffer());
        this.stats.misses += b - run;
        this.stats.bytesRead += buf.length;
        for (let i = run; i < b; i++) {
          const off = (i - run) * BLOCK_SIZE;
          if (off >= buf.length) break;
          const bytes = buf.subarray(off, Math.min(off + BLOCK_SIZE, buf.length));
          parts.set(i, bytes);
          this.#store(i, bytes);
        }
        run = -1;
      }
    }
    return parts;
  }

  #store(index, bytes) {
    // Two reads that miss the same block at the same time both fetch it (the
    // first fetch is still in flight when the second looks). The second store
    // replaces the first, so its bytes must come off the count, or every such
    // pair leaves a phantom block's worth in cachedBytes and the cache evicts
    // itself down to nothing over a long session.
    const old = this.blocks.get(index);
    if (old !== undefined) {
      this.cachedBytes -= old.length;
      this.blocks.delete(index);            // re-insert at the recently-used end
    }
    this.blocks.set(index, bytes);
    this.cachedBytes += bytes.length;
    while (this.cachedBytes > CACHE_BYTES && this.blocks.size > 1) {
      const oldest = this.blocks.keys().next().value;
      if (oldest === index) break;            // never evict what we just stored
      this.cachedBytes -= this.blocks.get(oldest).length;
      this.blocks.delete(oldest);
    }
  }

  #touch(index) {
    // Move to the back of the insertion order: that is the "recently used" end.
    const bytes = this.blocks.get(index);
    this.blocks.delete(index);
    this.blocks.set(index, bytes);
    return bytes;
  }

  async read(offset, length) {
    this.stats.calls++;
    if (length <= 0) return new ArrayBuffer(0);
    const end = Math.min(offset + length, this.size);
    if (offset >= this.size) return new ArrayBuffer(0);

    const first = offset >>> BLOCK_SHIFT;
    const last = (end - 1) >>> BLOCK_SHIFT;

    // A request larger than the cache would evict itself block by block as it
    // filled; serve those straight from the file and leave the cache alone.
    if ((last - first + 1) * BLOCK_SIZE > CACHE_BYTES) {
      this.stats.misses++;
      this.stats.bytesRead += end - offset;
      return this.file.slice(offset, end).arrayBuffer();
    }

    const parts = await this.#fill(first, last);

    const out = new Uint8Array(end - offset);
    let written = 0;
    for (let b = first; b <= last; b++) {
      const block = parts.get(b);
      if (block === undefined) {
        // `end` is clamped to the file size, so every block in the range holds
        // real bytes. Missing one means the fetch above did not deliver it,
        // and quietly leaving zeros here would corrupt an archive in a way
        // that surfaces much later as unexplained data.
        throw new Error(`disc read ${offset}+${length}: block ${b} was not fetched`);
      }
      const blockStart = b * BLOCK_SIZE;
      const from = Math.max(offset, blockStart) - blockStart;
      const to = Math.min(end, blockStart + block.length) - blockStart;
      if (to > from) {
        out.set(block.subarray(from, to), written);
        written += to - from;
      }
    }
    this.stats.hits++;
    return out.buffer;
  }

  // Returns { gameId, fstOffset, fstSize } or throws with a user-facing message.
  async validate() {
    if (this.size < 0x440) {
      throw new Error('This file is too small to be a GameCube disc image.');
    }
    const hdr = new Uint8Array(await this.read(0, 0x440));
    const gameId = String.fromCharCode(...hdr.subarray(0, 6));
    if (gameId !== 'GALE01') {
      throw new Error(`Expected a GALE01 (NTSC-U Super Smash Bros. Melee) disc, got "${gameId}".`);
    }
    const dv = new DataView(hdr.buffer, hdr.byteOffset, hdr.byteLength);
    const fstOffset = dv.getUint32(0x424);
    const fstSize = dv.getUint32(0x428);
    if (fstSize === 0 || fstOffset + fstSize > this.size) {
      throw new Error('The disc image has an invalid file table.');
    }
    return { gameId, fstOffset, fstSize };
  }
}
