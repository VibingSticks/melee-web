// Open the hosted build in Chrome, feed it a disc image, and print the console.
// Usage: node boot_probe.mjs <build-dir> <disc.iso> [seconds] [--headed] [--query=gpu=compat]
// Serves <build-dir> on a local port for the run.
import { createRequire } from 'node:module';
import { spawn } from 'node:child_process';
import path from 'node:path';
const require = createRequire('/home/ralsei/.npm/_npx/9833c18b2d85bc59/node_modules/');
const { chromium } = require('playwright-core');

const [buildDir, disc, secsArg] = process.argv.slice(2);
const secs = Number(secsArg ?? 8);
const headed = process.argv.includes('--headed');
const query = process.argv.find(a => a.startsWith('--query='))?.slice(8) ?? '';
const port = 8766;

const server = spawn('python3', ['-m', 'http.server', '-d', path.resolve(buildDir), String(port)], { stdio: 'ignore' });
await new Promise(r => setTimeout(r, 800));

const browser = await chromium.launch({
  executablePath: '/opt/google/chrome/chrome',
  headless: !headed,
  args: ['--enable-unsafe-webgpu', '--ignore-gpu-blocklist', '--no-sandbox', '--js-flags=--stack-trace-limit=100'],
});
const page = await browser.newPage({ viewport: { width: 1280, height: 960 } });
await page.addInitScript(() => { Error.stackTraceLimit = 64; });
const t0 = Date.now();
const logs = [];
page.on('console', m => logs.push(`[${Date.now() - t0}ms][${m.type()}] ${m.text()}`));
page.on('pageerror', e => logs.push(`[${Date.now() - t0}ms][pageerror] ${e.message}\n` + String(e.stack || '').split('\n').slice(0, 40).join('\n')));
try {
  await page.goto(`http://localhost:${port}/index.html${query ? '?' + query : ''}`, { waitUntil: 'domcontentloaded' });
  await page.setInputFiles('#disc', path.resolve(disc));
  await page.waitForTimeout(secs * 1000);
  await page.screenshot({ path: 'boot-probe.png' });
  logs.push('status: ' + await page.evaluate(() => document.getElementById('status').textContent));
} catch (e) {
  logs.push('[runner] ' + String(e.message).split('\n')[0]);
}
await browser.close().catch(() => {});
server.kill();
for (const l of logs) console.log(l.slice(0, 400));
