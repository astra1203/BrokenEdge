/* 不运行 UE；验证教学材料来源与浏览器模拟的不变量。 */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const vm = require('node:vm');
const sim = require('./simulations.js');
const root = path.resolve(__dirname, '../..');
const ctx = {window: {}};
for (const file of ['content.js', 'sources.js']) vm.runInNewContext(fs.readFileSync(path.join(__dirname, file), 'utf8'), ctx);
const {chapters, refs} = ctx.window.TUTORIAL;
const snapshot = ctx.window.SOURCE_SNAPSHOT;
let checks = 0;

function test(name, fn) {
    fn();
    checks++;
    console.log('PASS ' + name);
}

test('8 complete chapters; valid unique routes, references and associated tests', () => {
    assert.equal(chapters.length, 8);
    assert.equal(new Set(chapters.map(c => c.id)).size, 8);
    const exposed = new Set(chapters.flatMap(c => [...c.extra, ...c.flows.flatMap(f => f.steps.map(s => s.ref))]));
    assert.deepEqual([...exposed].sort(), Object.keys(refs).sort(), 'all advertised functions accessible');
    for (const c of chapters) {
        assert.ok(c.topics.length >= 4 && c.questions.length >= 2 && c.tech.length >= 3 && c.flows.length >= 1);
        for (const f of c.flows) {
            assert.ok(f.note && f.steps.length >= 4);
            for (const s of f.steps) {
                assert.ok(refs[s.ref] && s.input && s.output && s.detail);
            }
        }
        for (const id of c.extra) assert.ok(refs[id]);
        for (const name of c.tests) assert.ok(fs.existsSync(path.join(root, 'Source/BrokenEdge/Tests', name)), name);
    }
});
test('Every snippet and line range exactly matches the current source bytes', () => {
    assert.deepEqual(Object.keys(refs).sort(), Object.keys(snapshot.functions).sort());
    for (const [file, expected] of Object.entries(snapshot.files)) assert.equal(crypto.createHash('sha256').update(fs.readFileSync(path.join(root, file))).digest('hex'), expected, file);
    for (const f of Object.values(snapshot.functions)) {
        const lines = fs.readFileSync(path.join(root, f.file), 'utf8').replace(/\r\n/g, '\n').split('\n');
        assert.equal(lines.slice(f.line - 1, f.endLine).join('\n'), f.code, f.symbol);
        assert.ok(f.code.includes(f.symbol + '(') && f.code.trimEnd().endsWith('}'), f.symbol);
    }
});
test('Offline entry resources exist; no external runtime URLs or fetch dependency', () => {
    const html = fs.readFileSync(path.join(__dirname, 'index.html'), 'utf8');
    for (const m of html.matchAll(/(?:src|href)="([^"]+)"/g)) {
        if (m[1].startsWith('#')) continue;
        assert.ok(!/^(?:https?:)?\/\//.test(m[1]), m[1]);
        assert.ok(fs.existsSync(path.resolve(__dirname, m[1])), m[1]);
    }
    const css = fs.readFileSync(path.join(__dirname, 'styles.css'), 'utf8');
    assert.ok(!/https?:\/\//.test(css));
    assert.ok(!/\bfetch\s*\(/.test(fs.readFileSync(path.join(__dirname, 'app.js'), 'utf8')));
});
test('Either player can win, stale versions are rejected, refreshing does not duplicate loot', () => {
    for (const first of ['A', 'B']) {
        const second = first === 'A' ? 'B' : 'A';
        let s = sim.inventory();
        const original = JSON.stringify(s);
        const accepted = sim.inventoryAct(s, 'take', first);
        assert.equal(JSON.stringify(s), original);
        s = sim.inventoryAct(accepted, 'take', second);
        assert.match(s.message, /容器已变化/);
        assert.equal(s.counts[second], 0);
        s = sim.inventoryAct(s, 'refresh', second);
        s = sim.inventoryAct(s, 'take', second);
        assert.match(s.message, /物品已变化/);
        assert.equal(s.counts.A + s.counts.B + Number(s.item), 1);
    }
});
test('Item conservation under all 1,024 five-operation client interleavings', () => {
    const ops = [['take', 'A'], ['take', 'B'], ['refresh', 'A'], ['refresh', 'B']];

    function explore(s, depth) {
        assert.equal(s.counts.A + s.counts.B + Number(s.item), 1);
        assert.ok(s.version >= 7 && s.version <= 8);
        if (depth) for (const [action, p] of ops) explore(sim.inventoryAct(s, action, p), depth - 1);
    }

    explore(sim.inventory(), 5);
});
test('Exit gates: time, switch, equipped pack and capacity prevent progress', () => {
    let s = sim.extractionAct(sim.extraction(), 'inside', true);
    assert.match(s.message, /秒后开放/);
    for (let i = 0; i < 3; i++) s = sim.extractionAct(s, 'tick');
    assert.match(s.message, /电闸/);
    s = sim.extractionAct(s, 'switched', true);
    assert.match(s.message, /丢掉背包/);
    s = sim.extractionAct(s, 'pack', false);
    assert.equal(s.phase, 'counting');
    s = sim.extractionAct(s, 'capacity', 0);
    assert.match(s.message, /名额已用尽/);
    assert.equal(s.deadline, null);
});
test('Exit leaving cancels the old deadline; re-entry requires the full hold; capacity consumed once', () => {
    let s = sim.extractionAct(sim.extraction(), 'ready');
    for (let i = 0; i < 4; i++) s = sim.extractionAct(s, 'tick');
    assert.equal(s.phase, 'counting');
    s = sim.extractionAct(s, 'inside', false);
    assert.equal(s.deadline, null);
    s = sim.extractionAct(s, 'inside', true);
    assert.equal(s.deadline, s.now + 5);
    for (let i = 0; i < 4; i++) s = sim.extractionAct(s, 'tick');
    assert.equal(s.capacity, 1);
    s = sim.extractionAct(s, 'tick');
    assert.equal(s.phase, 'extracted');
    assert.equal(s.capacity, 0);
    s = sim.extractionAct(s, 'tick');
    assert.equal(s.capacity, 0);
});
test('Exit closes at exact boundary, even if a hold was in progress', () => {
    let s = sim.extractionAct(sim.extraction(), 'ready');
    s.now = 44;
    s.deadline = 46;
    s = sim.extractionAct(s, 'tick');
    assert.match(s.message, /已关闭/);
    assert.equal(s.deadline, null);
    assert.equal(s.capacity, 1);
});
test('Commit publishes memory only after durable flush and cleans only after success', () => {
    let s = sim.persistence();
    for (let i = 0; i < 2; i++) s = sim.persistenceAct(s, 'next');
    assert.equal(s.durable, 0);
    assert.equal(s.memory, 0);
    s = sim.persistenceAct(s, 'next');
    assert.equal(s.durable, 3);
    assert.equal(s.memory, 0);
    assert.equal(s.clean, false);
    s = sim.persistenceAct(s, 'next');
    assert.equal(s.memory, 3);
    assert.equal(s.clean, false);
    s = sim.persistenceAct(s, 'next');
    assert.equal(s.clean, true);
});
test('Write failure preserves memory/character and blocks retry', () => {
    let s = sim.persistence('failure');
    for (let i = 0; i < 5; i++) s = sim.persistenceAct(s, 'next');
    assert.equal(s.faulted, true);
    assert.equal(s.durable, 0);
    assert.equal(s.memory, 0);
    assert.equal(s.clean, false);
    s = sim.persistenceAct(s, 'retry');
    assert.match(s.message, /拒绝重试/);
    assert.equal(s.durable, 0);
});
test('Lost-acknowledgement retries never duplicate the settled asset count', () => {
    let s = sim.persistence('duplicate');
    for (let i = 0; i < 5; i++) s = sim.persistenceAct(s, 'next');
    for (let i = 0; i < 20; i++) s = sim.persistenceAct(s, 'retry');
    assert.equal(s.durable, 3);
    assert.equal(s.memory, 3);
    assert.match(s.message, /幂等命中/);
});
console.log(`\n${checks} verification groups passed; ${Object.keys(refs).length} exact source snippets verified. These are tutorial checks, not Unreal runtime tests.`);
