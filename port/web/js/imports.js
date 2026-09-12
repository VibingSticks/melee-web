// Functions the wasm imports (declared in port/src/dvd_web/disc_io.h).
// Attached with `--js-library`. Module.discSource is set by boot.js before the
// runtime starts.
mergeInto(LibraryManager.library, {
  port_disc_read__sig: 'viiiii',
  port_disc_read: function (offset, length, dst, cb, user) {
    Module.discSource.read(offset >>> 0, length >>> 0).then(
      function (buf) {
        HEAPU8.set(new Uint8Array(buf), dst);
        {{{ makeDynCall('vii', 'cb') }}}(user, 0);
      },
      function (err) {
        console.error('[melee] disc read failed:', err);
        {{{ makeDynCall('vii', 'cb') }}}(user, -1);
      });
  },
  port_disc_size__sig: 'i',
  port_disc_size: function () {
    return Module.discSource ? Module.discSource.size >>> 0 : 0;
  },
});
