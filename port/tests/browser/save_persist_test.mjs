// Does a memory card survive a page reload?
//
// The card lives on an IDBFS mount (port/src/save_web.c). IndexedDB is scoped
// to the origin and outlives a reload, so this boots the game once, waits for
// the card to be stored, reloads in the SAME browser context, and checks that
// Aurora finds the stored image instead of formatting a blank one.
//
// Usage: node save_persist_test.mjs <build-dir> <disc.iso>
import { createRequire } from 'node:module';
import { spawn } from 'node:child_process';
import path from 'node:path';
// Same resolution the boot probe uses: playwright lives in the npx cache.
const require = createRequire('/home/ralsei/.npm/_npx/9833c18b2d85bc59/node_modules/');
const { chromium } = require('playwright-core');

const [buildDir, disc] = process.argv.slice(2);
if (!buildDir || !disc) {
  console.error('usage: save_persist_test.mjs <build-dir> <disc.iso>');
  process.exit(2);
}
const port = 8791;
const url = `http://localhost:${port}/index.html?renderer=webgl2&res=640x480`;
const server = spawn('python3', ['-m', 'http.server', '-d', path.resolve(buildDir), String(port)], { stdio: 'ignore' });
await new Promise(r => setTimeout(r, 800));

// Same browser and flags as the boot probe.
const browser = await chromium.launch({
  executablePath: '/opt/google/chrome/chrome',
  args: ['--enable-unsafe-webgpu', '--ignore-gpu-blocklist', '--no-sandbox', '--enable-unsafe-swiftshader'],
});
const context = await browser.newContext();      // one context: IndexedDB persists across reloads
const page = await context.newPage();
let logs = [];
page.on('console', m => logs.push(m.text()));

const waitFor = async (pred, ms, what) => {
  const until = Date.now() + ms;
  while (Date.now() < until) {
    if (logs.some(pred)) return true;
    await new Promise(r => setTimeout(r, 250));
  }
  throw new Error(`timed out waiting for ${what}`);
};

let importLogs = [];
let failed = false;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed = true; };

// A recognisable payload standing in for a .gci the game would write. Using
// our own file rather than a real save keeps the test independent of how far
// the game has to be driven to save, while still proving that byte-for-byte
// content survives a reload -- which is the thing that actually matters.
const PAYLOAD = Array.from({ length: 512 }, (_, i) => (i * 7 + 3) & 0xff);
const GCI_NAME = 'persist-probe.gci';

const readSaveTree = (p) => p.evaluate(() => {
  const FS = window.Module.FS, out = [];
  const walk = (d) => {
    for (const n of FS.readdir(d)) {
      if (n === '.' || n === '..') continue;
      const full = `${d}/${n}`;
      if (FS.isDir(FS.stat(full).mode)) walk(full); else out.push(full);
    }
  };
  try { walk('/saves'); } catch (e) { out.push('<unreadable: ' + e + '>'); }
  return out.sort();
});

try {
  // --- first boot: write a file into the card folder ---
  await page.goto(url, { waitUntil: 'domcontentloaded' });
  await page.setInputFiles('#disc', path.resolve(disc));
  await waitFor(t => t.includes('saves: IndexedDB mounted'), 30000, 'the IDBFS mount');
  check(true, 'mounted IndexedDB');

  await waitFor(t => t.includes('saves: stored'), 90000, 'the first write-back');

  const wrote = await page.evaluate(({ payload, name }) => {
    const FS = window.Module.FS;
    let dir = '/saves';
    for (;;) {  // descend to the folder Aurora keeps the card in
      const sub = FS.readdir(dir).filter(n => n !== '.' && n !== '..')
        .map(n => `${dir}/${n}`).filter(f => FS.isDir(FS.stat(f).mode));
      if (sub.length === 0) break;
      dir = sub[0];
    }
    FS.writeFile(`${dir}/${name}`, new Uint8Array(payload));
    return `${dir}/${name}`;
  }, { payload: PAYLOAD, name: GCI_NAME });
  check(wrote.includes(GCI_NAME), `wrote a save file at ${wrote}`);

  // The write-back is debounced, so wait for the store that follows it.
  logs = [];
  await waitFor(t => t.includes('saves: stored'), 60000, 'the write-back after the save');
  check(true, 'card stored after the write');

  const before = await readSaveTree(page);

  // --- reload: same origin, so IndexedDB is still there ---
  logs = [];
  await page.reload({ waitUntil: 'domcontentloaded' });
  await page.setInputFiles('#disc', path.resolve(disc));
  await waitFor(t => t.includes('saves: IndexedDB mounted'), 30000, 'the mount after reload');

  const after = await readSaveTree(page);
  check(JSON.stringify(after) === JSON.stringify(before), `same files after reload (${JSON.stringify(after)})`);

  const roundTripped = await page.evaluate(({ payload, name }) => {
    const FS = window.Module.FS;
    const hits = [];
    const walk = (d) => {
      for (const n of FS.readdir(d)) {
        if (n === '.' || n === '..') continue;
        const full = `${d}/${n}`;
        if (FS.isDir(FS.stat(full).mode)) walk(full); else if (n === name) hits.push(full);
      }
    };
    walk('/saves');
    if (hits.length !== 1) return `expected one ${name}, found ${hits.length}`;
    const got = FS.readFile(hits[0]);
    if (got.length !== payload.length) return `length ${got.length}, expected ${payload.length}`;
    for (let i = 0; i < payload.length; i++) {
      if (got[i] !== payload[i]) return `byte ${i} is ${got[i]}, expected ${payload[i]}`;
    }
    return 'ok';
  }, { payload: PAYLOAD, name: GCI_NAME });
  check(roundTripped === 'ok', `save content identical after reload (${roundTripped})`);

  // --- export ---
  const dl = page.waitForEvent('download', { timeout: 20000 });
  await page.click('#export');
  const download = await dl;
  // Save it under the name the page chose. The raw download path is a UUID
  // with no extension, and importing that would exercise a filename the
  // player would never actually have.
  const { readFileSync, mkdtempSync } = await import('node:fs');
  const os = await import('node:os');
  const exported = path.join(mkdtempSync(path.join(os.tmpdir(), 'melee-save-')), download.suggestedFilename());
  await download.saveAs(exported);
  const bytes = readFileSync(exported);
  check(bytes.length > 0, `exported ${download.suggestedFilename()} (${bytes.length} bytes)`);

  // --- import: wipe the card, put the export back, and check it returns ---
  await page.evaluate(({ name }) => {
    const FS = window.Module.FS;
    const walk = (d) => {
      for (const n of FS.readdir(d)) {
        if (n === '.' || n === '..') continue;
        const full = `${d}/${n}`;
        if (FS.isDir(FS.stat(full).mode)) walk(full); else if (n === name) FS.unlink(full);
      }
    };
    walk('/saves');
  }, { name: GCI_NAME });
  const gone = await readSaveTree(page);
  check(!gone.some(f => f.includes(GCI_NAME)), 'save removed before the import');

  logs = [];
  // Importing stores the file and then reloads the page; the mount only
  // happens once a disc is supplied again, so wait for the navigation first.
  const reloaded = page.waitForNavigation({ waitUntil: 'domcontentloaded', timeout: 30000 });
  await page.setInputFiles('#import', exported);
  await reloaded;
  check(true, 'import reloaded the page');
  importLogs = logs.slice();
  logs = [];
  await page.setInputFiles('#disc', path.resolve(disc));
  await waitFor(t => t.includes('saves: IndexedDB mounted'), 60000, 'the mount after the import reload');

  const restored = await page.evaluate(({ payload, name }) => {
    const FS = window.Module.FS;
    const hits = [];
    const walk = (d) => {
      for (const n of FS.readdir(d)) {
        if (n === '.' || n === '..') continue;
        const full = `${d}/${n}`;
        if (FS.isDir(FS.stat(full).mode)) walk(full); else if (n.includes('persist-probe') && n.endsWith('.gci')) hits.push(full);
      }
    };
    walk('/saves');
    if (hits.length === 0) return 'the imported file is not there';
    const got = FS.readFile(hits[0]);
    for (let i = 0; i < payload.length; i++) {
      if (got[i] !== payload[i]) return `byte ${i} differs after import`;
    }
    return 'ok';
  }, { payload: PAYLOAD, name: GCI_NAME });
  check(restored === 'ok', `imported save matches the export (${restored})`);
} catch (e) {
  check(false, e.message);
} finally {
  await browser.close();
  server.kill();
}
if (failed) {
  console.log('\n--- import log ---');
  console.log(importLogs.filter(t => t.includes('saves') || t.includes('melee')).join('\n') || '(nothing from the import)');
  console.log('\n--- page log (last 40 lines) ---');
  console.log(logs.slice(-40).join('\n'));
}
console.log(failed ? '\nFAILED' : '\nPASSED');
process.exit(failed ? 1 : 0);
