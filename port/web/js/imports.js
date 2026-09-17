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
  port_disc_size__sig: 'i',
  port_disc_size: function () {
    return Module.discSource ? Module.discSource.size >>> 0 : 0;
  },
});
