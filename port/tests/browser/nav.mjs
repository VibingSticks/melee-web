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
