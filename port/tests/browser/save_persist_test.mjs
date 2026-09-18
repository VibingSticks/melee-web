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

let failed = false;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed = true; };

try {
  // --- first boot: no stored card yet ---
  await page.goto(url, { waitUntil: 'domcontentloaded' });
  await page.setInputFiles('#disc', path.resolve(disc));
  await waitFor(t => t.includes('saves: IndexedDB mounted'), 30000, 'the IDBFS mount');
  check(true, 'first boot mounted IndexedDB');

  await waitFor(t => t.includes('saves: stored'), 90000, 'the card to be written back');
  check(true, 'first boot stored the card');

  const filesBefore = await page.evaluate(() => {
    try { return window.Module.FS.readdir('/saves').filter(n => n !== '.' && n !== '..'); } catch (e) { return ['<unreadable: ' + e + '>']; }
  });
  check(filesBefore.length > 0 && !String(filesBefore[0]).startsWith('<unreadable'),
    `card present in /saves before reload (${JSON.stringify(filesBefore)})`);

  // --- reload: same origin, so IndexedDB is still there ---
  logs = [];
  await page.reload({ waitUntil: 'domcontentloaded' });
  await page.setInputFiles('#disc', path.resolve(disc));
  await waitFor(t => t.includes('saves: IndexedDB mounted'), 30000, 'the mount after reload');

  const filesAfter = await page.evaluate(() => {
    try { return window.Module.FS.readdir('/saves').filter(n => n !== '.' && n !== '..'); } catch (e) { return ['<unreadable: ' + e + '>']; }
  });
  check(filesAfter.length > 0 && !String(filesAfter[0]).startsWith('<unreadable'),
    `card read back after reload (${JSON.stringify(filesAfter)})`);
  check(JSON.stringify(filesAfter) === JSON.stringify(filesBefore), 'same card files before and after');

  // Aurora says which of the two happened: it loads an existing image, or it
  // formats a new one. Only the first means the save survived.
  const loaded = await waitFor(t => t.includes('Loaded GC Card Image'), 30000, "Aurora's card load")
    .then(() => true).catch(() => false);
  check(loaded, 'Aurora loaded the stored card image rather than formatting a blank one');
} catch (e) {
  check(false, e.message);
} finally {
  await browser.close();
  server.kill();
}
console.log(failed ? '\nFAILED' : '\nPASSED');
process.exit(failed ? 1 : 0);
