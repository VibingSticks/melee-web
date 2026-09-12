// Unit tests for the WebGL2 WebGPU polyfill (port/web/js/gpu-gl2.js). The polyfill is
// force-installed so the tests exercise it even where real WebGPU exists.
// Usage: node gl2_polyfill_test.mjs [--browser=chromium|firefox] [--headed]
// Chromium runs headless on SwiftShader (only WebGL2 is needed); Firefox uses the real GPU
// and is the browser that will actually take this path, having no WebGPU on Linux.
import { createRequire } from 'node:module';
import { spawn } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const require = createRequire('/home/ralsei/.npm/_npx/9833c18b2d85bc59/node_modules/');
const playwright = require('playwright-core');

const browserName = process.argv.find(a => a.startsWith('--browser='))?.slice(10) ?? 'chromium';
if (!['chromium', 'firefox'].includes(browserName)) throw new Error(`unknown browser ${browserName}`);
const headless = !process.argv.includes('--headed');

const portDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const port = 8767;
const server = spawn('python3', ['-m', 'http.server', '-d', portDir, String(port)], { stdio: 'ignore' });
await new Promise(r => setTimeout(r, 800));

const launch = browserName === 'chromium'
  ? { executablePath: '/opt/google/chrome/chrome', headless, args: ['--no-sandbox', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] }
  : { headless };
const browser = await playwright[browserName].launch(launch);
const page = await browser.newPage();
const logs = [];
page.on('console', m => logs.push(`[${m.type()}] ${m.text()}`));
page.on('pageerror', e => logs.push(`[pageerror] ${e.message}`));
await page.goto(`http://localhost:${port}/tests/browser/gl2_polyfill_page.html`, { waitUntil: 'domcontentloaded' });
const results = await page.evaluate(() => window.__runTests(), null).catch(e => [{ name: 'harness', ok: false, msg: String(e) }]);
await browser.close().catch(() => {});
server.kill();

let failed = 0;
for (const r of results) {
  if (r.ok) console.log(`ok   ${r.name}`);
  else { failed++; console.log(`FAIL ${r.name}: ${r.msg}`); }
}
for (const l of logs) if (!l.startsWith('[log]')) console.log(l.slice(0, 300));
console.log(`${results.length - failed}/${results.length} passed on ${browserName}`);
process.exit(failed ? 1 : 0);
