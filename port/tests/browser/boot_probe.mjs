// Open the hosted build in Chrome, feed it a disc image, and print the console.
// Usage: node boot_probe.mjs <build-dir> <disc.iso> [seconds] [--headed] [--query=gpu=compat] [--browser=chromium|firefox] [--stack-after=N] [--trace-after=N]
// Firefox has no WebGPU on Linux, so it exercises the automatic WebGL2 fallback.
// Serves <build-dir> on a local port for the run.
import { createRequire } from 'node:module';
import { spawn } from 'node:child_process';
import path from 'node:path';
const require = createRequire('/home/ralsei/.npm/_npx/9833c18b2d85bc59/node_modules/');
const playwright = require('playwright-core');

const [buildDir, disc, secsArg] = process.argv.slice(2);
const secs = Number(secsArg ?? 8);
const headed = process.argv.includes('--headed');
const query = process.argv.find(a => a.startsWith('--query='))?.slice(8) ?? '';
// --stack-after=N: after N seconds, pause the (possibly busy) main thread through the
// DevTools protocol and print the wasm call stack, then resume. Finds spin loops.
const stackAfter = Number(process.argv.find(a => a.startsWith('--stack-after='))?.slice(14) ?? 0);
// --trace-after=N: at N seconds, sample the renderer with Chrome's tracing service for 3 s
// (works while the main thread is busy) and print the hottest wasm functions.
const traceAfter = Number(process.argv.find(a => a.startsWith('--trace-after='))?.slice(14) ?? 0);
import { readFileSync, unlinkSync } from 'node:fs';
const browserName = process.argv.find(a => a.startsWith('--browser='))?.slice(10) ?? 'chromium';
if (!['chromium', 'firefox'].includes(browserName)) throw new Error(`unknown browser ${browserName}`);
const port = 8766;

const server = spawn('python3', ['-m', 'http.server', '-d', path.resolve(buildDir), String(port)], { stdio: 'ignore' });
await new Promise(r => setTimeout(r, 800));

const browser = await playwright[browserName].launch(browserName === 'chromium' ? {
  executablePath: '/opt/google/chrome/chrome',
  headless: !headed,
  args: ['--enable-unsafe-webgpu', '--ignore-gpu-blocklist', '--no-sandbox', '--js-flags=--stack-trace-limit=100',
         '--disable-backgrounding-occluded-windows', '--disable-renderer-backgrounding'],
} : { headless: !headed });
const page = await browser.newPage({ viewport: { width: 1280, height: 960 } });
await page.addInitScript(() => { Error.stackTraceLimit = 64; });
const t0 = Date.now();
const logs = [];
page.on('console', m => logs.push(`[${Date.now() - t0}ms][${m.type()}] ${m.text()}`));
page.on('crash', () => logs.push(`[${Date.now() - t0}ms][crash] the renderer process crashed`));
page.on('close', () => logs.push(`[${Date.now() - t0}ms][close] the page closed`));
page.on('pageerror', e => logs.push(`[${Date.now() - t0}ms][pageerror] ${e.message}\n` + String(e.stack || '').split('\n').slice(0, 40).join('\n')));
try {
  await page.goto(`http://localhost:${port}/index.html${query ? '?' + query : ''}`, { waitUntil: 'domcontentloaded' });
  await page.bringToFront();
  let cdp = null;
  if (stackAfter > 0 && browserName === 'chromium') {
    // Attach before the game runs: enabling the debugger needs the main thread, pausing does not.
    cdp = await page.context().newCDPSession(page);
    await cdp.send('Debugger.enable');
  }
  await page.setInputFiles('#disc', path.resolve(disc));
  if (traceAfter > 0 && browserName === 'chromium') {
    await page.waitForTimeout(traceAfter * 1000);
    const tracePath = '/tmp/melee-boot-trace.json';
    await browser.startTracing(page, { path: tracePath, categories: ['disabled-by-default-v8.cpu_profiler'] });
    await new Promise(r => setTimeout(r, 3000));
    await browser.stopTracing();
    const trace = JSON.parse(readFileSync(tracePath, 'utf8'));
    unlinkSync(tracePath);
    const nodes = new Map(); const self = new Map(); const parent = new Map();
    for (const ev of trace.traceEvents ?? trace) {
      const cp = ev.args?.data?.cpuProfile;
      if (!cp) continue;
      for (const n of cp.nodes ?? []) { nodes.set(n.id, n.callFrame.functionName || '(anonymous)'); if (n.parent !== undefined) parent.set(n.id, n.parent); for (const ch of n.children ?? []) parent.set(ch, n.id); }
      for (const id of cp.samples ?? []) self.set(id, (self.get(id) ?? 0) + 1);
    }
    const total = [...self.values()].reduce((a, b) => a + b, 0);
    const chain = (id) => { const out = []; for (let k = 0; k < 12 && id !== undefined; k++) { out.push(nodes.get(id)); id = parent.get(id); } return out.join(' < '); };
    logs.push(`--- ${total} samples in 3s; hottest leaf frames with callers ---`);
    for (const [id, n] of [...self.entries()].sort((a, b) => b[1] - a[1]).slice(0, 8)) logs.push(`  ${(100 * n / total).toFixed(1)}%  ${chain(id)}`);
    await page.waitForTimeout(Math.max(0, secs - traceAfter - 3) * 1000);
  } else if (stackAfter > 0 && browserName === 'chromium') {
    await page.waitForTimeout(stackAfter * 1000);
    // Is the thread idle (sleeping in an Asyncify unwind) or truly busy?
    const idle = await Promise.race([
      page.evaluate(() => `Asyncify.state=${Asyncify.state} currData=${Asyncify.currData} pending=${typeof Module._port_dvd_pending === 'function' ? Module._port_dvd_pending() : '?'}`),
      new Promise(res => setTimeout(() => res('main thread busy: evaluate timed out'), 3000)),
    ]).catch(e => 'evaluate failed: ' + e.message);
    logs.push(`--- at ${stackAfter}s: ${idle}`);
    const paused = new Promise(res => cdp.once('Debugger.paused', res));
    cdp.send('Debugger.pause').catch(e => logs.push('[cdp] pause: ' + e.message));
    const ev = await Promise.race([paused, new Promise(res => setTimeout(() => res(null), 10000))]);
    if (ev) {
      logs.push(`--- stack after ${stackAfter}s (${ev.reason}) ---`);
      for (const f of ev.callFrames.slice(0, 40)) logs.push(`    ${f.functionName || '(anonymous)'}  @${(f.url || '').split('/').pop()}:${f.location?.lineNumber}:${f.location?.columnNumber}`);
      await cdp.send('Debugger.resume');
    } else {
      logs.push('--- could not pause the page ---');
    }
    await cdp.send('Debugger.disable').catch(() => {});
    await page.waitForTimeout(Math.max(0, secs - stackAfter) * 1000);
  } else {
    await page.waitForTimeout(secs * 1000);
  }
  await page.screenshot({ path: 'boot-probe.png', timeout: 5000 });
  logs.push('renderer: ' + await page.evaluate(() => document.getElementById('renderer').textContent));
  logs.push('status: ' + await page.evaluate(() => document.getElementById('status').textContent));
} catch (e) {
  logs.push('[runner] ' + String(e.message).split('\n')[0]);
}
await browser.close().catch(() => {});
server.kill();
for (const l of logs) console.log(l.slice(0, 400));
