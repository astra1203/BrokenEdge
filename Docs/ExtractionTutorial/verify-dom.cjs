/* JSDOM interaction verification only. It cannot validate actual browser rendering. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {pathToFileURL} = require('node:url');
if (!process.argv[2]) throw new Error('Pass an installed jsdom module path. The tutorial itself has no dependency.');
const {JSDOM, VirtualConsole} = require(path.resolve(process.argv[2]));
const html = fs.readFileSync(path.join(__dirname, 'index.html'), 'utf8');
const scriptNames = [...html.matchAll(/<script src="([^"]+)"/g)].map(m => m[1]);
const errors = [];
let assertions = 0;
const check = (truth, message) => {
    assert.ok(truth, message);
    assertions++;
};

function create(url, blockedStorage = false) {
    const console = new VirtualConsole();
    console.on('jsdomError', e => errors.push(String(e)));
    const intervals = new Map();
    let nextId = 0;
    const dom = new JSDOM(html, {url, runScripts: 'outside-only', pretendToBeVisual: true, virtualConsole: console});
    const w = dom.window;
    w.matchMedia = () => ({
        matches: false, addEventListener() {
        }, removeEventListener() {
        }
    });
    w.scrollTo = () => {
    };
    w.HTMLElement.prototype.scrollIntoView = function () {
    };
    w.setInterval = fn => {
        const id = ++nextId;
        intervals.set(id, fn);
        return id;
    };
    w.clearInterval = id => intervals.delete(id);
    if (blockedStorage) Object.defineProperty(w, 'localStorage', {
        get() {
            throw new Error('storage unavailable');
        }
    });
    w.addEventListener('error', e => errors.push(String(e.error || e.message)));
    const style = w.document.createElement('style');
    style.textContent = fs.readFileSync(path.join(__dirname, 'styles.css'), 'utf8');
    w.document.head.append(style);
    for (const file of scriptNames) w.eval(fs.readFileSync(path.join(__dirname, file), 'utf8'));
    return {dom, w, intervals};
}

async function run() {
    for (const scenario of [{
        url: pathToFileURL(path.join(__dirname, 'index.html')).href,
        blocked: true
    }, {url: 'http://localhost:31991/index.html', blocked: false}]) {
        const {dom, w, intervals} = create(scenario.url, scenario.blocked);
        const d = w.document;
        const click = selector => {
            const e = d.querySelector(selector);
            check(e, 'click target ' + selector);
            e.click();
        };
        const change = (selector, value) => {
            const e = d.querySelector(selector);
            if (e.type === 'checkbox') e.checked = value; else e.value = value;
            e.dispatchEvent(new w.Event('change', {bubbles: true}));
        };
        const navigate = async hash => {
            if (w.location.hash === '#' + hash.replace(/^#/, '')) return;
            await new Promise(resolve => {
                w.addEventListener('hashchange', resolve, {once: true});
                w.location.hash = hash;
            });
        };
        check(d.querySelectorAll('.chapter-card').length === 8, 'overview has eight chapter cards');
        check(d.querySelectorAll('script[src^="http"]').length === 0, 'offline script references');
        for (const c of w.TUTORIAL.chapters) {
            await navigate(c.id);
            check(d.querySelector('h1').textContent === c.title, 'correct chapter title');
            check(d.querySelectorAll('.topic').length === 4, 'four implementation topics');
            check(d.querySelectorAll('.qa').length >= 2, 'interview questions');
            for (let fi = 0; fi < c.flows.length; fi++) {
                change('#flow-select', String(fi));
                check(d.querySelectorAll('.step').length === c.flows[fi].steps.length, 'all flow steps rendered');
                for (let i = 0; i < c.flows[fi].steps.length; i++) {
                    click(`[data-step="${i}"]`);
                    const f = w.SOURCE_SNAPSHOT.functions[c.flows[fi].steps[i].ref];
                    check(d.querySelector('.source-label').textContent.includes(f.symbol), 'selected function');
                    check(d.querySelectorAll('.code-line').length === f.endLine - f.line + 1, 'complete source lines');
                }
            }
            click('[data-control="reset"]');
            check(d.querySelector('output').textContent.startsWith('1/'), 'reset step');
            click('[data-control="play"]');
            check(intervals.size === 1, 'play starts a single timer');
            for (const fn of [...intervals.values()]) fn();
            check(d.querySelector('output').textContent.startsWith('2/'), 'play advances');
            click('[data-control="play"]');
            check(intervals.size === 0, 'pause clears timer');
            click('#mark-read');
            check(d.querySelector('#mark-read').textContent.includes('已读'), 'mark read');
            for (const extra of c.extra) {
                click(`[data-extra="${extra}"]`);
                check(d.querySelector('#extra-source').textContent.includes(w.TUTORIAL.refs[extra].symbol), 'extra function visible');
            }
            const ids = [...d.querySelectorAll('[id]')].map(e => e.id);
            check(new Set(ids).size === ids.length, 'unique DOM IDs');
        }
        check(d.querySelector('#progress-text').textContent === '8 / 8', 'all read counted');
        click('#theme');
        check(d.documentElement.dataset.theme === 'dark', 'theme toggled');
        if (!scenario.blocked) check(JSON.parse(w.localStorage.getItem('be-tutorial:completed')).length === 8, 'progress persisted');
        const search = d.querySelector('#search');
        search.value = 'ServerRequestShot';
        search.dispatchEvent(new w.Event('input'));
        check(d.querySelectorAll('.search-result').length > 0, 'function search results');
        await new Promise(resolve => {
            w.addEventListener('hashchange', resolve, {once: true});
            click('.search-result');
        });
        check(d.querySelector('.source-label').textContent.includes('ServerRequestShot'), 'search deep-links function');
        search.value = '<img src=x onerror=alert(1)>';
        search.dispatchEvent(new w.Event('input'));
        check(!d.querySelector('#search-results img'), 'search does not inject HTML');
        search.value = '';
        search.dispatchEvent(new w.Event('input'));
        await navigate('inventory');
        click('#take-A');
        click('#take-B');
        check(d.querySelector('.lab-status').textContent.includes('容器已变化'), 'inventory stale request');
        click('#refresh-B');
        click('#take-B');
        check(d.querySelector('.lab-status').textContent.includes('物品已变化'), 'inventory missing item');
        click('#lab-reset');
        check(d.querySelector('.server-container .big').textContent === 'v7', 'inventory reset');
        await navigate('extraction');
        click('#exit-ready');
        click('#exit-tick');
        click('#exit-leave');
        check(d.querySelector('.lab-status').textContent.includes('进度为零'), 'exit interrupted');
        change('#exit-inside', true);
        for (let i = 0; i < 5; i++) click('#exit-tick');
        check(d.querySelector('.lab-status').textContent.includes('Extracted'), 'exit success');
        check(d.querySelector('#exit-tick').disabled, 'completed exit disabled');
        click('#lab-reset');
        await navigate('persistence');
        change('#persistence-mode', 'failure');
        for (let i = 0; i < 3; i++) click('#store-next');
        check(d.querySelector('.lab-status').textContent.includes('Faulted'), 'write failure');
        click('#store-retry');
        check(d.querySelector('.lab-status').textContent.includes('拒绝重试'), 'failed storage blocks retry');
        change('#persistence-mode', 'duplicate');
        for (let i = 0; i < 5; i++) click('#store-next');
        click('#store-retry');
        check(d.querySelector('.lab-status').textContent.includes('幂等命中'), 'idempotent settlement');
        click('#lab-reset');
        check(d.querySelector('#store-next').disabled === false, 'persistence reset');
        click('[data-copy]');
        await Promise.resolve();
        const copy = d.querySelector('.manual-copy');
        check(copy.closest('details').open, 'clipboard fallback expanded');
        check(d.activeElement === copy, 'clipboard fallback selected input');
        await navigate('not-a-chapter');
        check(d.querySelectorAll('.chapter-card').length === 8, 'bad chapter falls back');
        await navigate('combat/999/999');
        check(d.querySelector('output').textContent === '1/8', 'invalid route indices bounded');
        click('[data-control="play"]');
        for (let i = 0; i < 10; i++) for (const fn of [...intervals.values()]) fn();
        check(intervals.size === 0, 'play stops at end');
        await navigate('ai');
        change('#flow-select', '1');
        click('[data-step="3"]');
        const deep = w.location.hash;
        await navigate('overview');
        await navigate(deep);
        check(d.querySelector('#flow-select').value === '1' && d.querySelector('output').textContent === '4/5', 'deep route restores flow and step: ' + JSON.stringify({
            deep,
            actualHash: w.location.hash,
            flow: d.querySelector('#flow-select').value,
            counter: d.querySelector('output').textContent,
            errors
        }));
        dom.window.close();
        console.log('PASS DOM scenario ' + (scenario.blocked ? 'file:// with unavailable storage/clipboard' : 'HTTP origin with persistent storage'));
    }
    assert.deepEqual(errors, [], 'No DOM runtime or CSS parser errors');
    console.log(`${assertions} DOM assertions passed. Browser screenshots, rendering and viewport overflow are NOT verified by JSDOM.`);
}

run().catch(e => {
    console.error(e);
    process.exitCode = 1;
});
