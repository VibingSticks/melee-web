// Page flow: pick a renderer (WebGPU, or the WebGL2 polyfill), take the user's disc image,
// then start the wasm.
import { DiscSource } from './disc_source.js';

// The packed offline file (tools/pack_single_html.py) has no second file to
// fetch, so it defines this with everything already in memory. On the hosted
// page it is absent and each piece is loaded over the network instead.
const OFFLINE = globalThis.__MELEE_OFFLINE__ ?? null;

// Chrome keeps ten frames of a stack by default. The game's crashes sit deep
// under the scene loop, so a crash report cut at ten frames names the leaf and
// never the gameplay code that got there.
Error.stackTraceLimit = 64;

// Resolves to { loadNaga, installWebGL2Fallback, openGlslCache }; only the WebGL2 path needs them.
function rendererModules() {
  if (OFFLINE) {
    return Promise.resolve({ loadNaga: OFFLINE.loadNaga, installWebGL2Fallback: OFFLINE.installWebGL2Fallback,
                             openGlslCache: OFFLINE.openGlslCache });
  }
  return Promise.all([import('./naga/naga.js'), import('./gpu-gl2.js')])
    .then(([a, b]) => ({ loadNaga: a.loadNaga, installWebGL2Fallback: b.installWebGL2Fallback,
                         openGlslCache: b.openGlslCache }));
}

// loadNaga takes either a URL or the bytes themselves.
const nagaSource = () => (OFFLINE ? OFFLINE.nagaWasm : 'naga/naga.wasm');

// Two builds of the engine differ only in how the game pauses inside its wait
// loops. JSPI (WebAssembly.Suspending, Chrome 137+) lets the browser suspend
// wasm itself; the Asyncify build rewrites every function a pause can be
// reached from, which makes it a third larger and slower. ?engine=asyncify or
// ?engine=jspi overrides the choice.
const ENGINE = (() => {
  const hasJspi = typeof WebAssembly.Suspending === 'function' && typeof WebAssembly.promising === 'function';
  const want = new URLSearchParams(location.search).get('engine');
  if (want === 'asyncify') return 'asyncify';
  return hasJspi ? 'jspi' : 'asyncify';
})();

// createMelee(config) -> Promise<Module>, from -sMODULARIZE (createMeleeJspi
// for the JSPI build). The offline file carries both engines as inert text and
// runs only the one this browser uses.
function meleeFactory() {
  const name = ENGINE === 'jspi' ? 'createMeleeJspi' : 'createMelee';
  if (globalThis[name]) return Promise.resolve(globalThis[name]);
  return new Promise((resolve, reject) => {
    const script = document.createElement('script');
    const stashed = document.getElementById(ENGINE === 'jspi' ? 'melee-engine-jspi' : 'melee-engine');
    if (stashed) {
      script.textContent = stashed.textContent; // runs synchronously on append
      stashed.remove();
      document.body.appendChild(script);
      if (globalThis[name]) resolve(globalThis[name]);
      else reject(new Error(`the embedded ${ENGINE} engine did not define ${name}`));
      return;
    }
    const file = ENGINE === 'jspi' ? 'melee-jspi.js' : 'melee.js';
    script.src = file;
    script.onload = () => resolve(globalThis[name]);
    script.onerror = () => reject(new Error(`${file} failed to load`));
    document.body.appendChild(script);
  });
}

const $ = (id) => document.getElementById(id);
const statusEl = $('status');
const logEl = $('log');
const picker = $('disc');
const canvas = $('canvas');

// Tab hides the toolbar and log so the game gets the whole window height;
// only once the game runs, so the disc picker can't be hidden away first.
let gameRunning = false;
function setUiHidden(hidden) {
  document.body.classList.toggle('ui-hidden', hidden);
}
addEventListener('keydown', (e) => {
  if (e.key !== 'Tab' || !gameRunning || e.ctrlKey || e.altKey || e.metaKey) return;
  e.preventDefault();
  e.stopImmediatePropagation();
  if (!e.repeat) setUiHidden(!document.body.classList.contains('ui-hidden'));
}, true);

function status(text, isError = false) {
  statusEl.textContent = text;
  statusEl.classList.toggle('error', isError);
}

function appendLog(text) {
  LOG_RING.push(text);
  if (LOG_RING.length > LOG_RING_MAX) LOG_RING.splice(0, LOG_RING.length - LOG_RING_MAX);
  if (!logEl) return;
  logEl.textContent += text + '\n';
  if (logEl.textContent.length > 20000) logEl.textContent = logEl.textContent.slice(-15000);
  logEl.scrollTop = logEl.scrollHeight;
}

// Everything that reaches the console reaches the log ring and the panel.
//
// Only Module.print/printErr (Aurora's stdout/stderr) fed appendLog, but the
// port's own port_log goes through emscripten_log straight to console.log,
// and the renderer shim reports through console.error -- so the first crash
// report from a real machine carried Aurora's boot banner and none of the
// game's lines: no frame timings, no archive warnings, no loads. Wrapping the
// console once makes it the single sink; print/printErr then only need to
// forward to the console.
for (const level of ['log', 'info', 'warn', 'error']) {
  const orig = console[level].bind(console);
  console[level] = (...args) => {
    orig(...args);
    try { appendLog(args.map(a => (typeof a === 'string' ? a : (a instanceof Error ? (a.stack || a.message) : JSON.stringify(a)))).join(' ')); }
    catch { appendLog(String(args[0])); }
  };
}

const params = new URLSearchParams(location.search);

// --- Crash reports -----------------------------------------------------------
//
// A crash on someone else's machine is unreportable without this: the page
// shows one red line and the stack is gone. Everything that can end the game
// funnels into one report -- an uncaught error (a wasm trap arrives here as a
// RuntimeError whose stack names the frames, now that release builds keep
// them), an unhandled rejection, Emscripten's abort, and a deliberate exit --
// together with the scene the game was in, the memory it was using, and the
// last few hundred log lines, which carry the profiler's frame timings.
//
// The report is also kept in localStorage, because the failure that matters
// most on a small machine leaves no error at all: the browser kills the tab
// for memory. A heartbeat records how far the game got, and the next start
// notices a session that ended without either a clean unload or a recorded
// crash and says so.
const LOG_RING = [];
const LOG_RING_MAX = 400;
const CRASH_KEY = 'melee-last-crash';
const HEARTBEAT_KEY = 'melee-heartbeat';
let crashReported = false;

function buildId() {
  const content = document.querySelector('meta[name="melee-build"]')?.content ?? '';
  return content && !content.startsWith('@') ? content : 'unknown';
}

// What the game was doing, read through the same exports the tests use.
function gameState() {
  const M = window.Module;
  const out = {};
  try { if (M?._port_scene_state) { const v = M._port_scene_state(); out.scene = `mode ${(v >> 8) & 0xff} index ${v & 0xff}`; } } catch { /* runtime gone */ }
  try { if (M?._port_frame_count) out.frame = M._port_frame_count(); } catch { /* runtime gone */ }
  try { if (M?.HEAPU8) out.wasmHeapMB = (M.HEAPU8.length / 1048576).toFixed(0); } catch { /* runtime gone */ }
  try { const m = performance.memory; if (m) out.jsHeapMB = (m.usedJSHeapSize / 1048576).toFixed(0); } catch { /* not exposed */ }
  return out;
}

const stateLine = () => Object.entries(gameState()).map(([k, v]) => `${k}=${v}`).join(' ') || '(runtime not up)';

function crashReport(kind, detail) {
  return [
    'melee-web crash report',
    `build: ${buildId()}`,
    `when: ${new Date().toISOString()}`,
    `kind: ${kind}`,
    `page: ${location.protocol}//${location.host}${location.pathname}${location.search}`,
    `renderer: ${$('renderer')?.textContent || '?'} (menu: ${$('renderer-select')?.value || '?'})  engine: ${ENGINE}`,
    `browser: ${navigator.userAgent}`,
    `cores: ${navigator.hardwareConcurrency ?? '?'}  deviceMemoryGB: ${navigator.deviceMemory ?? '?'}  screen: ${screen.width}x${screen.height}`,
    `game: ${stateLine()}`,
    '',
    '--- error ---',
    String(detail ?? '(none)'),
    '',
    `--- last ${LOG_RING.length} log lines ---`,
    ...LOG_RING,
  ].join('\n');
}

function writeHeartbeat(clean) {
  try {
    localStorage.setItem(HEARTBEAT_KEY, JSON.stringify({ at: new Date().toISOString(), clean, crashed: crashReported, game: stateLine() }));
  } catch { /* storage unavailable: nothing to do */ }
}

function showCrash(text, headline) {
  try { localStorage.setItem(CRASH_KEY, text); } catch { /* still shown on the page */ }
  const box = $('crash');
  if (box) { box.hidden = false; box.dataset.report = text; }
  setUiHidden(false);
  status(headline, true);
}

function reportCrash(kind, err) {
  if (crashReported) return; // the first failure is the cause; what follows is fallout
  crashReported = true;
  const detail = err instanceof Error ? (err.stack || err.message)
    : typeof err === 'string' ? err
    : (() => { try { return JSON.stringify(err); } catch { return String(err); } })();
  console.error(`[melee] crash (${kind}): ${detail}`);
  showCrash(crashReport(kind, detail), `The game crashed (${kind}). Use "Copy crash report" and send it along.`);
  writeHeartbeat(false);
}

function installCrashHandlers() {
  window.addEventListener('error', (e) => reportCrash('uncaught error', e.error ?? e.message));
  window.addEventListener('unhandledrejection', (e) => reportCrash('unhandled rejection', e.reason));

  const box = $('crash');
  if (!box) return;
  $('crash-copy').addEventListener('click', async () => {
    const text = box.dataset.report || '';
    try {
      await navigator.clipboard.writeText(text);
      status('Crash report copied to the clipboard.');
    } catch {
      download(`melee-crash-${Date.now()}.txt`, text, 'text/plain'); // clipboard refused: hand over a file instead
    }
  });
  $('crash-save').addEventListener('click', () => download(`melee-crash-${Date.now()}.txt`, box.dataset.report || '', 'text/plain'));

  // Did the previous session end badly? A recorded crash is shown again; a
  // heartbeat that never saw a clean unload and recorded no crash means the
  // browser killed the tab, and on a small machine that is memory.
  let hb = null;
  try { hb = JSON.parse(localStorage.getItem(HEARTBEAT_KEY) ?? 'null'); } catch { /* absent or unreadable */ }
  if (hb && (hb.crashed || !hb.clean)) {
    let last = '';
    try { last = localStorage.getItem(CRASH_KEY) || ''; } catch { /* absent */ }
    if (hb.crashed && last) {
      showCrash(last, 'The last session crashed. "Copy crash report" has the details.');
    } else {
      const note = `The previous session ended at ${hb.at} with no error recorded (${hb.game}).\n`
        + 'No unload was seen either, so the browser most likely killed the tab -- on a small machine that is usually memory.';
      showCrash(crashReport('tab killed (no error recorded)', note), 'The last session died without an error. "Copy crash report" has what is known.');
    }
    try { localStorage.removeItem(HEARTBEAT_KEY); } catch { /* fine */ }
  }
}

function startHeartbeat() {
  writeHeartbeat(false);
  setInterval(() => writeHeartbeat(false), 5000);
  addEventListener('pagehide', () => writeHeartbeat(true));
}

installCrashHandlers();

// The renderer the player asked for: ?renderer= in the address wins, then the
// choice saved by the toolbar's Renderer menu, else auto. Storage can be
// missing (a private window, a blocked file:// origin); auto then.
const RENDERER_KEY = 'melee-renderer';
const rendererChoice = (() => {
  const fromUrl = params.get('renderer');
  if (fromUrl === 'webgpu' || fromUrl === 'webgl2') return { value: fromUrl, fromUrl: true };
  let saved = null;
  try { saved = localStorage.getItem(RENDERER_KEY); } catch { saved = null; }
  return { value: saved === 'webgpu' || saved === 'webgl2' ? saved : 'auto', fromUrl: false };
})();

// Renderer selection: WebGPU when the browser gives us an adapter, otherwise the WebGL2
// polyfill (port/web/js/gpu-gl2.js). ?renderer=webgl2|webgpu, or the toolbar menu,
// forces one. See docs/superpowers/specs/2026-09-11-webgl2-fallback-design.md.
async function selectRenderer() {
  const forced = rendererChoice.value === 'auto' ? null : rendererChoice.value;
  let haveWebGPU = false;
  if (forced !== 'webgl2' && navigator.gpu) {
    try { haveWebGPU = !!(await navigator.gpu.requestAdapter()); } catch { haveWebGPU = false; }
  }
  if (haveWebGPU) return 'webgpu';
  // A saved choice from another session is only a preference: fall back
  // rather than refuse to start. An explicit ?renderer=webgpu still errors.
  if (forced === 'webgpu' && rendererChoice.fromUrl) throw new Error('This browser has no usable WebGPU adapter (?renderer=webgpu was requested).');
  const probe = document.createElement('canvas').getContext('webgl2');
  if (!probe) throw new Error('This browser has neither WebGPU nor WebGL2. Use a current Chrome, Edge, Firefox, or Safari.');
  const { loadNaga, installWebGL2Fallback, openGlslCache } = await rendererModules();
  const naga = await loadNaga(nagaSource());
  // Shader translations saved by earlier boots (?noglslcache ignores them).
  const glslCache = params.has('noglslcache') ? null : await openGlslCache(naga.version);
  if (glslCache) console.log(`[boot] saved shader translations: ${glslCache.loaded}`);
  installWebGL2Fallback({ canvas, naga, force: true, glslCache });
  return 'webgl2';
}

// The toolbar's Renderer menu. The renderer is chosen once, before a disc is
// picked, so a change saves the choice, puts it in the address and reloads;
// once a disc is picked the menu is locked until the page is reloaded.
const rendererSelect = $('renderer-select');
rendererSelect.value = rendererChoice.value;
rendererSelect.addEventListener('change', () => {
  const value = rendererSelect.value;
  try {
    if (value === 'auto') localStorage.removeItem(RENDERER_KEY); else localStorage.setItem(RENDERER_KEY, value);
  } catch { /* the address carries it instead */ }
  const next = new URLSearchParams(location.search);
  if (value === 'auto') next.delete('renderer'); else next.set('renderer', value);
  const query = next.toString();
  location.href = location.pathname + (query ? `?${query}` : '') + location.hash;
});

const rendererReady = selectRenderer().then((r) => {
  $('renderer').textContent = r === 'webgpu' ? '(using WebGPU)' : '(using WebGL2)';
  console.log(`[boot] engine: ${ENGINE === 'jspi' ? 'JSPI' : 'Asyncify'}`);
  return r;
}, (e) => {
  status(String(e.message || e), true);
  picker.disabled = true;
  throw e;
});

picker.addEventListener('change', async () => {
  const file = picker.files[0];
  if (!file) return;
  picker.disabled = true;
  rendererSelect.disabled = true; // the game is about to start on the current renderer
  try {
    await rendererReady;
    const disc = new DiscSource(file);
    const hdr = await disc.validate();
    const fst = new Uint8Array(await disc.read(hdr.fstOffset, hdr.fstSize));
    status(`Disc ${hdr.gameId} (${(file.size / 1048576).toFixed(0)} MB). Starting…`);
    await startGame(disc, fst);
  } catch (e) {
    status(String(e.message || e), true);
    picker.disabled = false;
    rendererSelect.disabled = false;
  }
});

async function startGame(disc, fst) {
  // Renderer profile overrides for testing (see docs/superpowers/specs/2026-09-11-webgl2-fallback-design.md).
  const gpuFlag = params.get('gpu');
  const forceCompatProfile = { compat: 7, noimm: 1, nostorage: 2, nocompute: 4 }[gpuFlag] ?? 0;
  // ?res=WxH renders at that size and scales to the canvas. The game itself is
  // 640x480; anything above that is supersampling, and the cost is fill-rate
  // bound, so dropping to 640x480 is the first thing to try on a slow machine.
  const resFlag = /^(\d{2,4})x(\d{2,4})$/.exec(params.get('res') ?? '');
  // Without ?res the engine renders at 1280x960. A small machine (two cores or
  // 4 GB, a typical Chromebook) gets the game's own 640x480 instead: a quarter
  // of the pixels to shade, copy and scale every frame.
  const smallMachine = (navigator.hardwareConcurrency ?? 8) <= 2 || (navigator.deviceMemory ?? 8) <= 4;
  const renderWidth = resFlag ? +resFlag[1] : smallMachine ? 640 : 0;
  const renderHeight = resFlag ? +resFlag[2] : smallMachine ? 480 : 0;
  if (!resFlag && smallMachine) {
    console.log('[boot] small machine: rendering at 640x480 (add ?res=1280x960 for the sharper default)');
  }
  // ?yield=timer puts the game's busy-wait yields back on setTimeout(0), for
  // A/B-ing the menu pauses (see port_yield_browser in imports.js).
  const yieldTimer = params.get('yield') === 'timer';
  // ?frameskip=off: draw every game frame even when that means slow motion.
  const noFrameSkip = params.get('frameskip') === 'off';
  const audioBackend = params.get('audio'); // 'sdl' keeps SDL's ScriptProcessor output
  const audioFxOff = params.get('fx') === 'off'; // the reverb and delay, for comparing
  const preload = params.get('preload'); // 'off' starts without compiling the pipelines first
  const createMelee = await meleeFactory();
  const seedPromise = params.has('noseed') ? Promise.resolve(null) : loadPipelineSeed();
  const Module = await createMelee({
    canvas,
    discSource: disc,
    forceCompatProfile,
    renderWidth,
    noFrameSkip,
    audioBackend,
    audioFxOff,
    preload,
    preloadInFlight: +(params.get('preloadjobs') ?? 0), // pipelines compiling at once during the preload (default 16)
    onPreload: preloadScreen(),
    renderHeight,
    yieldTimer,
    noInitialRun: true,
    print: (t) => { console.log(t); },     // the console wrapper above feeds the log
    printErr: (t) => { console.error(t); },
    setStatus: (t) => { if (t) status(t); },
    onAbort(what) { reportCrash('abort', what); },
    // The port exits deliberately when the game panics (HSD_Panic); to the
    // player that is a crash, and the log tail says why.
    onExit(code) { reportCrash('exit', `the game exited with status ${code}`); },
  });
  window.Module = Module; // the browser tests reach the exports through this
  wireSaveButtons(Module);
  startHeartbeat();

  const ptr = Module._malloc(fst.length);
  Module.HEAPU8.set(fst, ptr);
  const rc = Module._port_dvd_init(ptr, fst.length);
  Module._free(ptr);
  if (rc !== 0) {
    throw new Error('The disc file table could not be parsed.');
  }
  // The shader pipelines a recorded session used, compiled behind the loading
  // screen before the game starts (port_pipeline_seed_take in imports.js
  // hands them over; main_loop.c preload_pipelines). On the WebGL2 fallback
  // too: it compiles on the main thread, which the loading screen can afford.
  await rendererReady;
  Module.pipelineSeed = await seedPromise;
  const saved = params.has('noseed') ? null : await savedPipelines.load(Module.pipelineSeed);
  Module.pipelineSeedSaved = saved;
  Module.savePipelines = () => savedPipelines.merge(Module);
  if (Module.pipelineSeed) console.log('[boot] pipeline seed handed to the game');
  status('Running - press Tab to hide this bar');
  gameRunning = true;
  canvas.focus();
  Module.callMain([]);
}

// The boot screen while the pipeline seed compiles (preload_pipelines in
// main_loop.c calls this through port_preload_progress). Returns true once
// Skip has been pressed.
function preloadScreen() {
  const el = $('preload'), bar = $('preload-bar'), count = $('preload-count'), eta = $('preload-eta');
  let skip = false, t0 = 0, done0 = 0;
  $('preload-skip').addEventListener('click', () => { skip = true; eta.textContent = 'starting...'; });
  return (done, total, finished) => {
    if (finished) {
      const g = globalThis.gl2Stats; // WebGL2 fallback only (gpu-gl2.js)
      if (g) console.log(`[boot] webgl2 shaders: ${g.programs} programs for ${g.pipelines} pipelines, ` +
                         `${g.translations} translated (${Math.round(g.translateMs)} ms), ${g.saved} from saved, ` +
                         `compile ${Math.round(g.compileMs)} ms`);
      el.hidden = true;
      status('Running - press Tab to hide this bar');
      canvas.focus();
      return skip;
    }
    if (el.hidden) {
      el.hidden = false;
      status('Compiling shaders before the game starts...');
    }
    if (!t0) { t0 = performance.now(); done0 = done; }
    const secs = (performance.now() - t0) / 1000, rate = (done - done0) / secs;
    bar.max = total || 1;
    bar.value = done;
    count.textContent = `${done} / ${total} shaders (${total ? Math.floor(100 * done / total) : 0}%)` +
                        (secs > 1 && rate > 0 ? `, ${rate.toFixed(1)} per second` : '');
    if (!skip && secs > 3 && rate > 0) {
      const left = Math.round((total - done) / rate);
      eta.textContent = `elapsed ${Math.round(secs)} s, about ${left < 60 ? left + ' s' : Math.round(left / 60) + ' min'} left`;
    }
    return skip;
  };
}

// The pipelines this browser's own sessions needed that the shipped list
// lacked (a Classic boss, a stage the recording never visited), kept in
// IndexedDB so the next boot's loading screen compiles them too. Stored as
// the same seed format, gzipped: magic "APSD", count, then per entry
// type, version, size, first frame (u32 LE each) and the config bytes.
const savedPipelines = (() => {
  const DB = 'melee-pipelines', STORE = 'lists', KEY = 'mine';
  let entries = null;    // Map key -> Uint8Array entry (header + config)
  let shipped = new Map(); // what pipelines.bin.gz already has: not worth saving again
  let busy = false;
  const open = () => new Promise((resolve, reject) => {
    const req = indexedDB.open(DB, 1);
    req.onupgradeneeded = () => req.result.createObjectStore(STORE);
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
  const gunzip = async (b) => new Uint8Array(await new Response(new Blob([b]).stream().pipeThrough(new DecompressionStream('gzip'))).arrayBuffer());
  const gzip = async (b) => new Uint8Array(await new Response(new Blob([b]).stream().pipeThrough(new CompressionStream('gzip'))).arrayBuffer());
  const keyOf = (u8, off, len) => { // FNV-1a over the entry's bytes
    let h = 0x811c9dc5;
    for (let i = off; i < off + len; i++) h = Math.imul(h ^ u8[i], 0x01000193);
    return `${h >>> 0}:${len}`;
  };
  function parse(u8, into) {
    const dv = new DataView(u8.buffer, u8.byteOffset, u8.byteLength);
    if (u8.length < 8 || dv.getUint32(0, true) !== 0x44535041) return 0;
    let off = 8, added = 0;
    for (let i = 0, n = dv.getUint32(4, true); i < n && off + 16 <= u8.length; i++) {
      const len = 16 + dv.getUint32(off + 8, true);
      if (off + len > u8.length) break;
      // The first-frame field (off + 12) differs between sessions; leave it out of the key.
      const k = keyOf(u8, off, 12) + '/' + keyOf(u8, off + 16, len - 16);
      if (!into.has(k) && !(into !== shipped && shipped.has(k))) { into.set(k, u8.slice(off, off + len)); added++; }
      off += len;
    }
    return added;
  }
  function serialize() {
    let total = 8;
    for (const e of entries.values()) total += e.length;
    const out = new Uint8Array(total), dv = new DataView(out.buffer);
    dv.setUint32(0, 0x44535041, true); dv.setUint32(4, entries.size, true);
    let off = 8;
    for (const e of entries.values()) { out.set(e, off); off += e.length; }
    return out;
  }
  return {
    async load(shippedSeed) {
      entries = new Map();
      if (shippedSeed) parse(shippedSeed, shipped);
      try {
        if (typeof DecompressionStream !== 'function') return null;
        const db = await open();
        const gz = await new Promise((resolve) => {
          const req = db.transaction(STORE, 'readonly').objectStore(STORE).get(KEY);
          req.onsuccess = () => resolve(req.result); req.onerror = () => resolve(null);
        });
        if (!gz) return null;
        const raw = await gunzip(gz);
        parse(raw, entries);
        console.log(`[boot] saved pipelines: ${entries.size} from earlier sessions (${gz.length} bytes)`);
        return raw;
      } catch (e) {
        console.warn('[boot] saved pipelines not loaded:', e && e.message);
        return null;
      }
    },
    async merge(Module) {
      if (busy || !entries || typeof CompressionStream !== 'function') return;
      busy = true;
      try {
        const sizePtr = Module._malloc(4);
        const p = Module._port_debug_pipeline_export(sizePtr);
        const h = Module.HEAPU8, n = (h[sizePtr] | h[sizePtr + 1] << 8 | h[sizePtr + 2] << 16 | h[sizePtr + 3] << 24) >>> 0;
        Module._free(sizePtr);
        if (!p) return;
        const exported = Module.HEAPU8.slice(p, p + n);
        Module._free(p);
        const added = parse(exported, entries);
        if (!added) return;
        const gz = await gzip(serialize());
        const db = await open();
        await new Promise((resolve) => {
          const tx = db.transaction(STORE, 'readwrite');
          tx.objectStore(STORE).put(gz, KEY);
          tx.oncomplete = resolve; tx.onerror = resolve;
        });
        console.log(`[melee] pipelines: saved ${entries.size} for the next boot's preload (+${added}, ${gz.length} bytes)`);
      } catch (e) {
        console.warn('[melee] pipelines not saved:', e && e.message);
      } finally {
        busy = false;
      }
    },
  };
})();

// The pipeline warm-up list, pipelines.bin.gz: base64 in a text script of the
// offline file, a file next to the page otherwise. Absent or undecodable, the
// game just compiles pipelines as it meets them.
async function loadPipelineSeed() {
  try {
    let gz;
    const stashed = document.getElementById('melee-pipelines');
    if (stashed) {
      const bin = atob(stashed.textContent.trim());
      stashed.remove();
      gz = new Uint8Array(bin.length);
      for (let i = 0; i < bin.length; i++) gz[i] = bin.charCodeAt(i);
    } else {
      const r = await fetch(new URL('pipelines.bin.gz', location.href));
      if (!r.ok) return null;
      gz = new Uint8Array(await r.arrayBuffer());
    }
    if (typeof DecompressionStream !== 'function') return null;
    const stream = new Blob([gz]).stream().pipeThrough(new DecompressionStream('gzip'));
    const seed = new Uint8Array(await new Response(stream).arrayBuffer());
    console.log(`[boot] pipeline seed: ${gz.length} bytes ${stashed ? 'embedded' : 'fetched'}, ${seed.length} decoded`);
    return seed;
  } catch (e) {
    console.warn('[boot] pipeline seed not loaded:', e.message);
    return null;
  }
}

// --- Memory-card export and import -----------------------------------------
//
// The card lives on an IDBFS mount (port/src/save_web.c), which survives a
// reload but not a browser that clears site data, and cannot be carried to
// another machine. These two buttons cover both.
//
// Aurora keeps the card as a folder of .gci files, which is the same shape
// Dolphin uses, so a single save exports as a plain .gci that Dolphin can
// import. Several files need a container, and that is a small JSON bundle.

const SAVE_ROOT = '/saves';

// Every regular file under the mount, except the renderer's own settings.
function listSaveFiles(FS) {
  const out = [];
  const walk = (dir) => {
    let names;
    try { names = FS.readdir(dir); } catch { return; }
    for (const n of names) {
      if (n === '.' || n === '..') continue;
      const full = `${dir}/${n}`;
      const st = FS.stat(full);
      if (FS.isDir(st.mode)) walk(full);
      else if (full !== `${SAVE_ROOT}/imgui.ini`) out.push({ path: full, size: st.size });
    }
  };
  walk(SAVE_ROOT);
  return out;
}

// btoa on a long string blows the argument limit, so build it in chunks.
function toBase64(bytes) {
  let s = '';
  for (let i = 0; i < bytes.length; i += 0x8000) {
    s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
  }
  return btoa(s);
}

function fromBase64(b64) {
  const bin = atob(b64);
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}

function download(name, bytes, type) {
  const url = URL.createObjectURL(new Blob([bytes], { type }));
  const a = document.createElement('a');
  a.href = url;
  a.download = name;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 10000);
}

function exportSave(Module) {
  const FS = Module.FS;
  const files = listSaveFiles(FS);
  if (files.length === 0) {
    status('No save to export yet - play far enough for the game to write one.', true);
    return;
  }
  const stamp = new Date().toISOString().slice(0, 10);
  if (files.length === 1) {
    // One file: hand it over as-is, so Dolphin can import it too.
    const name = files[0].path.split('/').pop();
    download(`melee-${stamp}-${name}`, FS.readFile(files[0].path), 'application/octet-stream');
    status(`Exported ${name} (${files[0].size} bytes).`);
    return;
  }
  const bundle = {
    format: 'melee-web-save',
    version: 1,
    saved: new Date().toISOString(),
    // Paths are stored relative to the mount so a future layout change can
    // still place them.
    files: files.map(f => ({ path: f.path.slice(SAVE_ROOT.length + 1), data: toBase64(FS.readFile(f.path)) })),
  };
  download(`melee-${stamp}-saves.json`, JSON.stringify(bundle), 'application/json');
  status(`Exported ${files.length} save files.`);
}

// Create every directory on the way to a path; FS has no mkdir -p.
function mkdirp(FS, dir) {
  let at = '';
  for (const part of dir.split('/').filter(Boolean)) {
    at += `/${part}`;
    try { FS.mkdir(at); } catch { /* already there */ }
  }
}

// Where a single loose .gci belongs: the deepest folder the card already uses,
// so an import lands beside the files Aurora expects rather than at the root.
function cardDir(FS) {
  let dir = SAVE_ROOT;
  for (;;) {
    let next = null;
    for (const n of FS.readdir(dir)) {
      if (n === '.' || n === '..') continue;
      const full = `${dir}/${n}`;
      if (FS.isDir(FS.stat(full).mode)) { next = full; break; }
    }
    if (next === null) return dir;
    dir = next;
  }
}

async function importSave(Module, file) {
  const FS = Module.FS;
  const bytes = new Uint8Array(await file.arrayBuffer());

  let written = 0;
  let bundle = null;
  if (file.name.endsWith('.json')) {
    try {
      bundle = JSON.parse(new TextDecoder().decode(bytes));
    } catch {
      status('That file is not a readable save bundle.', true);
      return;
    }
    if (bundle.format !== 'melee-web-save' || !Array.isArray(bundle.files)) {
      status('That JSON is not a Melee save bundle.', true);
      return;
    }
    for (const f of bundle.files) {
      // Reject anything trying to climb out of the mount.
      if (typeof f.path !== 'string' || f.path.includes('..') || f.path.startsWith('/')) {
        status('That bundle contains an unsafe path; nothing was imported.', true);
        return;
      }
    }
    for (const f of bundle.files) {
      const full = `${SAVE_ROOT}/${f.path}`;
      mkdirp(FS, full.slice(0, full.lastIndexOf('/')));
      FS.writeFile(full, fromBase64(f.data));
      written++;
    }
  } else {
    // Aurora's card folder holds .gci files and ignores anything else, so the
    // imported file has to land with that extension whatever the browser
    // happened to call the download. Keep the stem for recognisability, drop
    // any directory parts, and refuse a name that could escape the folder.
    const stem = (file.name.split(/[\\/]/).pop() || 'save')
      .replace(/\.(gci|raw|bin)$/i, '')
      .replace(/[^A-Za-z0-9._-]/g, '_')
      .replace(/^\.+/, '')
      .slice(0, 64) || 'save';
    const dir = cardDir(FS);
    mkdirp(FS, dir);
    const dest = `${dir}/${stem}.gci`;
    FS.writeFile(dest, bytes);
    console.log(`[melee] saves: imported ${bytes.length} bytes to ${dest}`);
    written = 1;
  }

  // Push it into IndexedDB before the reload that makes the game read it.
  await new Promise((resolve) => FS.syncfs(false, (err) => {
    if (err) console.error('[melee] saves: import could not be stored:', err);
    else console.log('[melee] saves: import stored');
    resolve();
  }));
  status(`Imported ${written} save file${written === 1 ? '' : 's'} - reloading...`);
  setTimeout(() => location.reload(), 900);
}

function wireSaveButtons(Module) {
  const box = $('saves');
  const exportBtn = $('export');
  const importInput = $('import');
  if (!box) return;
  box.hidden = false;
  exportBtn.disabled = false;
  exportBtn.addEventListener('click', () => {
    try { exportSave(Module); } catch (e) { status('Export failed: ' + e.message, true); }
  });
  // The same report a crash produces, on demand: the per-second frame timings
  // and load lines are what a performance question needs.
  $('log-save')?.addEventListener('click', () => {
    download(`melee-log-${Date.now()}.txt`, crashReport('log', 'saved by the player, no crash'), 'text/plain');
  });
  importInput.addEventListener('change', async () => {
    const file = importInput.files?.[0];
    importInput.value = ''; // so picking the same file twice still fires
    if (file) {
      try { await importSave(Module, file); } catch (e) { status('Import failed: ' + e.message, true); }
    }
  });
}
