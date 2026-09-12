// Hand-written stand-in for Aurora's GX shader under the compat profile:
// no immediates (per-draw data lives in the uniform block), vertex data pulled
// from rgba32uint textures with textureLoad, big-endian decoding in WGSL.
struct Uniform {
    render_viewport_size: vec2f,
    logical_viewport_size: vec2f,
    proj: mat4x4f,
    tex0_size_bias: vec4f,
    imm_vtx_start: u32,
    imm_current_pnmtx: u32,
    imm_array_start0: vec4u,
    imm_array_start1: vec4u,
    imm_array_start2: vec4u,
};
@group(0) @binding(0) var vbuf: texture_2d<u32>;
@group(0) @binding(1) var abuf: texture_2d<u32>;
@group(1) @binding(0) var<uniform> ubuf: Uniform;
@group(2) @binding(0) var tex0: texture_2d<f32>;
@group(2) @binding(1) var tex0_samp: sampler;

fn bswap32(v: u32, le: bool) -> u32 {
  if (le) { return v; }
  return ((v & 0x000000FFu) << 24u) | ((v & 0x0000FF00u) << 8u) |
         ((v & 0x00FF0000u) >> 8u) | ((v & 0xFF000000u) >> 24u);
}
fn bswap16(v: u32, le: bool) -> u32 {
  return select(((v & 0xFFu) << 8u) | (v >> 8u), v, le);
}
fn load_word(t: texture_2d<u32>, word_idx: u32) -> u32 {
  let texel = word_idx >> 2u;
  let v = textureLoad(t, vec2u(texel & 2047u, texel >> 11u), 0);
  return v[word_idx & 3u];
}
fn load_u8(t: texture_2d<u32>, byte_off: u32) -> u32 {
  let word = load_word(t, byte_off / 4u);
  return (word >> ((byte_off & 3u) * 8u)) & 0xFFu;
}
fn load_u32_raw(t: texture_2d<u32>, byte_off: u32) -> u32 {
  let word_idx = byte_off >> 2u;
  let sub = byte_off & 3u;
  let lo = load_word(t, word_idx);
  if (sub == 0u) { return lo; }
  let hi = load_word(t, word_idx + 1u);
  let shift = sub * 8u;
  return (lo >> shift) | (hi << (32u - shift));
}
fn load_u16(t: texture_2d<u32>, byte_off: u32, le: bool) -> u32 {
  let word_idx = byte_off >> 2u;
  let sub = byte_off & 3u;
  let word = load_word(t, word_idx);
  // no extractBits: GLSL ES 3.00 has no bitfieldExtract
  if (sub <= 2u) { return bswap16((word >> (sub * 8u)) & 0xFFFFu, le); }
  let next = load_word(t, word_idx + 1u);
  return bswap16((word >> 24u) | ((next & 0xFFu) << 8u), le);
}
fn load_f32(t: texture_2d<u32>, byte_off: u32, le: bool) -> f32 {
  return bitcast<f32>(bswap32(load_u32_raw(t, byte_off), le));
}

struct VertexOutput {
    @builtin(position) pos: vec4f,
    @location(0) uv: vec2f,
    @location(1) clr: vec4f,
};

@vertex
fn vs_main(@builtin(vertex_index) vidx: u32, @builtin(instance_index) iidx: u32) -> VertexOutput {
    var out: VertexOutput;
    let base = ubuf.imm_vtx_start + vidx * 12u;
    let pidx = load_u16(vbuf, base, false);
    let pbase = ubuf.imm_array_start0.x + pidx * 12u;
    let p = vec3f(load_f32(abuf, pbase, false), load_f32(abuf, pbase + 4u, false), load_f32(abuf, pbase + 8u, false));
    let s = f32(load_u16(vbuf, base + 2u, false)) / 256.0;
    let t = f32(load_u16(vbuf, base + 4u, false)) / 256.0;
    out.pos = vec4f(p + vec3f(f32(iidx), 0.0, 0.0), 1.0) * ubuf.proj;
    out.uv = vec2f(s, t);
    let c = load_u8(vbuf, base + 6u);
    out.clr = vec4f(f32(c) / 255.0, 0.0, 0.0, 1.0);
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let tex = textureSampleBias(tex0, tex0_samp, in.uv, ubuf.tex0_size_bias.z);
    var prev = tex * in.clr;
    if (prev.a < 0.5) { discard; }
    return vec4f(prev.rgb, 1.0);
}
