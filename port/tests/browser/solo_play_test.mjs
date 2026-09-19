// Try to actually PLAY each single-player mode, not merely reach it.
//
// For every entry under 1P Mode: walk in, and if a character select appears,
// pick a portrait at random, confirm it, press START, and see whether a match
// actually begins. Random rather than fixed so a run exercises different
// characters and one unlucky roster slot cannot make the suite look healthy.
//
// Usage: node solo_play_test.mjs <build-dir> <disc.iso> [entries]
import {
  launchGame, reboot, bootToMainMenu, scene, sceneName, press, sleep, isAlive,
  cssIcons, cssSlots, cssPickCharacter, randomIcon,
} from './nav.mjs';

const [buildDir, disc, entriesArg] = process.argv.slice(2);
if (!buildDir || !disc) { console.error('usage: solo_play_test.mjs <build-dir> <disc.iso> [entries]'); process.exit(2); }
const ENTRIES = Number(entriesArg ?? 5);
const ONE_P = 1; // "1P Mode" is one entry below the default cursor

const { page, logs, close } = await launchGame({ buildDir, disc, port: 8823 });
const BAD = /assert|abort|Abort|trap|Can not Load|Cannot find|panic/i;
const NOISE = /develop\.ini|usa\.ini|ScriptProcessorNode|SuperSmashBros|Failed to close/;
const results = [];

for (let e = 0; e < ENTRIES; e++) {
  if (e > 0) await reboot(page, disc);
  const mark = logs.length;
  const row = { entry: e, mode: '?', pick: '-', started: '-', fps: '-', note: '' };
  try {
    // Into the 1P submenu with a single confirm (this stays inside MENU), then
    // down to this entry, and only then confirm until the game hands over.
    await bootToMainMenu(page);
    for (let d = 0; d < ONE_P; d++) { await press(page, 'DOWN', 280); await sleep(450); }
    await sleep(300);
    await press(page, 'A', 280); await sleep(1900);
    for (let d = 0; d < e; d++) { await press(page, 'DOWN', 280); await sleep(450); }
    await sleep(350);
    const landed = await enterModeTail();
    row.mode = sceneName(landed);

    const icons = await cssIcons(page);
    const cur = await cssSlots(page);
    if (icons && cur) {
      const ic = randomIcon(icons);
      const r = await cssPickCharacter(page, ic);
      row.pick = r.ok ? `kind ${r.picked}` : `FAILED (wanted ${r.wanted}, got ${r.picked}, hand ${r.at})`;
      const before = await scene(page);
      await press(page, 'START', 300);
      await sleep(5000);
      const after = await scene(page);
      row.started = (after.mode !== before.mode || after.index !== before.index)
        ? `yes -> ${sceneName(after)}` : 'no (still on select)';
    } else {
      row.note = 'no character select here';
    }
    const live = await isAlive(page, 2000);
    row.fps = live?.alive ? `${live.fps.toFixed(0)}` : 'HUNG';
  } catch (err) {
    row.note = err.message.slice(0, 70);
  }
  const bad = [...new Set(logs.slice(mark).filter(l => BAD.test(l) && !NOISE.test(l)))];
  if (bad.length) row.note = (row.note ? row.note + '; ' : '') + bad[0].slice(0, 70);
  results.push(row);
  console.log(`  entry ${row.entry}: ${row.mode.padEnd(18)} pick=${String(row.pick).padEnd(26)} start=${String(row.started).padEnd(22)} fps=${row.fps} ${row.note}`);
}

// Confirm past any extra menu layer until the game leaves MENU.
async function enterModeTail() {
  for (let i = 0; i < 5; i++) {
    const s = await scene(page);
    if (s.mode !== 1) return s;
    await press(page, 'A', 280);
    await sleep(2200);
  }
  return await scene(page);
}

console.log('\n=== summary ===');
const played = results.filter(r => String(r.started).startsWith('yes'));
const hung = results.filter(r => r.fps === 'HUNG');
console.log(`${played.length} of ${results.length} entries started something; ${hung.length} hung.`);
await close();
