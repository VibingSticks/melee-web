// Open index.html from disk (file://) in a browser and print the probe results.
// Usage: node run_pw.mjs [chrome|firefox] [--headed]
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
const require = createRequire('/home/ralsei/.npm/_npx/9833c18b2d85bc59/node_modules/');
const pw = require('playwright-core');

const which = process.argv[2] ?? 'chrome';
const headed = process.argv.includes('--headed');
const page_path = path.join(path.dirname(fileURLToPath(import.meta.url)), 'index.html');
const url = 'file://' + page_path;

let browser;
if (which === 'firefox') {
  browser = await pw.firefox.launch({ headless: !headed });
} else {
  browser = await pw.chromium.launch({
    executablePath: '/opt/google/chrome/chrome',
    headless: !headed,
    args: ['--enable-unsafe-webgpu', '--no-sandbox'],
  });
}
const page = await browser.newPage();
const errors = [];
page.on('pageerror', e => errors.push(e.message));
await page.goto(url);
await page.waitForFunction(() => document.title === 'S3 done', null, { timeout: 15000 }).catch(() => {});
if (await page.evaluate(() => typeof window.showOpenFilePicker === 'function')) {
  await page.click('#fsa').catch(() => {});
  await page.waitForFunction(() => 'showOpenFilePicker' in window.__results, null, { timeout: 5000 }).catch(() => {});
}
const results = await page.evaluate(() => window.__results);
const ua = await page.evaluate(() => navigator.userAgent);
await browser.close();
console.log(`== ${which} (${headed ? 'headed' : 'headless'}) ==`);
console.log(ua);
for (const [k, v] of Object.entries(results ?? {})) console.log(`${k}: ${v}`);
if (errors.length) console.log('page errors:', errors.join(' | '));
