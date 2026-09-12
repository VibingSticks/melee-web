// A GameCube disc image backed by a File the user picked. Reads are ranged so
// the image is never loaded into memory as a whole.
export class DiscSource {
  constructor(file) {
    this.file = file;
    this.size = file.size;
  }

  async read(offset, length) {
    return this.file.slice(offset, offset + length).arrayBuffer();
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
    const dv = new DataView(hdr.buffer);
    const fstOffset = dv.getUint32(0x424);
    const fstSize = dv.getUint32(0x428);
    if (fstSize === 0 || fstOffset + fstSize > this.size) {
      throw new Error('The disc image has an invalid file table.');
    }
    return { gameId, fstOffset, fstSize };
  }
}
