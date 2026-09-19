// Functions the wasm imports (declared in port/src/dvd_web/disc_io.h).
// Attached with `--js-library`. Module.discSource is set by boot.js before the
// runtime starts.
mergeInto(LibraryManager.library, {
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
  port_disc_size__sig: 'i',
  port_disc_size: function () {
    return Module.discSource ? Module.discSource.size >>> 0 : 0;
  },
});
