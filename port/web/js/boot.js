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

// Resolves to { loadNaga, installWebGL2Fallback }; only the WebGL2 path needs them.
function rendererModules() {
  if (OFFLINE) {
    return Promise.resolve({ loadNaga: OFFLINE.loadNaga, installWebGL2Fallback: OFFLINE.installWebGL2Fallback });
  }
  return Promise.all([import('./naga/naga.js'), import('./gpu-gl2.js')])
    .then(([a, b]) => ({ loadNaga: a.loadNaga, installWebGL2Fallback: b.installWebGL2Fallback }));
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
    `renderer: ${$('renderer')?.textContent || '?'}  engine: ${ENGINE}`,
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

// Renderer selection: WebGPU when the browser gives us an adapter, otherwise the WebGL2
// polyfill (port/web/js/gpu-gl2.js). ?renderer=webgl2|webgpu forces one.
// See docs/superpowers/specs/2026-09-11-webgl2-fallback-design.md.
async function selectRenderer() {
  const forced = params.get('renderer');
  let haveWebGPU = false;
  if (forced !== 'webgl2' && navigator.gpu) {
    try { haveWebGPU = !!(await navigator.gpu.requestAdapter()); } catch { haveWebGPU = false; }
  }
  if (haveWebGPU) return 'webgpu';
  if (forced === 'webgpu') throw new Error('This browser has no usable WebGPU adapter (?renderer=webgpu was requested).');
  const probe = document.createElement('canvas').getContext('webgl2');
  if (!probe) throw new Error('This browser has neither WebGPU nor WebGL2. Use a current Chrome, Edge, Firefox, or Safari.');
  const { loadNaga, installWebGL2Fallback } = await rendererModules();
  const naga = await loadNaga(nagaSource());
  installWebGL2Fallback({ canvas, naga, force: true });
  return 'webgl2';
}

const rendererReady = selectRenderer().then((r) => {
  $('renderer').textContent = r === 'webgpu' ? 'Renderer: WebGPU' : 'Renderer: WebGL2 (fallback)';
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
  const createMelee = await meleeFactory();
  const Module = await createMelee({
    canvas,
    discSource: disc,
    forceCompatProfile,
    renderWidth,
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
  status('Running - press Tab to hide this bar');
  gameRunning = true;
  canvas.focus();
  Module.callMain([]);
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
  importInput.addEventListener('change', async () => {
    const file = importInput.files?.[0];
    importInput.value = ''; // so picking the same file twice still fires
    if (file) {
      try { await importSave(Module, file); } catch (e) { status('Import failed: ' + e.message, true); }
    }
  });
}
