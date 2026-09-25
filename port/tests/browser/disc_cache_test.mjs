// Does the block cache return exactly what the file holds, for every shape of
// read the game makes? A wrong byte here corrupts an archive silently.
//
// The file is generated rather than allocated, so the tests can exceed the
// 64 MiB cache -- which is where the interesting bug lived: a read big enough
// to evict its own earlier blocks while fetching its later ones used to
// assemble from the cache afterwards and silently return zeros for whatever
// had been pushed out.
import { DiscSource } from '/home/ralsei/projects/melee/port/web/js/disc_source.js';

const SIZE = 300 * 1024 * 1024 + 12345;        // not a block multiple, on purpose
const byteAt = (i) => (i * 31 + (i >> 11)) & 0xff;

let sliceCalls = 0, bytesServed = 0;
const fakeFile = {
  size: SIZE,
  slice(start, end) {
    sliceCalls++;
    const s = Math.max(0, Math.min(start, SIZE)), e = Math.max(s, Math.min(end, SIZE));
    return { arrayBuffer: async () => {
      const b = new Uint8Array(e - s);
      for (let i = 0; i < b.length; i++) b[i] = byteAt(s + i);
      bytesServed += b.length;
      return b.buffer;
    } };
  },
};

const src = new DiscSource(fakeFile);
let fail = 0;
const check = (ok, msg) => { if (!ok) { console.log('FAIL ' + msg); fail++; } };

async function expect(off, len, label) {
  const got = new Uint8Array(await src.read(off, len));
  const wantLen = Math.max(0, Math.min(off + len, SIZE) - Math.min(off, SIZE));
  check(got.length === wantLen, `${label}: length ${got.length} != ${wantLen}`);
  for (let i = 0; i < got.length; i++) {
    if (got[i] !== byteAt(off + i)) {
      check(false, `${label}: byte ${i} = ${got[i]} != ${byteAt(off + i)}`);
      return;
    }
  }
}

await expect(0, 0x440, 'header');
await expect(0x440, 32, 'small read after header');
await expect(1024 * 1024 - 8, 16, 'straddling a block boundary');
await expect(SIZE - 10, 10, 'final bytes');
await expect(SIZE - 10, 500, 'past the end is clamped');
await expect(SIZE + 100, 16, 'entirely past the end');
await expect(3 * 1024 * 1024 + 7, 2 * 1024 * 1024 + 3, 'multi-block unaligned');

// The regression: a read spanning more blocks than fit comfortably, which
// previously evicted its own early blocks before assembling them.
await expect(10 * 1024 * 1024, 60 * 1024 * 1024 + 7, '60 MiB read (self-eviction)');
await expect(100 * 1024 * 1024, 63 * 1024 * 1024, '63 MiB read at another offset');

// Concurrent reads over overlapping ranges, interleaved by the event loop.
await Promise.all([
  expect(200 * 1024 * 1024, 20 * 1024 * 1024, 'concurrent A'),
  expect(205 * 1024 * 1024, 20 * 1024 * 1024, 'concurrent B'),
  expect(150 * 1024 * 1024, 40 * 1024 * 1024, 'concurrent C (evicts A/B)'),
  expect(0x440, 64, 'concurrent D (tiny)'),
]);

for (let i = 0; i < 120; i++) {
  const off = Math.floor(Math.random() * SIZE);
  const len = Math.floor(Math.random() * 200000) + 1;
  await expect(off, len, `random ${i} (off=${off} len=${len})`);
}

const before = sliceCalls;
for (let i = 0; i < 64; i++) await src.read(250 * 1024 * 1024 + i * 16384, 16384);
const calls = sliceCalls - before;
console.log(`64 sequential 16 KB reads over 1 MiB -> ${calls} file call(s)`);
check(calls <= 2, `expected coalescing, took ${calls} calls`);

// A prefetched file is one slice, and reads of it -- even ones that start
// while the prefetch is still in flight -- add none.
{
  const base = 100 * 1024 * 1024 + 12345; // not read by the checks above
  const len = 3 * 1024 * 1024;
  const s0 = sliceCalls;
  src.prefetch(base, len);
  await Promise.all([expect(base, 16384, 'during prefetch A'), expect(base + len - 100, 100, 'during prefetch B')]);
  for (let i = 0; i < 40; i++) await expect(base + i * 65536, 65536, `after prefetch ${i}`);
  const used = sliceCalls - s0;
  console.log(`prefetch of 3 MB + 42 reads of it -> ${used} file call(s)`);
  check(used === 1, `prefetch should cost one slice, took ${used}`);
}

console.log(`cachedBytes=${(src.cachedBytes / 1048576).toFixed(1)} MiB (cap 64), slices=${sliceCalls}`);
check(src.cachedBytes <= 64 * 1024 * 1024, `cache exceeded its cap: ${src.cachedBytes}`);
console.log(fail ? `\n${fail} FAILURES` : '\nall disc cache checks passed');
process.exit(fail ? 1 : 0);
