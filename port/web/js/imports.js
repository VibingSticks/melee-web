// Functions the wasm imports (declared in port/src/dvd_web/disc_io.h).
// Attached with `--js-library`. Module.discSource is set by boot.js before the
// runtime starts.
mergeInto(LibraryManager.library, {
  port_disc_prefetch__sig: 'vii',
  port_disc_prefetch: function (offset, length) {
    // A hint (see disc_io.h); failures are the next real read's to report.
    if (Module.discSource && Module.discSource.prefetch) {
      Module.discSource.prefetch(offset >>> 0, length >>> 0);
    }
  },
  port_disc_read__sig: 'viiiii',
  port_disc_read: function (offset, length, dst, cb, user) {
    // Every path out of here must call cb exactly once. If it does not, the
    // C side waits on an in-flight read that can never complete and the game
    // spins forever, so the success handler runs inside a try/catch rather
    // than leaving a throw to become an unhandled rejection.
    var done = function (status) {
      {{{ makeDynCall('vii', 'cb') }}}(user, status);
    };
    Module.discSource.read(offset >>> 0, length >>> 0).then(
      function (buf) {
        try {
          var src = new Uint8Array(buf);
          if (dst < 0 || dst + src.length > HEAPU8.length) {
            throw new RangeError(
              'disc read of ' + src.length + ' bytes at ' + dst +
              ' does not fit the ' + HEAPU8.length + '-byte heap');
          }
          HEAPU8.set(src, dst);
        } catch (err) {
          console.error('[melee] disc read could not be delivered:', err);
          done(-1);
          return;
        }
        done(0);
      },
      function (err) {
        console.error('[melee] disc read failed:', err);
        done(-1);
      });
  },
  // One turn of the browser's event loop, without setTimeout's clamp.
  //
  // emscripten_sleep(0) is setTimeout(0), and Chrome clamps a timer set from
  // inside a timer callback -- which under Asyncify every wake-up is -- to a
  // 4 ms minimum once five deep. The game yields once per disc chunk while it
  // loads a scene, and HSD's DevCom relays ARAM-bound data in 16 KB chunks, so
  // a 3 MB load paid ~200 x 4 ms of timer clamp and froze the frame for most
  // of a second. A MessageChannel message is a plain macrotask: the browser
  // still gets to complete the read, run its callbacks and paint between two
  // of them, but the round trip is a fraction of a millisecond.
  $portYieldQueue: [],
  $portYieldChannel: null,
  // 'auto' has the glue wrap this in Asyncify.handleAsync (as emscripten_sleep
  // is); `true` would mean the function drives Asyncify itself, and a bare
  // Promise then comes back to the wasm as 0 without ever suspending.
  port_yield_browser__async: 'auto',
  port_yield_browser__deps: ['$portYieldQueue', '$portYieldChannel'],
  port_yield_browser: function () {
    return new Promise(function (resolve) {
      if (Module.yieldTimer) { // ?yield=timer: the old behaviour, for comparison
        setTimeout(resolve, 0);
        return;
      }
      if (portYieldChannel === null) {
        portYieldChannel = new MessageChannel();
        portYieldChannel.port1.onmessage = function () {
          var next = portYieldQueue.shift();
          if (next) next();
        };
      }
      portYieldQueue.push(resolve);
      portYieldChannel.port2.postMessage(0);
    });
  },
  // The pipeline warm-up list boot.js decoded (Module.pipelineSeed), copied
  // into the heap for aurora_pipeline_seed_import; the caller frees it.
  port_pipeline_seed_take__sig: 'ii',
  port_pipeline_seed_take__deps: ['malloc'],
  port_pipeline_seed_take: function (sizePtr) {
    const seed = Module.pipelineSeed;
    Module.pipelineSeed = null;
    if (!seed || !seed.length) { HEAPU32[sizePtr >> 2] = 0; return 0; }
    const p = _malloc(seed.length);
    HEAPU8.set(seed, p);
    HEAPU32[sizePtr >> 2] = seed.length;
    return p;
  },
  // Audio out through an AudioWorklet (ax_hle.c). SDL's Emscripten backend
  // plays from a ScriptProcessorNode, whose callback runs on the main thread:
  // any frame that holds the thread past its ~43 ms buffer is a gap in the
  // sound. A worklet plays on the audio thread from a queue the game fills,
  // so the game only has to stay ahead of the queue, not of every callback.
  // The worklet reports how much it played and how long it ran dry, which is
  // what the mixer uses to stay on the wall clock.
  port_audio_open__sig: 'ii',
  port_audio_open: function (rate) {
    try {
      var AC = window.AudioContext || window.webkitAudioContext;
      if (!AC || !AC.prototype || !('audioWorklet' in AC.prototype)) return 0;
      var ctx = new AC({ sampleRate: rate, latencyHint: 'interactive' });
      var a = Module.portAudio = {
        ctx: ctx, rate: rate, node: null, failed: false,
        posted: 0,         // frames handed to the worklet
        played: 0,         // frames it had played at the last report
        dry: 0,            // frames of silence it played for want of data
        reportedAt: 0,     // performance.now() of that report
        pending: [],       // chunks pushed before the worklet was ready
      };
      var src =
        'class PortAudio extends AudioWorkletProcessor {\n' +
        '  constructor() {\n' +
        '    super(); this.q = []; this.off = 0; this.avail = 0;\n' +
        '    this.played = 0; this.dry = 0; this.started = false; this.last = 0;\n' +
        '    this.port.onmessage = (e) => {\n' +
        '      const b = e.data; this.q.push(b); this.avail += b.length >> 1; this.started = true;\n' +
        '      const cap = sampleRate * 0.4;\n' +  // never hold more than 400 ms
        '      while (this.avail > cap && this.q.length > 1) {\n' +
        '        const d = this.q.shift(), gone = (d.length >> 1) - this.off;\n' +
        '        this.avail -= gone; this.played += gone; this.off = 0;\n' +  // counted as played: the game's queue estimate is posted - played
        '      }\n' +
        '    };\n' +
        '  }\n' +
        '  process(inputs, outputs) {\n' +
        '    const out = outputs[0], L = out[0], R = out[1] || out[0], n = L.length;\n' +
        '    let i = 0;\n' +
        '    while (i < n && this.q.length) {\n' +
        '      const b = this.q[0], left = (b.length >> 1) - this.off, take = Math.min(n - i, left);\n' +
        '      for (let k = 0; k < take; k++) { const j = (this.off + k) << 1; L[i + k] = b[j]; R[i + k] = b[j + 1]; }\n' +
        '      i += take; this.off += take;\n' +
        '      if (take === left) { this.q.shift(); this.off = 0; }\n' +
        '    }\n' +
        '    this.avail -= i; this.played += i;\n' +
        '    if (i < n) { L.fill(0, i); if (R !== L) R.fill(0, i); if (this.started) this.dry += n - i; }\n' +
        '    if (currentFrame - this.last >= sampleRate / 50) {\n' +
        '      this.last = currentFrame; this.port.postMessage([this.played, this.dry]);\n' +
        '    }\n' +
        '    return true;\n' +
        '  }\n' +
        '}\n' +
        'registerProcessor("port-audio", PortAudio);\n';
      // A blob: URL first; from file:// (the offline page) the document has
      // no origin and Chrome refuses blob: worklet modules, so try data:.
      var blobUrl = URL.createObjectURL(new Blob([src], { type: 'application/javascript' }));
      var dataUrl = 'data:application/javascript;base64,' + btoa(src);
      ctx.audioWorklet.addModule(blobUrl).catch(function () {
        return ctx.audioWorklet.addModule(dataUrl);
      }).then(function () {
        var node = new AudioWorkletNode(ctx, 'port-audio', { numberOfInputs: 0, outputChannelCount: [2] });
        node.port.onmessage = function (e) {
          a.played = e.data[0]; a.dry = e.data[1]; a.reportedAt = performance.now();
        };
        node.connect(ctx.destination);
        a.node = node;
        a.reportedAt = performance.now();
        for (var i = 0; i < a.pending.length; i++) node.port.postMessage(a.pending[i], [a.pending[i].buffer]);
        a.pending = [];
      }, function (err) {
        console.error('[melee] audio: the AudioWorklet did not load:', err);
        a.failed = true;
        ctx.close(); // ax_hle.c falls back to SDL, which opens its own
      });
      // Autoplay: a context made before any gesture starts suspended. The
      // disc was chosen with a click, so this is usually already allowed.
      var resume = function () { if (ctx.state !== 'running') ctx.resume(); };
      resume();
      ['pointerdown', 'keydown', 'touchstart'].forEach(function (t) {
        window.addEventListener(t, resume, { capture: true });
      });
      return 1;
    } catch (err) {
      console.error('[melee] audio: no AudioWorklet output:', err);
      return 0;
    }
  },
  // 1 while the worklet is playing, 0 while it is loading or the context is
  // suspended (then the mixer runs on the wall clock, silently), -1 if it
  // failed for good.
  port_audio_running__sig: 'i',
  port_audio_running: function () {
    var a = Module.portAudio;
    if (!a || a.failed) return -1;
    return a.node && a.ctx.state === 'running' ? 1 : 0;
  },
  // Frames queued in the worklet, from its last report run forward by the
  // time since: the report is at most ~20 ms old.
  port_audio_queued_frames__sig: 'i',
  port_audio_queued_frames: function () {
    var a = Module.portAudio;
    if (!a || !a.node) return 0;
    var reported = a.posted - a.played;
    var since = (performance.now() - a.reportedAt) * a.rate / 1000;
    var q = reported - since;
    return q > 0 ? Math.floor(q) : 0;
  },
  // Frames of silence the worklet has played for want of data, in total.
  port_audio_dry_frames__sig: 'i',
  port_audio_dry_frames: function () {
    var a = Module.portAudio;
    return a ? a.dry >>> 0 : 0;
  },
  port_audio_push__sig: 'vii',
  port_audio_push: function (ptr, frames) {
    var a = Module.portAudio;
    if (!a) return;
    var n = frames * 2, buf = new Float32Array(n), base = ptr >> 1;
    for (var i = 0; i < n; i++) buf[i] = HEAP16[base + i] / 32768;
    a.posted += frames;
    if (a.node) {
      a.node.port.postMessage(buf, [buf.buffer]);
    } else {
      a.pending.push(buf);
      if (a.pending.length > 40) { a.posted -= a.pending.shift().length >> 1; }
    }
  },
  // The boot screen's pipeline preload (main_loop.c preload_pipelines):
  // boot.js draws it; returns 1 once the player has pressed Skip.
  port_preload_progress__sig: 'iiii',
  port_preload_progress: function (done, total, finished) {
    return Module.onPreload && Module.onPreload(done, total, !!finished) ? 1 : 0;
  },
  port_disc_size__sig: 'i',
  port_disc_size: function () {
    return Module.discSource ? Module.discSource.size >>> 0 : 0;
  },
});
