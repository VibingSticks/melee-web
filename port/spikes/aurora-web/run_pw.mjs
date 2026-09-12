// Drive the spike page in headless Chrome with WebGPU, capture console + screenshot.
// Usage: node run_pw.mjs [url] [seconds]
import { createRequire } from 'node:module';
const require = createRequire('/home/ralsei/.npm/_npx/9833c18b2d85bc59/node_modules/');
const { chromium } = require('playwright-core');

const url = process.argv[2] ?? 'http://localhost:8765/simple.html';
const secs = Number(process.argv[3] ?? 6);
const headless = process.env.HEADED !== '1';
const extraArgs = (process.env.CHROME_ARGS ?? '').split(' ').filter(Boolean);
const browser = await chromium.launch({
  executablePath: '/opt/google/chrome/chrome',
  headless,
  args: ['--enable-unsafe-webgpu', '--ignore-gpu-blocklist', '--no-sandbox', ...extraArgs],
});
const page = await browser.newPage({ viewport: { width: 1280, height: 960 } });
const logs = []; const t0 = Date.now();
page.on('console', m => logs.push(`[${Date.now() - t0}ms][${m.type()}] ${m.text()}`));
page.on('pageerror', e => logs.push(`[pageerror] ${e.message}`));
page.on('crash', () => logs.push('[crash] page crashed'));
let gpu = null;
try {
  await page.goto(url, { waitUntil: 'domcontentloaded', timeout: 30000 });
  await page.waitForTimeout(secs * 1000);
  gpu = await page.evaluate(async () => {
    const a = navigator.gpu ? await navigator.gpu.requestAdapter() : null;
    const info = a ? a.info : null;
    return a ? { ok: true, vendor: info?.vendor, arch: info?.architecture, device: info?.device, desc: info?.description, immediates: navigator.gpu.wgslLanguageFeatures?.has('immediate_address_space') ?? null } : { ok: false };
  }).catch(e => ({ error: String(e) }));
  await page.screenshot({ path: 'screenshot-pw.png' });
} catch (e) {
  logs.push(`[runner] ${e.message.split('\n')[0]}`);
}
await browser.close().catch(() => {});
console.log('page WebGPU probe:', JSON.stringify(gpu));
console.log('--- console ---');
for (const l of logs) console.log(l.slice(0, 300));
