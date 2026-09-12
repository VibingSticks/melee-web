// Translate every fixture with naga.wasm and compile/link the GLSL in headless Chrome.
// Usage: node translate_test.mjs [--print]
import { readFileSync, readdirSync } from 'node:fs';
import { createRequire } from 'node:module';
import { loadNaga } from '../../web/js/naga/naga.js';

const require = createRequire('/home/ralsei/.npm/_npx/9833c18b2d85bc59/node_modules/');
const { chromium } = require('playwright-core');

const print = process.argv.includes('--print');
const naga = await loadNaga(readFileSync(new URL('../../web/js/naga/naga.wasm', import.meta.url)));
const fixtures = readdirSync(new URL('./fixtures', import.meta.url)).filter(f => f.endsWith('.wgsl')).sort();

const translated = [];
let failed = 0;
for (const f of fixtures) {
  const wgsl = readFileSync(new URL(`./fixtures/${f}`, import.meta.url), 'utf8');
  const stages = {};
  for (const [entry, stage] of [['vs_main', 'vertex'], ['fs_main', 'fragment']]) {
    if (!wgsl.includes(`fn ${entry}`)) continue;
    try {
      const r = naga.translate(wgsl, entry, stage);
      if (!r.glsl.startsWith('#version 300 es')) throw new Error('output is not GLSL ES 3.00');
      stages[stage] = r;
      console.log(`ok   ${f} ${entry}: ${r.glsl.length} chars, uniforms=${JSON.stringify(r.uniforms)}, textures=${JSON.stringify(r.textures)}`);
      if (print) console.log(r.glsl);
    } catch (e) {
      failed++;
      console.log(`FAIL ${f} ${entry}: ${e.message}`);
    }
  }
  if (stages.vertex && stages.fragment) translated.push({ name: f, vs: stages.vertex.glsl, fs: stages.fragment.glsl });
}

// Compile and link each pair in a real WebGL2 context.
const browser = await chromium.launch({ executablePath: '/opt/google/chrome/chrome', headless: true, args: ['--no-sandbox', '--use-angle=swiftshader'] });
const page = await browser.newPage();
await page.setContent('<canvas id="c"></canvas>');
const results = await page.evaluate((pairs) => {
  const gl = document.getElementById('c').getContext('webgl2');
  if (!gl) return pairs.map(p => ({ name: p.name, ok: false, log: 'no WebGL2' }));
  return pairs.map(({ name, vs, fs }) => {
    const compile = (type, src) => {
      const s = gl.createShader(type);
      gl.shaderSource(s, src);
      gl.compileShader(s);
      return [s, gl.getShaderParameter(s, gl.COMPILE_STATUS) ? '' : gl.getShaderInfoLog(s)];
    };
    const [v, vlog] = compile(gl.VERTEX_SHADER, vs);
    const [f, flog] = compile(gl.FRAGMENT_SHADER, fs);
    if (vlog || flog) return { name, ok: false, log: 'vs: ' + vlog + ' fs: ' + flog };
    const p = gl.createProgram();
    gl.attachShader(p, v); gl.attachShader(p, f); gl.linkProgram(p);
    const ok = gl.getProgramParameter(p, gl.LINK_STATUS);
    return { name, ok, log: ok ? '' : gl.getProgramInfoLog(p) };
  });
}, translated);
await browser.close();
for (const r of results) {
  if (r.ok) console.log(`link ok   ${r.name}`);
  else { failed++; console.log(`link FAIL ${r.name}: ${r.log}`); }
}
process.exit(failed ? 1 : 0);
