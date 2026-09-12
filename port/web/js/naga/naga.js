// Loader for naga.wasm (WGSL -> GLSL ES 3.00), used by the WebGL2 fallback.
//   const naga = await loadNaga('naga.wasm');     // or pass an ArrayBuffer / Uint8Array
//   const { glsl, uniforms, textures } = naga.translate(wgsl, 'vs_main', 'vertex');
// Throws an Error with naga's diagnostic text when the WGSL does not translate.

const STAGE = { vertex: 0, fragment: 1 };

export async function loadNaga(source) {
  let bytes;
  if (source instanceof ArrayBuffer || ArrayBuffer.isView(source)) {
    bytes = source;
  } else {
    bytes = await (await fetch(source)).arrayBuffer();
  }
  const { instance } = await WebAssembly.instantiate(bytes, {});
  const ex = instance.exports;
  const enc = new TextEncoder();
  const dec = new TextDecoder();
  const mem = () => new Uint8Array(ex.memory.buffer);

  function putString(s) {
    const b = enc.encode(s);
    const ptr = ex.nw_alloc(b.length || 1);
    mem().set(b, ptr);
    return { ptr, len: b.length };
  }

  return {
    translate(wgsl, entryPoint, stage) {
      const st = STAGE[stage];
      if (st === undefined) throw new Error('naga: stage must be "vertex" or "fragment"');
      const src = putString(wgsl);
      const ep = putString(entryPoint);
      const lenPtr = ex.nw_alloc(4);
      const out = ex.nw_translate(src.ptr, src.len, ep.ptr, ep.len, st, lenPtr);
      const len = new DataView(ex.memory.buffer).getUint32(lenPtr, true);
      const text = dec.decode(mem().subarray(out, out + len));
      ex.nw_free(out, len);
      ex.nw_free(lenPtr, 4);
      ex.nw_free(src.ptr, src.len || 1);
      ex.nw_free(ep.ptr, ep.len || 1);
      if (text[0] === 'E') throw new Error('naga: ' + text.slice(1));
      return JSON.parse(text);
    },
  };
}
