// Can the game be driven from boot to the main menu, and what happens when we
// walk into each menu entry?
//
// The second half is the useful part: "some menus freeze or go black" is not
// something anyone can act on, but "entry 3 leaves MENU, reaches VS[0] and
// then stops advancing for 10 s" is. Every entry is tried from a known state
// and the outcome recorded, so unimplemented screens become a list.
//
// Usage: node menu_nav_test.mjs <build-dir> <disc.iso> [entries]
import path from 'node:path';
import { launchGame, bootToMainMenu, scene, sceneName, press, waitForScene, pressUntilScene, menuState, moveCursorTo } from './nav.mjs';

const [buildDir, disc, entriesArg] = process.argv.slice(2);
if (!buildDir || !disc) {
  console.error('usage: menu_nav_test.mjs <build-dir> <disc.iso> [entries]');
  process.exit(2);
}
const ENTRIES = Number(entriesArg ?? 5);

let failed = false;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed = true; };
const sleep = (ms) => new Promise(r => setTimeout(r, ms));

const { page, logs, close } = await launchGame({ buildDir, disc });

try {
  // --- reaching the menu at all ---
  const { scene: menu, trail } = await bootToMainMenu(page);
  for (const step of trail) console.log(`     ${step}`);
  check(menu.mode === 1, `reached the main menu (${sceneName(menu)})`);

  // --- walk into each entry and see where it goes ---
  //
  // After each attempt we come back with B, so every entry starts from the
  // same place. An entry that cannot be backed out of is itself a finding.
  const results = [];
  for (let i = 0; i < ENTRIES; i++) {
    const before = await scene(page);
    if (before.mode !== 1) {
      results.push({ entry: i, outcome: `skipped: not at the menu (${sceneName(before)})` });
      continue;
    }

    // Drive the cursor by reading it back, not by counting presses: backing
    // out of a submenu leaves it wherever it was, and the menu ignores input
    // while it animates.
    const placed = await moveCursorTo(page, i);
    if (!placed.ok) {
      results.push({ entry: i, outcome: `cursor would not reach it (stopped at ${placed.hovered}, reachable: ${JSON.stringify(placed.seen)})` });
      continue;
    }
    await press(page, 'A', 150);
    await sleep(1500);

    const after = await scene(page);
    if (after.mode === 1) {
      const m = await menuState(page);
      results.push({ entry: i, outcome: `stayed at the menu (cursor now ${m.hovered}, submenu ${m.curMenu})` });
      continue;
    }

    // It went somewhere. Does it keep moving, or has it stopped?
    const first = sceneName(after);
    let moved = false;
    for (let t = 0; t < 12; t++) {
      await sleep(700);
      const now = await scene(page);
      if (now.mode !== after.mode || now.index !== after.index) { moved = true; break; }
    }
    const settled = await scene(page);
    // A screen that sits still may be waiting for input or may be wedged.
    // Poke it and see whether it answers; that is the difference between "not
    // implemented" and "frozen".
    let answered = false;
    if (!moved) {
      for (const btn of ['A', 'START', 'B']) {
        await press(page, btn, 150);
        await sleep(900);
        const now = await scene(page);
        if (now.mode !== settled.mode || now.index !== settled.index) { answered = true; break; }
      }
    }
    results.push({
      entry: i,
      outcome: moved ? `${first} -> advanced to ${sceneName(settled)}`
        : `${first} -> sat still ~8s, ${answered ? 'but responded to input' : 'and ignored A/START/B'}`,
    });

    // Back out, however many levels deep we ended up.
    try {
      await pressUntilScene(page, 'B', s => s.mode === 1, { tries: 14, gap: 600, what: 'the menu again' });
    } catch {
      results.push({ entry: i, outcome: `could not return to the menu from ${sceneName(await scene(page))}` });
      break;
    }
  }

  console.log('\n--- menu entries ---');
  for (const r of results) console.log(`  entry ${r.entry}: ${r.outcome}`);

  check(results.length > 0, 'walked at least one menu entry');
  const reachable = results.filter(r => r.outcome.includes('->')).length;
  console.log(`\n${reachable} of ${results.length} entries led somewhere.`);
} catch (e) {
  check(false, e.message);
  console.log('\n--- page log (last 30) ---');
  console.log(logs.slice(-30).join('\n'));
} finally {
  await close();
}
console.log(failed ? '\nFAILED' : '\nPASSED');
process.exit(failed ? 1 : 0);
