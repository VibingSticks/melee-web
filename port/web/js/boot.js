// Page flow: pick a renderer (WebGPU, or the WebGL2 polyfill), take the user's disc image,
// then start the wasm.
import { DiscSource } from './disc_source.js';

// The packed offline file (tools/pack_single_html.py) has no second file to
// fetch, so it defines this with everything already in memory. On the hosted
// page it is absent and each piece is loaded over the network instead.
const OFFLINE = globalThis.__MELEE_OFFLINE__ ?? null;

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

// createMelee(config) -> Promise<Module>, from -sMODULARIZE. The offline file
// has already run the script that defines it.
function meleeFactory() {
  if (globalThis.createMelee) return Promise.resolve(globalThis.createMelee);
  return new Promise((resolve, reject) => {
    const script = document.createElement('script');
    script.src = 'melee.js';
    script.onload = () => resolve(globalThis.createMelee);
    script.onerror = () => reject(new Error('melee.js failed to load'));
    document.body.appendChild(script);
  });
}

const $ = (id) => document.getElementById(id);
const statusEl = $('status');
const logEl = $('log');
const picker = $('disc');
const canvas = $('canvas');

function status(text, isError = false) {
  statusEl.textContent = text;
  statusEl.classList.toggle('error', isError);
}

function appendLog(text) {
  if (!logEl) return;
  logEl.textContent += text + '\n';
  if (logEl.textContent.length > 20000) logEl.textContent = logEl.textContent.slice(-15000);
  logEl.scrollTop = logEl.scrollHeight;
}

const params = new URLSearchParams(location.search);

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
  const createMelee = await meleeFactory();
  const Module = await createMelee({
    canvas,
    discSource: disc,
    forceCompatProfile,
    noInitialRun: true,
    print: (t) => { console.log(t); appendLog(t); },
    printErr: (t) => { console.error(t); appendLog(t); },
    setStatus: (t) => { if (t) status(t); },
    onAbort(what) { status('The game stopped: ' + what, true); },
  });
  window.Module = Module; // the browser tests reach the exports through this

  const ptr = Module._malloc(fst.length);
  Module.HEAPU8.set(fst, ptr);
  const rc = Module._port_dvd_init(ptr, fst.length);
  Module._free(ptr);
  if (rc !== 0) {
    throw new Error('The disc file table could not be parsed.');
  }
  status('Running');
  canvas.focus();
  Module.callMain([]);
}
