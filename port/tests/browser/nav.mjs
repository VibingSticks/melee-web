// Driving Melee's menus from a test.
//
// Timed button presses are fragile: the boot sequence ignores input for its
// first few hundred frames, and how long a screen takes to appear depends on
// how fast the machine is. So everything here is written against the game's
// own state machine instead. port_scene_state() reports the current game mode
// and scene index, and the helpers press a button and wait for that pair to
// change, retrying rather than guessing.
import { createRequire } from 'node:module';
import { spawn } from 'node:child_process';
import path from 'node:path';

const require = createRequire('/home/ralsei/.npm/_npx/9833c18b2d85bc59/node_modules/');
const { chromium } = require('playwright-core');

// src/melee/gm/forward.h
export const MODES = {
  0: 'TITLE', 1: 'MENU', 2: 'VS', 3: 'CLASSIC', 4: 'ADVENTURE', 5: 'ALLSTAR',
  6: 'DEBUG', 7: 'DEBUG_SOUND_TEST', 8: 'HANYU_CSS', 9: 'HANYU_SSS',
  10: 'CAMERA_MODE', 11: 'TOY_GALLERY', 12: 'TOY_LOTTERY', 13: 'TOY_COLLECTION',
  14: 'DEBUG_VS', 15: 'TARGET_TEST', 16: 'SSD_VS', 17: 'INVISIBLE_VS',
  18: 'SLOMO_VS', 19: 'LIGHTNING_VS', 20: 'CHALLENGER_APPROACH',
  21: 'CLASSIC_GOVER', 22: 'ADVENTURE_GOVER', 23: 'ALLSTAR_GOVER',
  24: 'OPENING_MV', 25: 'DEBUG_CUTSCENE', 26: 'DEBUG_GOVER', 27: 'TOURNAMENT',
  28: 'TRAINING', 29: 'TINY_VS', 30: 'GIANT_VS', 31: 'STAMINA_VS',
  32: 'HOME_RUN_CONTEST', 33: '10MAN_VS', 34: '100MAN_VS', 35: '3MIN_VS',
  36: '15MIN_VS', 37: 'ENDLESS_VS', 38: 'CRUEL_VS', 39: 'PROGRESSIVE_SCAN',
  40: 'BOOT', 41: 'MEMCARD', 42: 'CAMERA_VS', 43: 'EVENT', 44: 'SINGLE_BUTTON_VS',
};
export const modeName = (m) => MODES[m] ?? `mode ${m}`;
export const sceneName = (s) => `${modeName(s.mode)}[${s.index}]`;

export const BUTTONS = { LEFT: 1, RIGHT: 2, DOWN: 4, UP: 8, Z: 16, R: 32, L: 64, A: 256, B: 512, X: 1024, Y: 2048, START: 4096 };
// Melee's menus are stick-driven, so a direction deflects the stick too.
const STICK = { UP: [0, 110], DOWN: [0, -110], LEFT: [-110, 0], RIGHT: [110, 0] };

function decode(name) {
  let bits = 0, sx = 0, sy = 0;
  for (const raw of name.split('+')) {
    const n = raw.toUpperCase();
    const stickOnly = n.startsWith('S') && STICK[n.slice(1)];
    const dir = stickOnly ? n.slice(1) : n;
    if (STICK[dir]) { sx += STICK[dir][0]; sy += STICK[dir][1]; }
    if (!stickOnly) bits |= BUTTONS[n] ?? 0;
  }
  return { bits, sx, sy };
}

export const sleep = (ms) => new Promise(r => setTimeout(r, ms));

/** Serve the build, open it, feed it the disc, and wait until the game runs. */
export async function launchGame({ buildDir, disc, port = 8795, query = 'renderer=webgl2&res=640x480', headed = false }) {
  const server = spawn('python3', ['-m', 'http.server', '-d', path.resolve(buildDir), String(port)], { stdio: 'ignore' });
  await sleep(800);
  const browser = await chromium.launch({
    headless: !headed,
    executablePath: '/opt/google/chrome/chrome',
    args: ['--enable-unsafe-webgpu', '--ignore-gpu-blocklist', '--no-sandbox', '--enable-unsafe-swiftshader'],
  });
  const context = await browser.newContext();
  const page = await context.newPage();
  const logs = [];
  const t0 = Date.now();
  page.on('console', m => logs.push(`[${Date.now() - t0}ms] ${m.text()}`));
  page.on('pageerror', e => logs.push(`[${Date.now() - t0}ms] [pageerror] ${e.message}`));

  await page.goto(`http://localhost:${port}/index.html?${query}`, { waitUntil: 'domcontentloaded' });
  await page.setInputFiles('#disc', path.resolve(disc));

  const close = async () => { await browser.close().catch(() => {}); server.kill(); };
  // The exports only exist once the runtime is up.
  const until = Date.now() + 60000;
  while (Date.now() < until) {
    if (await page.evaluate(() => !!(window.Module && window.Module._port_scene_state))) break;
    await sleep(200);
  }
  return { browser, context, page, logs, server, close, t0 };
}

/** The game's current {mode, index}, or null before the runtime is ready. */
export async function scene(page) {
  return page.evaluate(() => {
    if (!window.Module || !window.Module._port_scene_state) return null;
    const v = window.Module._port_scene_state();
    return { mode: (v >> 8) & 0xff, index: v & 0xff };
  });
}

/** Wait until `pred({mode,index})` holds. Throws with the last scene seen. */
export async function waitForScene(page, pred, { timeout = 30000, what = 'a scene' } = {}) {
  const until = Date.now() + timeout;
  let last = null;
  while (Date.now() < until) {
    last = await scene(page);
    if (last && pred(last)) return last;
    await sleep(120);
  }
  throw new Error(`timed out waiting for ${what}; still at ${last ? sceneName(last) : 'no scene'}`);
}

/** Press a button (or "A+START", or stick-only "SDOWN") for `hold` ms. */
export async function press(page, name, hold = 150) {
  const { bits, sx, sy } = decode(name);
  await page.evaluate(([b, x, y, h]) => {
    window.Module._port_pad_virtual(0, b, x, y, 0, 0, 0, 0);
    setTimeout(() => window.Module._port_pad_virtual_clear(0), h);
  }, [bits, sx, sy, hold]);
  await sleep(hold + 60);
}

/**
 * Press until the scene satisfies `pred`, or give up.
 *
 * This is the workhorse. A screen may ignore input while it loads or animates,
 * so a single press proves nothing; pressing repeatedly and watching the state
 * machine is what makes navigation reproducible on a slow machine.
 */
export async function pressUntilScene(page, name, pred, { tries = 12, gap = 700, hold = 150, what } = {}) {
  const label = what ?? `${name} to change the scene`;
  for (let i = 0; i < tries; i++) {
    const now = await scene(page);
    if (now && pred(now)) return now;
    await press(page, name, hold);
    await sleep(gap);
  }
  const last = await scene(page);
  throw new Error(`pressed ${name} ${tries} times waiting for ${label}; still at ${last ? sceneName(last) : 'no scene'}`);
}

/** Boot through the health screen, intro and title to the main menu. */
export async function bootToMainMenu(page, { timeout = 90000 } = {}) {
  const trail = [];
  const note = async (why) => { const s = await scene(page); trail.push(`${why}: ${sceneName(s)}`); return s; };

  // The boot scene ignores input for its first few hundred frames.
  await waitForScene(page, s => s.mode === 40 || s.mode === 0 || s.mode === 1, { timeout, what: 'the boot sequence' });
  await note('booted');

  // BOOT -> (intro movie) -> TITLE -> MENU. The movie is skipped in this port,
  // so the intro may pass by on its own; either way, press on until MENU.
  await pressUntilScene(page, 'START', s => s.mode !== 40, { tries: 20, what: 'the boot screen to end' });
  await note('left boot');

  await pressUntilScene(page, 'A', s => s.mode === 0 || s.mode === 1, { tries: 20, what: 'the title screen' });
  await note('reached title');

  const at = await pressUntilScene(page, 'START', s => s.mode === 1, { tries: 20, what: 'the main menu' });
  await note('reached menu');
  return { scene: at, trail };
}

/** The main menu's cursor: {curMenu, prevMenu, hovered}. */
export async function menuState(page) {
  return page.evaluate(() => {
    if (!window.Module || !window.Module._port_menu_state) return null;
    const v = window.Module._port_menu_state();
    return { curMenu: (v >>> 24) & 0xff, prevMenu: (v >>> 16) & 0xff, hovered: v & 0xffff };
  });
}

/**
 * Put the main-menu cursor on `target` and report whether it got there.
 *
 * Counting button presses does not work: backing out of a submenu leaves the
 * cursor where it was, and a menu ignores input while it animates. So step one
 * at a time and read the cursor back after each press, which also reveals how
 * many entries the menu actually has (the cursor stops moving or wraps).
 */
export async function moveCursorTo(page, target, { tries = 24, gap = 260 } = {}) {
  let last = await menuState(page);
  if (!last) return { ok: false, why: 'no menu state' };
  const seen = new Set([last.hovered]);
  for (let i = 0; i < tries && last.hovered !== target; i++) {
    await press(page, last.hovered < target ? 'DOWN' : 'UP', 110);
    await sleep(gap);
    const now = await menuState(page);
    if (now.hovered === last.hovered) {
      // Did not move: either the menu is busy or we are against an end.
      await press(page, 'DOWN', 110);
      await sleep(gap);
    }
    last = await menuState(page);
    seen.add(last.hovered);
  }
  return { ok: last.hovered === target, hovered: last.hovered, seen: [...seen].sort((a, b) => a - b) };
}

/** Reload the page, feed the disc again, and wait for the runtime. */
export async function reboot(page, disc) {
  await page.reload({ waitUntil: 'domcontentloaded' });
  await page.setInputFiles('#disc', path.resolve(disc));
  const until = Date.now() + 60000;
  while (Date.now() < until) {
    if (await page.evaluate(() => !!(window.Module && window.Module._port_scene_state))) return;
    await sleep(200);
  }
  throw new Error('the runtime did not come back after a reload');
}

/** Frames the game has completed since boot, or null if unavailable. */
export async function frameCount(page) {
  return page.evaluate(() => (window.Module && window.Module._port_frame_count)
    ? window.Module._port_frame_count() : null);
}

/**
 * Is the game still running? A screen that sits on one scene may be waiting
 * for input or may be hung, and only the frame counter tells them apart --
 * calling the first one "wedged" is how a working character-select screen got
 * reported as a freeze.
 */
export async function isAlive(page, overMs = 2000) {
  const a = await frameCount(page);
  if (a === null) return null;
  await sleep(overMs);
  const b = await frameCount(page);
  return { alive: b > a, frames: b - a, fps: ((b - a) * 1000) / overMs };
}

// --- character select -------------------------------------------------------
//
// port_css_cursor biases both coordinates so a valid result is never negative;
// -1 alone means "no cursor". Decoding it as a plain signed int is what made
// three of the four hands look absent when they were all present.
const CSS_X_BIAS = 0x4000, CSS_Y_BIAS = 0x8000;

/** Every port's hand cursor: {x, y} in game units, or null if there is none. */
export async function cssCursors(page) {
  const raw = await page.evaluate(() => (window.Module && window.Module._port_css_cursor)
    ? [0, 1, 2, 3].map(i => window.Module._port_css_cursor(i)) : null);
  if (!raw) return null;
  return raw.map(v => v < 0 ? null
    : { x: (((v >>> 16) & 0x7fff) - CSS_X_BIAS) / 100, y: ((v & 0xffff) - CSS_Y_BIAS) / 100 });
}

export const CSS_SLOT = { 0: 'HUMAN', 1: 'CPU', 2: 'DEMO', 3: 'NONE', 4: 'BOSS' };

/** Every port's slot: {type, typeName, ckind}, or null before the screen is up. */
export async function cssSlots(page) {
  const raw = await page.evaluate(() => (window.Module && window.Module._port_css_live_slot)
    ? [0, 1, 2, 3].map(i => window.Module._port_css_live_slot(i)) : null);
  if (!raw) return null;
  return raw.map(v => v < 0 ? null
    : { type: (v >> 8) & 0xff, typeName: CSS_SLOT[(v >> 8) & 0xff] ?? String((v >> 8) & 0xff), ckind: v & 0xff });
}

/**
 * Join port 1 by walking its hand up onto the roster.
 *
 * The hand moves about 0.0002 units per frame of full deflection and has some
 * 22 units to cover, so it needs the stick held for the better part of a
 * second -- a 150 ms tap moves it a couple of units and looks like nothing
 * happening at all. Joining takes no button: crossing onto the roster with the
 * door unclaimed sets the slot to Human by itself.
 */
export async function cssJoinPort1(page, { tries = 4, hold = 1000 } = {}) {
  for (let i = 0; i < tries; i++) {
    const slots = await cssSlots(page);
    if (slots && slots[0] && slots[0].type === 0) return true;
    await press(page, 'SUP', hold);
    await sleep(400);
  }
  const slots = await cssSlots(page);
  return !!(slots && slots[0] && slots[0].type === 0);
}

/** The roster's icons: {i, x, y, kind, state} for each selectable portrait. */
export async function cssIcons(page) {
  const raw = await page.evaluate(() => {
    if (!window.Module || !window.Module._port_css_icon_count) return null;
    const n = window.Module._port_css_icon_count();
    return Array.from({ length: n }, (_, i) => [window.Module._port_css_icon_pos(i), window.Module._port_css_icon_info(i)]);
  });
  if (!raw) return null;
  return raw.map(([pos, info], i) => pos < 0 || info < 0 ? null : {
    i,
    x: (((pos >>> 16) & 0x7fff) - CSS_X_BIAS) / 100,
    y: ((pos & 0xffff) - CSS_Y_BIAS) / 100,
    kind: info & 0xff,
    state: (info >> 8) & 0xff,
  }).filter(Boolean);
}

/**
 * Steer port 1's hand onto a portrait and choose it.
 *
 * Closed loop, not timed: read the cursor, push toward the target, read again.
 * The hand's speed depends on the frame rate, so any fixed press duration is
 * wrong on a different machine -- which is how earlier attempts kept missing.
 * The hit test uses the token, which sits at hand + (2.7, -2.0), so the hand is
 * aimed at the portrait centre less that offset.
 */
export async function cssPickCharacter(page, icon, { steps = 26, tol = 1.2 } = {}) {
  const tx = icon.x - 2.7, ty = icon.y + 2.0;
  for (let n = 0; n < steps; n++) {
    const cur = (await cssCursors(page))?.[0];
    if (!cur) return { ok: false, why: 'port 1 has no cursor' };
    const dx = tx - cur.x, dy = ty - cur.y;
    if (Math.abs(dx) < tol && Math.abs(dy) < tol) break;
    const dir = [];
    if (dy > tol) dir.push('SUP'); else if (dy < -tol) dir.push('SDOWN');
    if (dx > tol) dir.push('SRIGHT'); else if (dx < -tol) dir.push('SLEFT');
    if (dir.length === 0) break;
    // Roughly proportional, clamped: long pushes when far, taps when close.
    const dist = Math.max(Math.abs(dx), Math.abs(dy));
    await press(page, dir.join('+'), Math.min(900, Math.max(90, Math.round(dist * 45))));
    await sleep(140);
  }
  const before = (await cssSlots(page))?.[0]?.ckind;
  await press(page, 'A', 260);
  await sleep(900);
  const after = (await cssSlots(page))?.[0];
  const cur = (await cssCursors(page))?.[0];
  return {
    ok: !!after && after.ckind === icon.kind,
    picked: after?.ckind, wanted: icon.kind, before,
    at: cur ? `(${cur.x.toFixed(1)},${cur.y.toFixed(1)})` : '?',
  };
}

/** Pick one selectable portrait at random (state != locked). */
export function randomIcon(icons) {
  const open = icons.filter(ic => ic.state !== 0);
  return open.length ? open[Math.floor(Math.random() * open.length)] : null;
}

/**
 * Wait until a character-select screen is actually interactive.
 *
 * The icon table is a static array and reads fine long before the screen is
 * up, so its presence proves nothing; port 1's hand cursor is what only exists
 * once the screen has been built. Waiting on a timer instead keeps breaking
 * whenever the game gets faster.
 */
export async function waitForCss(page, { timeout = 30000 } = {}) {
  const until = Date.now() + timeout;
  while (Date.now() < until) {
    const s = await scene(page);
    const cur = await cssCursors(page);
    if (s && s.mode !== 1 && cur && cur[0]) return { scene: s, cursor: cur[0] };
    await sleep(250);
  }
  const s = await scene(page);
  throw new Error(`no character select became interactive; at ${s ? sceneName(s) : 'no scene'}`);
}

/**
 * From a fresh boot, walk `downs` entries into the main menu and confirm until
 * the game leaves MENU, then report where it landed.
 *
 * Every fixed-count sequence I wrote for this broke as soon as the game's
 * speed changed -- presses that were being dropped started registering. So the
 * confirms are driven by the state machine, not by a count.
 */
export async function enterMode(page, downs, { confirms = 6, timeout = 30000 } = {}) {
  await bootToMainMenu(page);
  for (let d = 0; d < downs; d++) { await press(page, 'DOWN', 280); await sleep(450); }
  await sleep(350);
  const landed = await pressUntilScene(page, 'A', s => s.mode !== 1,
    { tries: confirms, gap: 2200, hold: 280, what: 'the menu to hand over' });
  // Some modes open a character select; others go straight to their own scene.
  let css = null;
  try { css = await waitForCss(page, { timeout: 12000 }); } catch { /* not a CSS */ }
  return { scene: landed, css };
}

/**
 * Measure the frame rate, so presses can be expressed in frames.
 *
 * Every fixed millisecond hold in this file has been wrong at least once: the
 * menu has a five-frame input cooldown, so the hold that works at 30 fps is
 * far too short at the 5 fps a debug build manages, and too long once the game
 * speeds up. Measure instead of guessing.
 */
export async function measureFps(page, overMs = 1500) {
  const a = await frameCount(page);
  if (a === null) return null;
  await sleep(overMs);
  const b = await frameCount(page);
  const fps = ((b - a) * 1000) / overMs;
  return fps > 0.5 ? fps : null;
}

/** Hold a button for `frames` frames at the measured rate (min 150 ms). */
export async function pressFrames(page, name, frames = 8, fpsHint = null) {
  const fps = fpsHint ?? (await measureFps(page, 800)) ?? 30;
  const ms = Math.max(150, Math.min(2500, Math.round((frames / fps) * 1000)));
  await press(page, name, ms);
  return { fps, ms };
}

/** pressUntilScene, but with frame-based holds for slow builds. */
export async function pressUntilSceneFrames(page, name, pred, { tries = 10, frames = 10, gap = 1200, what } = {}) {
  const fps = (await measureFps(page, 1000)) ?? 30;
  for (let i = 0; i < tries; i++) {
    const now = await scene(page);
    if (now && pred(now)) return now;
    await pressFrames(page, name, frames, fps);
    await sleep(gap);
  }
  const last = await scene(page);
  throw new Error(`pressed ${name} ${tries}x (${fps.toFixed(1)} fps) waiting for ${what ?? 'a scene'}; at ${last ? sceneName(last) : '?'}`);
}
