// Walk the menus broadly and classify every screen we can reach.
//
// The point is to find things that need fixing or implementing, so a probe
// reports more than "did the scene change": it watches the page log for the
// signals that actually mean something is missing -- assertions, files the
// disc lookup could not find, aborts, traps -- and checks the frame counter so
// "hung" is never confused with "waiting for input we cannot give".
//
// Every probe starts from a fresh boot. The menu cursor persists across a
// back-out, so reusing a session silently tests the wrong entry.
//
// Usage: node scene_crawl.mjs <build-dir> <disc.iso> [topEntries] [subEntries]
import {
  launchGame, bootToMainMenu, reboot, scene, sceneName, press, menuState, sleep, isAlive,
} from './nav.mjs';

const [buildDir, disc, topArg, subArg] = process.argv.slice(2);
if (!buildDir || !disc) {
  console.error('usage: scene_crawl.mjs <build-dir> <disc.iso> [topEntries] [subEntries]');
  process.exit(2);
}
const TOP = Number(topArg ?? 8);
const SUB = Number(subArg ?? 4);

const { page, logs, close } = await launchGame({ buildDir, disc });

// Log lines that mean something is wrong, as opposed to ordinary chatter.
const BAD = /assert|ASSERT|abort|Abort|trap|RuntimeError|no such file|failed|Failed|panic|unreachable|stuck:/;
const NOISE = /develop\.ini|usa\.ini|ScriptProcessorNode|sdl3 port|Failed to open file: SuperSmashBros|Failed to close file at idx/;
const badSince = (mark) => [...new Set(logs.slice(mark).filter(l => BAD.test(l) && !NOISE.test(l)))];

const findings = [];

async function classify(mark, label) {
  const bad = badSince(mark);
  const live = await isAlive(page, 1800);
  const where = sceneName(await scene(page));
  if (!live || !live.alive) {
    findings.push({ label, kind: 'HUNG', where, detail: 'the frame loop stopped' });
    return `${where} HUNG`;
  }
  if (bad.length) {
    findings.push({ label, kind: 'ERROR', where, detail: bad.slice(0, 3).join(' | ') });
    return `${where} alive ${live.fps.toFixed(0)}fps but logged: ${bad[0].slice(0, 120)}`;
  }
  return `${where} alive ${live.fps.toFixed(0)}fps`;
}

async function toMenu() {
  await reboot(page, disc);
  const r = await bootToMainMenu(page);
  return r.scene.mode === 1;
}

try {
  if (!(await bootToMainMenu(page)).scene.mode === 1) throw new Error('never reached the menu');

  for (let top = 0; top < TOP; top++) {
    if (top > 0 && !(await toMenu())) { console.log(`top ${top}: could not reach the menu`); continue; }

    for (let d = 0; d < top; d++) { await press(page, 'DOWN', 280); await sleep(500); }
    await sleep(400);
    const before = await menuState(page);
    await press(page, 'A', 280);
    await sleep(1800);
    const after = await scene(page);
    const m = await menuState(page);

    if (after.mode !== 1) {
      const mark = logs.length;
      console.log(`top ${top}: left MENU immediately -> ${await classify(mark, `top ${top}`)}`);
      continue;
    }
    if (m.curMenu === before.curMenu) { console.log(`top ${top}: nothing happened`); continue; }

    // Inside a submenu: try each of its entries.
    const sub = m.curMenu;
    process.stdout.write(`top ${top} (submenu ${sub}):`);
    for (let s = 0; s < SUB; s++) {
      if (s > 0) {
        if (!(await toMenu())) break;
        for (let d = 0; d < top; d++) { await press(page, 'DOWN', 280); await sleep(500); }
        await sleep(300);
        await press(page, 'A', 280); await sleep(1600);
        if ((await scene(page)).mode !== 1) break;
      }
      for (let d = 0; d < s; d++) { await press(page, 'DOWN', 280); await sleep(450); }
      await sleep(300);
      const mark = logs.length;
      await press(page, 'A', 280);
      await sleep(2400);
      const deep = await scene(page);
      if (deep.mode === 1) { process.stdout.write(` [${s}:menu]`); continue; }
      process.stdout.write(` [${s}:${await classify(mark, `top ${top} sub ${s}`)}]`);
    }
    process.stdout.write('\n');
  }

  console.log('\n=== things that need fixing or implementing ===');
  if (findings.length === 0) {
    console.log('nothing hung and nothing logged an error on the paths reached.');
  } else {
    for (const f of findings) console.log(`  [${f.kind}] ${f.label} at ${f.where}: ${f.detail}`);
  }
} catch (e) {
  console.log('crawl stopped:', e.message);
  console.log(logs.slice(-20).join('\n'));
} finally {
  await close();
}
