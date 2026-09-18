// Can the game be driven from boot to the main menu, and what happens when we
// walk into each menu entry?
//
// The second half is the useful part: "some menus freeze or go black" is not
// something anyone can act on, but "+2 DOWN reaches VS[0], which is alive at
// 28 fps and simply ignores the buttons we know how to press" is. Screens that
// are genuinely hung and screens that are merely waiting become two lists.
//
// Each entry is probed from a fresh boot rather than by backing out of the
// previous one. That costs a reload per entry, but it is the only way to make
// the probes independent: the menu cursor persists across a back-out, and a
// screen that cannot be escaped would otherwise end the sweep (which is
// exactly what happened before this was written that way).
//
// Usage: node menu_nav_test.mjs <build-dir> <disc.iso> [entries]
import path from 'node:path';
import {
  launchGame, bootToMainMenu, reboot, scene, sceneName, press, menuState, sleep, isAlive,
} from './nav.mjs';

const [buildDir, disc, entriesArg] = process.argv.slice(2);
if (!buildDir || !disc) {
  console.error('usage: menu_nav_test.mjs <build-dir> <disc.iso> [entries]');
  process.exit(2);
}
const ENTRIES = Number(entriesArg ?? 5);

let failed = false;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed = true; };

const { page, logs, close } = await launchGame({ buildDir, disc });

// Only call a screen hung if the game has actually stopped. A live frame loop
// sitting on one scene is a screen waiting for input we do not know how to
// give -- a different problem, and a much less alarming one. Confusing the two
// is how a working character-select screen got reported here as a freeze.
async function liveness(page, where) {
  const live = await isAlive(page);
  return live && live.alive
    ? `${where} -> alive (${live.fps.toFixed(0)} fps) but ignores A/START/B`
    : `${where} -> HUNG: the frame loop stopped`;
}


// Walk into the entry `steps` below the cursor's start and describe where we
// land. Assumes the caller has just reached the main menu from a fresh boot.
async function probeEntry(steps) {
  // Hold long enough to register. The menu has a five-frame input cooldown
  // (mn_804D6BC8.cooldown), and at the ~20 fps this build manages under a
  // software renderer a 110 ms press is barely two frames -- short enough that
  // the cursor often did not move at all.
  for (let d = 0; d < steps; d++) { await press(page, 'DOWN', 280); await sleep(500); }
  await sleep(400);
  const menuBefore = await menuState(page);
  await press(page, 'A', 150);
  await sleep(1800);

  const after = await scene(page);
  const menuAfter = await menuState(page);

  if (after.mode === 1) {
    if (menuAfter.curMenu === menuBefore.curMenu) {
      return `nothing happened (menu ${menuAfter.curMenu})`;
    }
    // Entering a submenu keeps us in MENU mode. Confirm once more to reach the
    // thing the submenu actually launches -- that is where an unimplemented
    // screen shows up.
    const sub = menuAfter.curMenu;
    await press(page, 'A', 280);
    await sleep(2200);
    const deep = await scene(page);
    if (deep.mode === 1) {
      const m2 = await menuState(page);
      return `submenu ${sub} -> then menu ${m2.curMenu} (stayed in MENU)`;
    }
    const landedDeep = sceneName(deep);
    for (let t = 0; t < 8; t++) {
      await sleep(800);
      const now = await scene(page);
      if (now.mode !== deep.mode || now.index !== deep.index) {
        return `submenu ${sub} -> ${landedDeep} -> advanced to ${sceneName(now)}`;
      }
    }
    for (const btn of ['A', 'START', 'B']) {
      await press(page, btn, 280);
      await sleep(1100);
      const now = await scene(page);
      if (now.mode !== deep.mode || now.index !== deep.index) {
        return `submenu ${sub} -> ${landedDeep} -> idle, ${btn} moved it to ${sceneName(now)}`;
      }
    }
    return `submenu ${sub} -> ${landedDeep} -> ` + await liveness(page, landedDeep);
  }

  // It left the menu. Is it still making progress, or has it stopped?
  const landed = sceneName(after);
  for (let t = 0; t < 10; t++) {
    await sleep(700);
    const now = await scene(page);
    if (now.mode !== after.mode || now.index !== after.index) {
      return `${landed} -> advanced to ${sceneName(now)}`;
    }
  }

  // Sitting still. Waiting for input, or hung?
  for (const btn of ['A', 'START', 'B']) {
    await press(page, btn, 150);
    await sleep(1000);
    const now = await scene(page);
    if (now.mode !== after.mode || now.index !== after.index) {
      return `${landed} -> idle, but ${btn} moved it to ${sceneName(now)}`;
    }
  }
  return `${landed} -> ` + await liveness(page, landed);
}

try {
  const { scene: menu, trail } = await bootToMainMenu(page);
  for (const step of trail) console.log(`     ${step}`);
  check(menu.mode === 1, `reached the main menu (${sceneName(menu)})`);

  const results = [];
  for (let i = 0; i < ENTRIES; i++) {
    if (i > 0) {
      await reboot(page, disc);
      const back = await bootToMainMenu(page);
      if (back.scene.mode !== 1) {
        results.push({ steps: i, outcome: `could not get back to the menu (${sceneName(back.scene)})` });
        continue;
      }
    }
    try {
      results.push({ steps: i, outcome: await probeEntry(i) });
    } catch (e) {
      results.push({ steps: i, outcome: `probe failed: ${e.message}` });
    }
  }

  console.log('\n--- menu entries (by DOWN presses from the default cursor) ---');
  for (const r of results) console.log(`  +${r.steps} DOWN: ${r.outcome}`);

  const wedged = results.filter(r => r.outcome.includes('HUNG'));
  const left = results.filter(r => r.outcome.includes('->') && !r.outcome.includes('still in MENU'));
  console.log(`\n${left.length} of ${results.length} entries left the menu; ${wedged.length} hung.`);
  if (wedged.length) {
    console.log('hung screens (frame loop stopped -- real freezes):');
    for (const w of wedged) console.log(`  +${w.steps} DOWN: ${w.outcome}`);
  }
  check(results.length === ENTRIES, `probed all ${ENTRIES} entries`);
} catch (e) {
  check(false, e.message);
  console.log('\n--- page log (last 30) ---');
  console.log(logs.slice(-30).join('\n'));
} finally {
  await close();
}
console.log(failed ? '\nFAILED' : '\nPASSED');
process.exit(failed ? 1 : 0);
