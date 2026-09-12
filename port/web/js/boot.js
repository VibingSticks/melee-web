// Page flow: check WebGPU, take the user's disc image, then start the wasm.
import { DiscSource } from './disc_source.js';

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

if (!navigator.gpu) {
  status('This browser has no WebGPU. Use a current Chrome, Edge, Safari 26+, or Firefox with WebGPU enabled.', true);
  picker.disabled = true;
}

picker.addEventListener('change', async () => {
  const file = picker.files[0];
  if (!file) return;
  picker.disabled = true;
  try {
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

function startGame(disc, fst) {
  return new Promise((resolve, reject) => {
    window.Module = {
      canvas,
      discSource: disc,
      noInitialRun: true,
      print: (t) => { console.log(t); appendLog(t); },
      printErr: (t) => { console.error(t); appendLog(t); },
      setStatus: (t) => { if (t) status(t); },
      onRuntimeInitialized() {
        const ptr = Module._malloc(fst.length);
        Module.HEAPU8.set(fst, ptr);
        const rc = Module._port_dvd_init(ptr, fst.length);
        Module._free(ptr);
        if (rc !== 0) {
          reject(new Error('The disc file table could not be parsed.'));
          return;
        }
        status('Running');
        canvas.focus();
        try {
          Module.callMain([]);
          resolve();
        } catch (e) {
          reject(e);
        }
      },
      onAbort(what) {
        status('The game stopped: ' + what, true);
      },
    };
    const script = document.createElement('script');
    script.src = 'melee.js';
    script.onerror = () => reject(new Error('melee.js failed to load'));
    document.body.appendChild(script);
  });
}
