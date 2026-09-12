// gpu-gl2.js — the subset of the browser WebGPU API that emdawnwebgpu calls,
// implemented on WebGL2. See docs/superpowers/specs/2026-09-11-webgl2-fallback-design.md.
//
//   import { installWebGL2Fallback } from './gpu-gl2.js';
//   const naga = await loadNaga('naga/naga.wasm');
//   installWebGL2Fallback({ canvas, naga });      // defines navigator.gpu
//
// Conventions: the translator flips clip-space Y and remaps Z in the vertex stage, so a
// WebGPU row r lands on GL framebuffer row r of the attached texture. Texture data keeps
// WebGPU's row-0-is-top layout throughout, viewports, scissors and copies are not flipped,
// front faces are inverted (the Y flip reverses winding), and the canvas texture is blitted
// to the default framebuffer with a vertical flip at submit.
//
// Buffers with MAP_WRITE/MAP_READ (Aurora's staging buffers) live in JavaScript
// ArrayBuffers; copies out of them are uploads. Storage buffers are not offered
// (limits.maxStorageBuffersPerShaderStage = 0), immediates and compute are absent.

const BufferUsage = { MAP_READ: 1, MAP_WRITE: 2, COPY_SRC: 4, COPY_DST: 8, INDEX: 16, VERTEX: 32, UNIFORM: 64, STORAGE: 128, INDIRECT: 256, QUERY_RESOLVE: 512 };
const TextureUsage = { COPY_SRC: 1, COPY_DST: 2, TEXTURE_BINDING: 4, STORAGE_BINDING: 8, RENDER_ATTACHMENT: 16 };
const ShaderStage = { VERTEX: 1, FRAGMENT: 2, COMPUTE: 4 };
const MapMode = { READ: 1, WRITE: 2 };
const ColorWrite = { RED: 1, GREEN: 2, BLUE: 4, ALPHA: 8, ALL: 15 };

let nextId = 1;
const uid = () => nextId++;

// --- errors -------------------------------------------------------------------------
class GPUErrorImpl { constructor(message) { this.message = message; } }
class GPUValidationErrorImpl extends GPUErrorImpl {}
class GPUOutOfMemoryErrorImpl extends GPUErrorImpl {}
class GPUInternalErrorImpl extends GPUErrorImpl {}
class GPUUncapturedErrorEventImpl { constructor(error) { this.type = 'uncapturederror'; this.error = error; } }

function unsupported(what) {
  throw new Error(`gpu-gl2: ${what} is not supported by the WebGL2 fallback`);
}

// --- format tables --------------------------------------------------------------------
// WebGPU texture format -> [internalFormat, format, type, bytesPerTexel, kind]
function formatTable(gl) {
  return {
    'rgba8unorm': [gl.RGBA8, gl.RGBA, gl.UNSIGNED_BYTE, 4, 'color'],
    'rgba8unorm-srgb': [gl.SRGB8_ALPHA8, gl.RGBA, gl.UNSIGNED_BYTE, 4, 'color'],
    'bgra8unorm': [gl.RGBA8, gl.RGBA, gl.UNSIGNED_BYTE, 4, 'color'],
    'r8unorm': [gl.R8, gl.RED, gl.UNSIGNED_BYTE, 1, 'color'],
    'rg8unorm': [gl.RG8, gl.RG, gl.UNSIGNED_BYTE, 2, 'color'],
    'r16sint': [gl.R16I, gl.RED_INTEGER, gl.SHORT, 2, 'int'],
    'r16uint': [gl.R16UI, gl.RED_INTEGER, gl.UNSIGNED_SHORT, 2, 'int'],
    'r32uint': [gl.R32UI, gl.RED_INTEGER, gl.UNSIGNED_INT, 4, 'int'],
    'rgba32uint': [gl.RGBA32UI, gl.RGBA_INTEGER, gl.UNSIGNED_INT, 16, 'int'],
    'rgba16float': [gl.RGBA16F, gl.RGBA, gl.HALF_FLOAT, 8, 'color'],
    'r32float': [gl.R32F, gl.RED, gl.FLOAT, 4, 'color'],
    'rgba32float': [gl.RGBA32F, gl.RGBA, gl.FLOAT, 16, 'color'],
    'depth32float': [gl.DEPTH_COMPONENT32F, gl.DEPTH_COMPONENT, gl.FLOAT, 4, 'depth'],
    'depth24plus': [gl.DEPTH_COMPONENT24, gl.DEPTH_COMPONENT, gl.UNSIGNED_INT, 4, 'depth'],
    'depth24plus-stencil8': [gl.DEPTH24_STENCIL8, gl.DEPTH_STENCIL, gl.UNSIGNED_INT_24_8, 4, 'depth-stencil'],
  };
}

function vertexFormat(gl, f) {
  const t = {
    'float32': [1, gl.FLOAT, false], 'float32x2': [2, gl.FLOAT, false], 'float32x3': [3, gl.FLOAT, false], 'float32x4': [4, gl.FLOAT, false],
    'unorm8x2': [2, gl.UNSIGNED_BYTE, true], 'unorm8x4': [4, gl.UNSIGNED_BYTE, true], 'uint8x2': [2, gl.UNSIGNED_BYTE, false], 'uint8x4': [4, gl.UNSIGNED_BYTE, false],
    'snorm8x4': [4, gl.BYTE, true], 'sint8x4': [4, gl.BYTE, false],
    'unorm16x2': [2, gl.UNSIGNED_SHORT, true], 'unorm16x4': [4, gl.UNSIGNED_SHORT, true], 'sint16x2': [2, gl.SHORT, false], 'sint16x4': [4, gl.SHORT, false],
    'uint16x2': [2, gl.UNSIGNED_SHORT, false], 'uint16x4': [4, gl.UNSIGNED_SHORT, false], 'float16x2': [2, gl.HALF_FLOAT, false], 'float16x4': [4, gl.HALF_FLOAT, false],
    'uint32': [1, gl.UNSIGNED_INT, false], 'uint32x2': [2, gl.UNSIGNED_INT, false], 'sint32': [1, gl.INT, false], 'sint32x2': [2, gl.INT, false],
  }[f];
  if (!t) unsupported(`vertex format ${f}`);
  return t;
}

function compareFunc(gl, f) {
  return { never: gl.NEVER, less: gl.LESS, equal: gl.EQUAL, 'less-equal': gl.LEQUAL, greater: gl.GREATER, 'not-equal': gl.NOTEQUAL, 'greater-equal': gl.GEQUAL, always: gl.ALWAYS }[f ?? 'always'];
}

function blendFactor(gl, f) {
  const t = {
    zero: gl.ZERO, one: gl.ONE, src: gl.SRC_COLOR, 'one-minus-src': gl.ONE_MINUS_SRC_COLOR, 'src-alpha': gl.SRC_ALPHA, 'one-minus-src-alpha': gl.ONE_MINUS_SRC_ALPHA,
    dst: gl.DST_COLOR, 'one-minus-dst': gl.ONE_MINUS_DST_COLOR, 'dst-alpha': gl.DST_ALPHA, 'one-minus-dst-alpha': gl.ONE_MINUS_DST_ALPHA,
    'src-alpha-saturated': gl.SRC_ALPHA_SATURATE, constant: gl.CONSTANT_COLOR, 'one-minus-constant': gl.ONE_MINUS_CONSTANT_COLOR,
  }[f ?? 'one'];
  if (t === undefined) unsupported(`blend factor ${f}`);
  return t;
}

function blendOp(gl, o) {
  const t = { add: gl.FUNC_ADD, subtract: gl.FUNC_SUBTRACT, 'reverse-subtract': gl.FUNC_REVERSE_SUBTRACT, min: gl.MIN, max: gl.MAX }[o ?? 'add'];
  if (t === undefined) unsupported(`blend operation ${o}`);
  return t;
}

function topology(gl, t) {
  return { 'point-list': gl.POINTS, 'line-list': gl.LINES, 'line-strip': gl.LINE_STRIP, 'triangle-list': gl.TRIANGLES, 'triangle-strip': gl.TRIANGLE_STRIP }[t ?? 'triangle-list'];
}

function sizeOf(size) {
  if (Array.isArray(size)) return { width: size[0], height: size[1] ?? 1, depthOrArrayLayers: size[2] ?? 1 };
  return { width: size.width, height: size.height ?? 1, depthOrArrayLayers: size.depthOrArrayLayers ?? 1 };
}

function originOf(o) {
  if (!o) return { x: 0, y: 0, z: 0 };
  if (Array.isArray(o)) return { x: o[0] ?? 0, y: o[1] ?? 0, z: o[2] ?? 0 };
  return { x: o.x ?? 0, y: o.y ?? 0, z: o.z ?? 0 };
}

// --- install -----------------------------------------------------------------------------
export function installWebGL2Fallback({ canvas, naga, force = false }) {
  if ('gpu' in navigator && !force) return navigator.gpu;
  const gl = canvas.getContext('webgl2', { antialias: false, depth: false, stencil: false, alpha: false, premultipliedAlpha: false, preserveDrawingBuffer: false, powerPreference: 'high-performance' });
  if (!gl) throw new Error('gpu-gl2: WebGL2 is not available');
  const extFloat = gl.getExtension('EXT_color_buffer_float');
  gl.getExtension('OES_texture_float_linear');
  gl.getExtension('EXT_float_blend');
  const extAniso = gl.getExtension('EXT_texture_filter_anisotropic');
  const FMT = formatTable(gl);
  const maxAniso = extAniso ? gl.getParameter(extAniso.MAX_TEXTURE_MAX_ANISOTROPY_EXT) : 1;

  // Globals emdawnwebgpu expects to exist. A browser with no WebGPU at all (Firefox on Linux)
  // has none of them, so each is defined only when missing rather than as one block: where the
  // browser does have WebGPU we must keep its own classes, because emdawnwebgpu's error
  // handling does `instanceof` against the natives.
  const g = globalThis;
  const define = (name, value) => { if (typeof g[name] === 'undefined') g[name] = value; };
  define('GPUValidationError', GPUValidationErrorImpl);
  define('GPUOutOfMemoryError', GPUOutOfMemoryErrorImpl);
  define('GPUInternalError', GPUInternalErrorImpl);
  define('GPUUncapturedErrorEvent', GPUUncapturedErrorEventImpl);
  define('GPUBufferUsage', BufferUsage);
  define('GPUTextureUsage', TextureUsage);
  define('GPUShaderStage', ShaderStage);
  define('GPUMapMode', MapMode);
  define('GPUColorWrite', ColorWrite);
  const ValidationError = g.GPUValidationError;

  const limits = {
    maxTextureDimension1D: gl.getParameter(gl.MAX_TEXTURE_SIZE), maxTextureDimension2D: gl.getParameter(gl.MAX_TEXTURE_SIZE),
    maxTextureDimension3D: gl.getParameter(gl.MAX_3D_TEXTURE_SIZE), maxTextureArrayLayers: gl.getParameter(gl.MAX_ARRAY_TEXTURE_LAYERS),
    maxBindGroups: 4, maxBindGroupsPlusVertexBuffers: 24, maxBindingsPerBindGroup: 1000,
    maxDynamicUniformBuffersPerPipelineLayout: 8, maxDynamicStorageBuffersPerPipelineLayout: 0,
    maxSampledTexturesPerShaderStage: Math.min(16, gl.getParameter(gl.MAX_TEXTURE_IMAGE_UNITS)), maxSamplersPerShaderStage: 16,
    maxStorageBuffersPerShaderStage: 0, maxStorageTexturesPerShaderStage: 0, maxUniformBuffersPerShaderStage: 12,
    maxUniformBufferBindingSize: gl.getParameter(gl.MAX_UNIFORM_BLOCK_SIZE), maxStorageBufferBindingSize: 0,
    minUniformBufferOffsetAlignment: Math.max(256, gl.getParameter(gl.UNIFORM_BUFFER_OFFSET_ALIGNMENT)), minStorageBufferOffsetAlignment: 256,
    maxVertexBuffers: 8, maxBufferSize: 268435456, maxVertexAttributes: 16, maxVertexBufferArrayStride: 2048,
    maxInterStageShaderVariables: 16, maxColorAttachments: 8, maxColorAttachmentBytesPerSample: 32,
    maxComputeWorkgroupStorageSize: 0, maxComputeInvocationsPerWorkgroup: 0, maxComputeWorkgroupSizeX: 0, maxComputeWorkgroupSizeY: 0,
    maxComputeWorkgroupSizeZ: 0, maxComputeWorkgroupsPerDimension: 0, maxImmediateSize: 0,
    // Compatibility-mode limits: emdawnwebgpu reads these only when GPUSupportedLimits.prototype
    // carries the names, which is why the class below declares every key.
    maxStorageBuffersInVertexStage: 0, maxStorageTexturesInVertexStage: 0,
    maxStorageBuffersInFragmentStage: 0, maxStorageTexturesInFragmentStage: 0,
  };
  // emdawnwebgpu feature-detects compatibility-mode limits with
  // `"maxStorageBuffersInVertexStage" in GPUSupportedLimits.prototype`, referencing the class
  // as a bare global. Firefox (no WebGPU) does not define it, which throws a ReferenceError
  // before the adapter is ever returned, so publish a stand-in carrying every limit name.
  class GPUSupportedLimitsImpl {}
  for (const name of Object.keys(limits)) GPUSupportedLimitsImpl.prototype[name] = undefined;
  define('GPUSupportedLimits', GPUSupportedLimitsImpl);

  const features = new Set(extFloat ? ['float32-filterable'] : []);
  const info = { vendor: 'webgl2-fallback', architecture: '', device: String(gl.getParameter(gl.RENDERER) || ''), description: 'WebGPU subset on WebGL2 (gpu-gl2.js)' };

  // ----- device-wide state -----
  let device = null;
  const errorScopes = [];
  function raise(err) {
    if (errorScopes.length) { errorScopes[errorScopes.length - 1].error ??= err; return; }
    if (device?.onuncapturederror) device.onuncapturederror(new g.GPUUncapturedErrorEvent(err));
    else console.error('gpu-gl2:', err.message);
  }

  const defaultSampler = gl.createSampler(); // for textures bound without a sampler (integer data textures)
  gl.samplerParameteri(defaultSampler, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.samplerParameteri(defaultSampler, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
  gl.samplerParameteri(defaultSampler, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.samplerParameteri(defaultSampler, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  const vao = gl.createVertexArray();
  gl.bindVertexArray(vao);
  const readFbo = gl.createFramebuffer();
  const drawFbo = gl.createFramebuffer();

  // ----- buffers -----
  class GPUBufferImpl {
    constructor(desc) {
      this.id = uid(); this.label = desc.label ?? ''; this.size = desc.size; this.usage = desc.usage; this.mapState = 'unmapped';
      this.host = (desc.usage & (BufferUsage.MAP_WRITE | BufferUsage.MAP_READ)) !== 0;
      this.glTarget = (desc.usage & BufferUsage.INDEX) ? gl.ELEMENT_ARRAY_BUFFER : (desc.usage & BufferUsage.UNIFORM) ? gl.UNIFORM_BUFFER : gl.ARRAY_BUFFER;
      if (this.host) {
        this.backing = new ArrayBuffer(desc.size);
      } else {
        this.gl = gl.createBuffer();
        gl.bindBuffer(this.glTarget, this.gl);
        gl.bufferData(this.glTarget, desc.size, (desc.usage & BufferUsage.UNIFORM) ? gl.DYNAMIC_DRAW : gl.STATIC_DRAW);
      }
      this.mapping = null;
      if (desc.mappedAtCreation) { this.mapState = 'mapped'; this.mapping = { offset: 0, size: desc.size, ab: this.host ? this.backing : new ArrayBuffer(desc.size) }; }
    }
    mapAsync(mode, offset = 0, size = this.size - offset) {
      this.mapState = 'pending';
      if (!this.host) {
        if (mode & MapMode.READ) { // readback (rare): pull the bytes now
          const ab = new ArrayBuffer(size);
          gl.bindBuffer(this.glTarget, this.gl);
          gl.getBufferSubData(this.glTarget, offset, new Uint8Array(ab));
          this.mapping = { offset, size, ab, readonly: true };
        } else {
          this.mapping = { offset, size, ab: new ArrayBuffer(size) };
        }
      } else {
        this.mapping = { offset, size, ab: (offset === 0 && size === this.size) ? this.backing : new ArrayBuffer(size) };
      }
      return new Promise(res => setTimeout(() => { this.mapState = 'mapped'; res(); }, 0));
    }
    getMappedRange(offset = 0, size) {
      const m = this.mapping;
      if (!m) throw new ValidationError('getMappedRange on an unmapped buffer');
      size ??= m.size - (offset - m.offset);
      if (offset === m.offset && size === m.size) return m.ab;
      // A sub-range of the mapped range: alias by tracking (offset, view) and folding back at unmap.
      const ab = new ArrayBuffer(size);
      (m.sub ??= []).push({ offset: offset - m.offset, ab });
      return ab;
    }
    unmap() {
      const m = this.mapping;
      if (!m) return;
      if (m.sub) for (const s of m.sub) new Uint8Array(m.ab, s.offset, s.ab.byteLength).set(new Uint8Array(s.ab));
      if (!m.readonly) {
        if (this.host) {
          if (m.ab !== this.backing) new Uint8Array(this.backing, m.offset, m.size).set(new Uint8Array(m.ab));
        } else {
          gl.bindBuffer(this.glTarget, this.gl);
          gl.bufferSubData(this.glTarget, m.offset, new Uint8Array(m.ab));
        }
      }
      this.mapping = null; this.mapState = 'unmapped';
    }
    destroy() { if (this.gl) { gl.deleteBuffer(this.gl); this.gl = null; } this.backing = null; }
  }

  // ----- textures, views, samplers -----
  class GPUTextureViewImpl {
    constructor(texture, desc = {}) {
      this.id = uid(); this.texture = texture; this.label = desc.label ?? '';
      this.format = desc.format ?? texture.format; this.baseMipLevel = desc.baseMipLevel ?? 0; this.mipLevelCount = desc.mipLevelCount ?? (texture.mipLevelCount - this.baseMipLevel);
      this.baseArrayLayer = desc.baseArrayLayer ?? 0; this.aspect = desc.aspect ?? 'all';
    }
  }
  class GPUTextureImpl {
    constructor(desc) {
      this.id = uid(); this.label = desc.label ?? '';
      const s = sizeOf(desc.size); this.width = s.width; this.height = s.height; this.depthOrArrayLayers = s.depthOrArrayLayers;
      this.format = desc.format; this.usage = desc.usage; this.dimension = desc.dimension ?? '2d'; this.mipLevelCount = desc.mipLevelCount ?? 1; this.sampleCount = desc.sampleCount ?? 1;
      const f = FMT[desc.format];
      if (!f) unsupported(`texture format ${desc.format}`);
      if (this.dimension !== '2d' || this.depthOrArrayLayers !== 1) unsupported('non-2D or array textures');
      this.fmt = f;
      if (this.sampleCount > 1) {
        this.rb = gl.createRenderbuffer();
        gl.bindRenderbuffer(gl.RENDERBUFFER, this.rb);
        gl.renderbufferStorageMultisample(gl.RENDERBUFFER, this.sampleCount, f[0], this.width, this.height);
      } else {
        this.gl = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, this.gl);
        gl.texStorage2D(gl.TEXTURE_2D, this.mipLevelCount, f[0], this.width, this.height);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, this.mipLevelCount > 1 ? gl.NEAREST_MIPMAP_NEAREST : gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAX_LEVEL, this.mipLevelCount - 1);
      }
      this.fbos = new Map();
    }
    createView(desc) { return new GPUTextureViewImpl(this, desc); }
    destroy() { if (this.gl) gl.deleteTexture(this.gl); if (this.rb) gl.deleteRenderbuffer(this.rb); for (const f of this.fbos.values()) gl.deleteFramebuffer(f); this.gl = this.rb = null; this.fbos.clear(); }
  }
  class GPUSamplerImpl {
    constructor(desc = {}) {
      this.id = uid(); this.label = desc.label ?? '';
      const s = this.gl = gl.createSampler();
      const wrap = (m) => ({ 'clamp-to-edge': gl.CLAMP_TO_EDGE, repeat: gl.REPEAT, 'mirror-repeat': gl.MIRRORED_REPEAT })[m ?? 'clamp-to-edge'];
      gl.samplerParameteri(s, gl.TEXTURE_WRAP_S, wrap(desc.addressModeU));
      gl.samplerParameteri(s, gl.TEXTURE_WRAP_T, wrap(desc.addressModeV));
      gl.samplerParameteri(s, gl.TEXTURE_MAG_FILTER, desc.magFilter === 'linear' ? gl.LINEAR : gl.NEAREST);
      const min = desc.minFilter === 'linear', mip = desc.mipmapFilter === 'linear';
      const hasMip = desc.lodMaxClamp === undefined || desc.lodMaxClamp > 0;
      gl.samplerParameteri(s, gl.TEXTURE_MIN_FILTER, hasMip ? (min ? (mip ? gl.LINEAR_MIPMAP_LINEAR : gl.LINEAR_MIPMAP_NEAREST) : (mip ? gl.NEAREST_MIPMAP_LINEAR : gl.NEAREST_MIPMAP_NEAREST)) : (min ? gl.LINEAR : gl.NEAREST));
      gl.samplerParameterf(s, gl.TEXTURE_MIN_LOD, desc.lodMinClamp ?? 0);
      gl.samplerParameterf(s, gl.TEXTURE_MAX_LOD, desc.lodMaxClamp ?? 32);
      if (desc.compare) { gl.samplerParameteri(s, gl.TEXTURE_COMPARE_MODE, gl.COMPARE_REF_TO_TEXTURE); gl.samplerParameteri(s, gl.TEXTURE_COMPARE_FUNC, compareFunc(gl, desc.compare)); }
      if (extAniso && (desc.maxAnisotropy ?? 1) > 1) gl.samplerParameterf(s, extAniso.TEXTURE_MAX_ANISOTROPY_EXT, Math.min(desc.maxAnisotropy, maxAniso));
    }
  }

  // Framebuffer for a (color view, depth view) pair, cached on the color texture (or depth texture).
  function fboFor(colorView, depthView) {
    const owner = colorView?.texture ?? depthView?.texture;
    const key = `${colorView?.id ?? 0}|${colorView?.baseMipLevel ?? 0}|${depthView?.id ?? 0}`;
    let fbo = owner.fbos.get(key);
    if (fbo) return fbo;
    fbo = gl.createFramebuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
    if (colorView) attach(gl.COLOR_ATTACHMENT0, colorView);
    if (depthView) attach(depthView.texture.fmt[4] === 'depth-stencil' ? gl.DEPTH_STENCIL_ATTACHMENT : gl.DEPTH_ATTACHMENT, depthView);
    const status = gl.checkFramebufferStatus(gl.FRAMEBUFFER);
    if (status !== gl.FRAMEBUFFER_COMPLETE) throw new Error(`gpu-gl2: framebuffer incomplete (0x${status.toString(16)}) for ${colorView?.format}/${depthView?.format}`);
    owner.fbos.set(key, fbo);
    return fbo;
  }
  function attach(point, view) {
    const t = view.texture;
    if (t.rb) gl.framebufferRenderbuffer(gl.FRAMEBUFFER, point, gl.RENDERBUFFER, t.rb);
    else gl.framebufferTexture2D(gl.FRAMEBUFFER, point, gl.TEXTURE_2D, t.gl, view.baseMipLevel);
  }

  // ----- bind group layouts / groups / shader modules -----
  class GPUBindGroupLayoutImpl { constructor(desc) { this.id = uid(); this.label = desc.label ?? ''; this.entries = desc.entries.map(e => ({ ...e })); } }
  class GPUPipelineLayoutImpl { constructor(desc) { this.id = uid(); this.label = desc.label ?? ''; this.bindGroupLayouts = [...desc.bindGroupLayouts]; } }
  class GPUBindGroupImpl {
    constructor(desc) {
      this.id = uid(); this.label = desc.label ?? ''; this.layout = desc.layout; this.entries = new Map();
      for (const e of desc.entries) this.entries.set(e.binding, e.resource);
    }
  }
  class GPUShaderModuleImpl {
    constructor(desc) { this.id = uid(); this.label = desc.label ?? ''; this.code = desc.code; this.cache = new Map(); }
    getCompilationInfo() { return Promise.resolve({ messages: [] }); }
    translate(entryPoint, stage) {
      const key = stage + ':' + entryPoint;
      let r = this.cache.get(key);
      if (!r) { r = naga.translate(this.code, entryPoint, stage); this.cache.set(key, r); }
      return r;
    }
  }

  // ----- render pipelines -----
  const programCache = new Map();
  function compileProgram(vsMod, vsEntry, fsMod, fsEntry) {
    const key = `${vsMod.id}:${vsEntry}|${fsMod?.id ?? 0}:${fsEntry ?? ''}`;
    let p = programCache.get(key);
    if (p) return p;
    const vs = vsMod.translate(vsEntry ?? 'vs_main', 'vertex');
    const fs = fsMod ? fsMod.translate(fsEntry ?? 'fs_main', 'fragment') : null;
    const program = gl.createProgram();
    for (const [type, src] of [[gl.VERTEX_SHADER, vs.glsl], [gl.FRAGMENT_SHADER, fs ? fs.glsl : '#version 300 es\nvoid main(){}']]) {
      const sh = gl.createShader(type);
      gl.shaderSource(sh, src); gl.compileShader(sh);
      if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) throw new Error(`gpu-gl2: shader compile failed (${vsMod.label}): ${gl.getShaderInfoLog(sh)}\n${src}`);
      gl.attachShader(program, sh);
    }
    gl.linkProgram(program);
    if (!gl.getProgramParameter(program, gl.LINK_STATUS)) throw new Error(`gpu-gl2: program link failed: ${gl.getProgramInfoLog(program)}`);
    // Resource bindings: (group, binding) -> uniform block binding point / texture unit.
    gl.useProgram(program);
    const ubos = new Map(); // key g:b -> point
    const texUnits = new Map(); // key g:b -> unit
    let nextPoint = 0, nextUnit = 0;
    for (const r of [vs, fs].filter(Boolean)) {
      for (const u of r.uniforms) {
        const idx = gl.getUniformBlockIndex(program, u.name);
        if (idx === gl.INVALID_INDEX) continue;
        const k = `${u.group}:${u.binding}`;
        if (!ubos.has(k)) ubos.set(k, nextPoint++);
        gl.uniformBlockBinding(program, idx, ubos.get(k));
      }
      for (const t of r.textures) {
        const loc = gl.getUniformLocation(program, t.name);
        if (!loc) continue;
        const k = `${t.texture[0]}:${t.texture[1]}`;
        if (!texUnits.has(k)) texUnits.set(k, { unit: nextUnit++, sampler: t.sampler[0] === 4294967295 ? null : `${t.sampler[0]}:${t.sampler[1]}` });
        gl.uniform1i(loc, texUnits.get(k).unit);
      }
    }
    p = { program, ubos, texUnits };
    programCache.set(key, p);
    return p;
  }

  class GPURenderPipelineImpl {
    constructor(desc) {
      this.id = uid(); this.label = desc.label ?? ''; this.layout = desc.layout;
      const prog = compileProgram(desc.vertex.module, desc.vertex.entryPoint, desc.fragment?.module, desc.fragment?.entryPoint);
      this.program = prog.program; this.ubos = prog.ubos; this.texUnits = prog.texUnits;
      this.buffers = (desc.vertex.buffers ?? []).map(b => b && ({ arrayStride: b.arrayStride, stepMode: b.stepMode ?? 'vertex', attributes: b.attributes.map(a => ({ ...vertexFormat(gl, a.format), offset: a.offset, location: a.shaderLocation })) }));
      const prim = desc.primitive ?? {};
      this.mode = topology(gl, prim.topology);
      this.cull = prim.cullMode ?? 'none';
      // The translator flips Y, which inverts winding: swap the front face.
      this.frontFace = (prim.frontFace ?? 'ccw') === 'ccw' ? gl.CW : gl.CCW;
      const ds = desc.depthStencil;
      this.depth = ds ? { test: (ds.depthCompare ?? 'always') !== 'always' || !!ds.depthWriteEnabled, func: compareFunc(gl, ds.depthCompare), write: !!ds.depthWriteEnabled, bias: ds.depthBias ?? 0, slope: ds.depthBiasSlopeScale ?? 0 } : null;
      const target = desc.fragment?.targets?.[0];
      this.blend = target?.blend ? { c: target.blend.color ?? {}, a: target.blend.alpha ?? {} } : null;
      this.writeMask = target?.writeMask ?? ColorWrite.ALL;
      this.sampleCount = desc.multisample?.count ?? 1;
      this.indexFormat = prim.stripIndexFormat;
    }
    getBindGroupLayout(i) { return this.layout.bindGroupLayouts[i]; }
  }

  // ----- command recording -----
  class GPUCommandBufferImpl { constructor(cmds) { this.cmds = cmds; } }

  class GPURenderPassEncoderImpl {
    constructor(cmds, desc) {
      this.cmds = cmds;
      const ca = desc.colorAttachments?.[0] ?? null;
      const da = desc.depthStencilAttachment ?? null;
      const colorView = ca?.view ?? null, depthView = da?.view ?? null;
      const owner = colorView?.texture ?? depthView?.texture;
      const W = owner.width, H = owner.height;
      this.H = H;
      cmds.push(() => {
        const fbo = fboFor(colorView, depthView);
        gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
        gl.disable(gl.SCISSOR_TEST);
        gl.colorMask(true, true, true, true); gl.depthMask(true);
        if (ca && ca.loadOp === 'clear') {
          const c = ca.clearValue ?? { r: 0, g: 0, b: 0, a: 0 };
          const v = Array.isArray(c) ? c : [c.r, c.g, c.b, c.a];
          if (colorView.texture.fmt[4] === 'int') gl.clearBufferuiv(gl.COLOR, 0, new Uint32Array(v.map(x => x >>> 0)));
          else gl.clearBufferfv(gl.COLOR, 0, new Float32Array(v));
        }
        if (da && da.depthLoadOp === 'clear') gl.clearBufferfv(gl.DEPTH, 0, new Float32Array([da.depthClearValue ?? 1]));
        gl.viewport(0, 0, W, H); gl.depthRange(0, 1);
        gl.enable(gl.SCISSOR_TEST); gl.scissor(0, 0, W, H);
        state.reset();
        if (colorView?.texture.isCanvas) colorView.texture.dirty = true;
      });
      this.resolve = ca?.resolveTarget ? { src: colorView, dst: ca.resolveTarget } : null;
    }
    setPipeline(p) { this.cmds.push(() => state.setPipeline(p)); }
    setBindGroup(index, group, dynamicOffsets, start = 0, count) {
      let offs = dynamicOffsets ?? [];
      if (count !== undefined) offs = Array.from(offs.slice(start, start + count));
      else if (!Array.isArray(offs)) offs = Array.from(offs);
      this.cmds.push(() => state.setBindGroup(index, group, offs));
    }
    setIndexBuffer(buffer, format, offset = 0, size) { this.cmds.push(() => state.setIndexBuffer(buffer, format, offset)); }
    setVertexBuffer(slot, buffer, offset = 0, size) { this.cmds.push(() => state.setVertexBuffer(slot, buffer, offset)); }
    // The vertex stage already flips Y, so WebGPU row r is GL framebuffer row r: no flip here.
    setViewport(x, y, w, h, minDepth, maxDepth) { this.cmds.push(() => { gl.viewport(x | 0, y | 0, w | 0, h | 0); gl.depthRange(minDepth, maxDepth); }); }
    setScissorRect(x, y, w, h) { this.cmds.push(() => gl.scissor(x, y, w, h)); }
    setBlendConstant(c) { const v = Array.isArray(c) ? c : [c.r, c.g, c.b, c.a]; this.cmds.push(() => gl.blendColor(v[0], v[1], v[2], v[3])); }
    setStencilReference() {}
    draw(vertexCount, instanceCount = 1, firstVertex = 0, firstInstance = 0) {
      this.cmds.push(() => { state.flush(); gl.drawArraysInstanced(state.pipeline.mode, firstVertex, vertexCount, instanceCount); });
    }
    drawIndexed(indexCount, instanceCount = 1, firstIndex = 0, baseVertex = 0, firstInstance = 0) {
      this.cmds.push(() => {
        state.flush();
        const fmt = state.indexFormat === 'uint32' ? gl.UNSIGNED_INT : gl.UNSIGNED_SHORT;
        const isz = state.indexFormat === 'uint32' ? 4 : 2;
        if (baseVertex !== 0) unsupported('drawIndexed with baseVertex');
        gl.drawElementsInstanced(state.pipeline.mode, indexCount, fmt, state.indexOffset + firstIndex * isz, instanceCount);
      });
    }
    pushDebugGroup() {} popDebugGroup() {} insertDebugMarker() {}
    beginOcclusionQuery() { unsupported('occlusion queries'); }
    executeBundles() { unsupported('render bundles'); }
    drawIndirect() { unsupported('indirect draws'); } drawIndexedIndirect() { unsupported('indirect draws'); }
    end() {
      if (this.resolve) {
        const { src, dst } = this.resolve;
        this.cmds.push(() => blit(src, dst, 0, 0, 0, 0, src.texture.width, src.texture.height, gl.COLOR_BUFFER_BIT));
      }
      this.cmds.push(() => { gl.bindFramebuffer(gl.FRAMEBUFFER, null); });
    }
  }

  // Draw-time state application (cached).
  const state = {
    pipeline: null, groups: [], indexBuffer: null, indexFormat: 'uint16', indexOffset: 0, vertexBuffers: [], dirtyGroups: 0, dirtyVerts: false, dirtyIndex: false,
    reset() { this.pipeline = null; this.groups = []; this.dirtyGroups = 0xF; this.dirtyVerts = true; this.dirtyIndex = true; this.lastProgram = null; },
    setPipeline(p) { this.pipeline = p; this.dirtyGroups = 0xF; this.dirtyVerts = true; },
    setBindGroup(i, group, offs) { this.groups[i] = { group, offs }; this.dirtyGroups |= 1 << i; },
    setIndexBuffer(b, f, o) { this.indexBuffer = b; this.indexFormat = f; this.indexOffset = o; this.dirtyIndex = true; },
    setVertexBuffer(slot, b, o) { this.vertexBuffers[slot] = { b, o }; this.dirtyVerts = true; },
    flush() {
      const p = this.pipeline;
      if (!p) throw new Error('gpu-gl2: draw without a pipeline');
      if (this.lastProgram !== p) { applyPipeline(p); this.lastProgram = p; }
      if (this.dirtyGroups) {
        for (let i = 0; i < 4; i++) if ((this.dirtyGroups >> i) & 1 && this.groups[i]) applyBindGroup(p, i, this.groups[i].group, this.groups[i].offs);
        this.dirtyGroups = 0;
      }
      if (this.dirtyIndex) { gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.indexBuffer?.gl ?? null); this.dirtyIndex = false; }
      if (this.dirtyVerts) { applyVertexBuffers(p, this.vertexBuffers); this.dirtyVerts = false; }
    },
  };

  let enabledAttribs = 0;
  function applyVertexBuffers(p, vbs) {
    let used = 0;
    p.buffers.forEach((layout, slot) => {
      const vb = vbs[slot];
      if (!layout || !vb) return;
      gl.bindBuffer(gl.ARRAY_BUFFER, vb.b.gl);
      for (const a of layout.attributes) {
        gl.enableVertexAttribArray(a.location);
        gl.vertexAttribPointer(a.location, a[0], a[1], a[2], layout.arrayStride, vb.o + a.offset);
        gl.vertexAttribDivisor(a.location, layout.stepMode === 'instance' ? 1 : 0);
        used |= 1 << a.location;
      }
    });
    for (let i = 0; i < 16; i++) if ((enabledAttribs >> i) & 1 && !((used >> i) & 1)) gl.disableVertexAttribArray(i);
    enabledAttribs = used;
  }

  function applyPipeline(p) {
    gl.useProgram(p.program);
    if (p.cull === 'none') gl.disable(gl.CULL_FACE); else { gl.enable(gl.CULL_FACE); gl.cullFace(p.cull === 'front' ? gl.FRONT : gl.BACK); }
    gl.frontFace(p.frontFace);
    if (p.depth && p.depth.test) { gl.enable(gl.DEPTH_TEST); gl.depthFunc(p.depth.func); gl.depthMask(p.depth.write); } else { gl.disable(gl.DEPTH_TEST); gl.depthMask(p.depth ? p.depth.write : false); }
    if (p.depth && (p.depth.bias || p.depth.slope)) { gl.enable(gl.POLYGON_OFFSET_FILL); gl.polygonOffset(p.depth.slope, p.depth.bias); } else gl.disable(gl.POLYGON_OFFSET_FILL);
    if (p.blend) {
      gl.enable(gl.BLEND);
      gl.blendEquationSeparate(blendOp(gl, p.blend.c.operation), blendOp(gl, p.blend.a.operation));
      gl.blendFuncSeparate(blendFactor(gl, p.blend.c.srcFactor), blendFactor(gl, p.blend.c.dstFactor ?? 'zero'), blendFactor(gl, p.blend.a.srcFactor), blendFactor(gl, p.blend.a.dstFactor ?? 'zero'));
    } else gl.disable(gl.BLEND);
    const m = p.writeMask;
    gl.colorMask(!!(m & 1), !!(m & 2), !!(m & 4), !!(m & 8));
  }

  function applyBindGroup(p, groupIndex, group, dynamicOffsets) {
    let dyn = 0;
    for (const e of group.layout.entries) {
      const res = group.entries.get(e.binding);
      if (res === undefined) continue;
      const k = `${groupIndex}:${e.binding}`;
      if (e.buffer) {
        const point = p.ubos.get(k);
        const off = (res.offset ?? 0) + (e.buffer.hasDynamicOffset ? (dynamicOffsets[dyn++] ?? 0) : 0);
        if (point === undefined) continue;
        if (e.buffer.type === 'storage' || e.buffer.type === 'read-only-storage') unsupported('storage buffer bindings');
        const size = res.size ?? (res.buffer.size - off);
        gl.bindBufferRange(gl.UNIFORM_BUFFER, point, res.buffer.gl, off, Math.min(size, res.buffer.size - off));
      } else if (e.texture) {
        const tu = p.texUnits.get(k);
        if (!tu) continue;
        gl.activeTexture(gl.TEXTURE0 + tu.unit);
        gl.bindTexture(gl.TEXTURE_2D, res.texture.gl);
        let sampler = defaultSampler;
        if (tu.sampler) {
          const [sg, sb] = tu.sampler.split(':').map(Number);
          const sres = state.groups[sg]?.group.entries.get(sb);
          if (sres?.gl) sampler = sres.gl;
        }
        gl.bindSampler(tu.unit, sampler);
      }
      // sampler entries are applied through the texture they pair with
    }
  }

  function blit(srcView, dstView, sx, sy, dx, dy, w, h, mask) {
    gl.bindFramebuffer(gl.READ_FRAMEBUFFER, readFbo);
    const rp = mask === gl.COLOR_BUFFER_BIT ? gl.COLOR_ATTACHMENT0 : gl.DEPTH_ATTACHMENT;
    if (srcView.texture.rb) gl.framebufferRenderbuffer(gl.READ_FRAMEBUFFER, rp, gl.RENDERBUFFER, srcView.texture.rb);
    else gl.framebufferTexture2D(gl.READ_FRAMEBUFFER, rp, gl.TEXTURE_2D, srcView.texture.gl, srcView.baseMipLevel);
    gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, drawFbo);
    if (dstView.texture.rb) gl.framebufferRenderbuffer(gl.DRAW_FRAMEBUFFER, rp, gl.RENDERBUFFER, dstView.texture.rb);
    else gl.framebufferTexture2D(gl.DRAW_FRAMEBUFFER, rp, gl.TEXTURE_2D, dstView.texture.gl, dstView.baseMipLevel);
    gl.disable(gl.SCISSOR_TEST);
    gl.blitFramebuffer(sx, sy, sx + w, sy + h, dx, dy, dx + w, dy + h, mask, gl.NEAREST);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  }

  class GPUCommandEncoderImpl {
    constructor(desc) { this.label = desc?.label ?? ''; this.cmds = []; }
    beginRenderPass(desc) { return new GPURenderPassEncoderImpl(this.cmds, desc); }
    beginComputePass() { unsupported('compute passes'); }
    copyBufferToBuffer(src, srcOffset, dst, dstOffset, size) {
      // WebGPU also allows (src, dst, size) and (src, srcOffset, dst, dstOffset) forms.
      if (typeof srcOffset === 'object') { size = dst; dst = srcOffset; srcOffset = 0; dstOffset = 0; }
      size ??= Math.min(src.size - srcOffset, dst.size - dstOffset);
      this.cmds.push(() => {
        if (src.host && !dst.host) {
          gl.bindBuffer(dst.glTarget, dst.gl);
          gl.bufferSubData(dst.glTarget, dstOffset, new Uint8Array(src.backing, srcOffset, size));
        } else if (src.host && dst.host) {
          new Uint8Array(dst.backing, dstOffset, size).set(new Uint8Array(src.backing, srcOffset, size));
        } else if (!src.host && !dst.host) {
          gl.bindBuffer(gl.COPY_READ_BUFFER, src.gl); gl.bindBuffer(gl.COPY_WRITE_BUFFER, dst.gl);
          gl.copyBufferSubData(gl.COPY_READ_BUFFER, gl.COPY_WRITE_BUFFER, srcOffset, dstOffset, size);
        } else {
          gl.bindBuffer(src.glTarget, src.gl);
          gl.getBufferSubData(src.glTarget, srcOffset, new Uint8Array(dst.backing, dstOffset, size));
        }
      });
    }
    copyBufferToTexture(src, dst, copySize) {
      const size = sizeOf(copySize), origin = originOf(dst.origin), tex = dst.texture, level = dst.mipLevel ?? 0;
      const [, format, type, bpp] = tex.fmt;
      const bytesPerRow = src.bytesPerRow ?? size.width * bpp;
      const offset = src.offset ?? 0;
      this.cmds.push(() => {
        gl.bindTexture(gl.TEXTURE_2D, tex.gl);
        gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
        gl.pixelStorei(gl.UNPACK_ROW_LENGTH, bytesPerRow / bpp);
        if (src.buffer.host) {
          gl.bindBuffer(gl.PIXEL_UNPACK_BUFFER, null);
          const view = arrayViewFor(type, src.buffer.backing, offset, size.height * bytesPerRow);
          gl.texSubImage2D(gl.TEXTURE_2D, level, origin.x, origin.y, size.width, size.height, format, type, view);
        } else {
          gl.bindBuffer(gl.PIXEL_UNPACK_BUFFER, src.buffer.gl);
          gl.texSubImage2D(gl.TEXTURE_2D, level, origin.x, origin.y, size.width, size.height, format, type, offset);
          gl.bindBuffer(gl.PIXEL_UNPACK_BUFFER, null);
        }
        gl.pixelStorei(gl.UNPACK_ROW_LENGTH, 0);
        if (tex.isCanvas) tex.dirty = true;
      });
    }
    copyTextureToBuffer(src, dst, copySize) {
      const size = sizeOf(copySize), origin = originOf(src.origin), tex = src.texture;
      const [, format, type, bpp] = tex.fmt;
      const bytesPerRow = dst.bytesPerRow ?? size.width * bpp;
      this.cmds.push(() => {
        gl.bindFramebuffer(gl.READ_FRAMEBUFFER, readFbo);
        gl.framebufferTexture2D(gl.READ_FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, tex.gl, src.mipLevel ?? 0);
        gl.pixelStorei(gl.PACK_ROW_LENGTH, bytesPerRow / bpp);
        const view = arrayViewFor(type, dst.buffer.host ? dst.buffer.backing : new ArrayBuffer(size.height * bytesPerRow), dst.buffer.host ? (dst.offset ?? 0) : 0, size.height * bytesPerRow);
        gl.readPixels(origin.x, origin.y, size.width, size.height, format, type, view);
        gl.pixelStorei(gl.PACK_ROW_LENGTH, 0);
        if (!dst.buffer.host) { gl.bindBuffer(dst.buffer.glTarget, dst.buffer.gl); gl.bufferSubData(dst.buffer.glTarget, dst.offset ?? 0, view); }
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      });
    }
    copyTextureToTexture(src, dst, copySize) {
      const size = sizeOf(copySize), so = originOf(src.origin), d_o = originOf(dst.origin);
      const depth = src.texture.fmt[4].startsWith('depth');
      const sv = new GPUTextureViewImpl(src.texture, { baseMipLevel: src.mipLevel ?? 0 });
      const dv = new GPUTextureViewImpl(dst.texture, { baseMipLevel: dst.mipLevel ?? 0 });
      this.cmds.push(() => { blit(sv, dv, so.x, so.y, d_o.x, d_o.y, size.width, size.height, depth ? gl.DEPTH_BUFFER_BIT : gl.COLOR_BUFFER_BIT); if (dst.texture.isCanvas) dst.texture.dirty = true; });
    }
    clearBuffer(buffer, offset = 0, size) {
      size ??= buffer.size - offset;
      this.cmds.push(() => { if (buffer.host) new Uint8Array(buffer.backing, offset, size).fill(0); else { gl.bindBuffer(buffer.glTarget, buffer.gl); gl.bufferSubData(buffer.glTarget, offset, new Uint8Array(size)); } });
    }
    pushDebugGroup() {} popDebugGroup() {} insertDebugMarker() {}
    resolveQuerySet() { unsupported('query sets'); }
    finish() { return new GPUCommandBufferImpl(this.cmds); }
  }

  function arrayViewFor(type, ab, offset, bytes) {
    switch (type) {
      case gl.FLOAT: return new Float32Array(ab, offset, bytes / 4);
      case gl.UNSIGNED_INT: case gl.UNSIGNED_INT_24_8: return new Uint32Array(ab, offset, bytes / 4);
      case gl.INT: return new Int32Array(ab, offset, bytes / 4);
      case gl.SHORT: return new Int16Array(ab, offset, bytes / 2);
      case gl.UNSIGNED_SHORT: case gl.HALF_FLOAT: return new Uint16Array(ab, offset, bytes / 2);
      default: return new Uint8Array(ab, offset, bytes);
    }
  }

  // ----- canvas context -----
  let canvasTexture = null;
  const canvasContext = {
    canvas,
    configure(cfg) { this.format = cfg.format; this.configured = true; },
    unconfigure() { this.configured = false; },
    getCurrentTexture() {
      const w = canvas.width, h = canvas.height;
      if (!canvasTexture || canvasTexture.width !== w || canvasTexture.height !== h) {
        canvasTexture?.destroy();
        canvasTexture = new GPUTextureImpl({ label: 'canvas', size: [w, h], format: 'rgba8unorm', usage: TextureUsage.RENDER_ATTACHMENT | TextureUsage.COPY_DST | TextureUsage.TEXTURE_BINDING });
        canvasTexture.isCanvas = true;
      }
      return canvasTexture;
    },
    getConfiguration() { return { device, format: this.format }; },
  };
  function present() {
    if (!canvasTexture || !canvasTexture.dirty) return;
    canvasTexture.dirty = false;
    gl.bindFramebuffer(gl.READ_FRAMEBUFFER, readFbo);
    gl.framebufferTexture2D(gl.READ_FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, canvasTexture.gl, 0);
    gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, null);
    gl.disable(gl.SCISSOR_TEST);
    const w = canvasTexture.width, h = canvasTexture.height;
    // Row 0 of the texture is the top; the default framebuffer shows row 0 at the bottom: flip.
    gl.blitFramebuffer(0, 0, w, h, 0, h, w, 0, gl.COLOR_BUFFER_BIT, gl.NEAREST);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  }
  const origGetContext = canvas.getContext.bind(canvas);
  canvas.getContext = function (kind, ...rest) { return kind === 'webgpu' ? canvasContext : origGetContext(kind, ...rest); };

  // ----- queue + device -----
  class GPUQueueImpl {
    constructor() { this.label = ''; }
    submit(buffers) {
      for (const cb of buffers) for (const c of cb.cmds) c();
      present();
      const err = gl.getError();
      if (err !== gl.NO_ERROR) raise(new ValidationError(`WebGL error 0x${err.toString(16)} during submit`));
    }
    writeBuffer(buffer, offset, data, dataOffset = 0, size) {
      const bytes = ArrayBuffer.isView(data) ? new Uint8Array(data.buffer, data.byteOffset + dataOffset * (data.BYTES_PER_ELEMENT ?? 1), size ?? (data.byteLength - dataOffset * (data.BYTES_PER_ELEMENT ?? 1))) : new Uint8Array(data, dataOffset, size);
      if (buffer.host) new Uint8Array(buffer.backing, offset, bytes.byteLength).set(bytes);
      else { gl.bindBuffer(buffer.glTarget, buffer.gl); gl.bufferSubData(buffer.glTarget, offset, bytes); }
    }
    writeTexture(dst, data, layout, copySize) {
      const size = sizeOf(copySize), origin = originOf(dst.origin), tex = dst.texture;
      const [, format, type, bpp] = tex.fmt;
      const bytesPerRow = layout.bytesPerRow ?? size.width * bpp;
      const src = ArrayBuffer.isView(data) ? data : new Uint8Array(data);
      gl.bindTexture(gl.TEXTURE_2D, tex.gl);
      gl.bindBuffer(gl.PIXEL_UNPACK_BUFFER, null);
      gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
      gl.pixelStorei(gl.UNPACK_ROW_LENGTH, bytesPerRow / bpp);
      const view = arrayViewFor(type, src.buffer, src.byteOffset + (layout.offset ?? 0), size.height * bytesPerRow);
      gl.texSubImage2D(gl.TEXTURE_2D, dst.mipLevel ?? 0, origin.x, origin.y, size.width, size.height, format, type, view);
      gl.pixelStorei(gl.UNPACK_ROW_LENGTH, 0);
      if (tex.isCanvas) tex.dirty = true;
    }
    onSubmittedWorkDone() { return new Promise(r => setTimeout(r, 0)); }
  }

  class GPUDeviceImpl {
    constructor(desc) {
      this.label = desc?.label ?? ''; this.features = features; this.limits = limits; this.queue = new GPUQueueImpl(); this.onuncapturederror = null;
      this.lost = new Promise(res => { canvas.addEventListener('webglcontextlost', () => res({ reason: 'unknown', message: 'WebGL context lost' }), { once: true }); });
      this.adapterInfo = info;
    }
    createBuffer(d) { return new GPUBufferImpl(d); }
    createTexture(d) { return new GPUTextureImpl(d); }
    createSampler(d) { return new GPUSamplerImpl(d); }
    createBindGroupLayout(d) { return new GPUBindGroupLayoutImpl(d); }
    createPipelineLayout(d) { return new GPUPipelineLayoutImpl(d); }
    createBindGroup(d) { return new GPUBindGroupImpl(d); }
    createShaderModule(d) { return new GPUShaderModuleImpl(d); }
    createRenderPipeline(d) { return new GPURenderPipelineImpl(d); }
    createRenderPipelineAsync(d) { try { return Promise.resolve(new GPURenderPipelineImpl(d)); } catch (e) { return Promise.reject(e); } }
    createComputePipeline() { unsupported('compute pipelines'); }
    createComputePipelineAsync() { return Promise.reject(new Error('gpu-gl2: compute pipelines are not supported')); }
    createCommandEncoder(d) { return new GPUCommandEncoderImpl(d); }
    createRenderBundleEncoder() { unsupported('render bundles'); }
    createQuerySet() { unsupported('query sets'); }
    pushErrorScope(filter) { errorScopes.push({ filter, error: null }); }
    popErrorScope() { const s = errorScopes.pop(); return Promise.resolve(s ? s.error : null); }
    addEventListener() {} removeEventListener() {}
    destroy() {}
  }

  const adapter = {
    features, limits, info, isFallbackAdapter: true,
    requestDevice(desc) { device = new GPUDeviceImpl(desc); return Promise.resolve(device); },
    requestAdapterInfo() { return Promise.resolve(info); },
  };
  const gpu = {
    isWebGL2Fallback: true,
    requestAdapter() { return Promise.resolve(adapter); },
    getPreferredCanvasFormat() { return 'rgba8unorm'; },
    wgslLanguageFeatures: new Set(),
  };
  Object.defineProperty(navigator, 'gpu', { value: gpu, configurable: true });
  return gpu;
}
