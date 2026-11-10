/* 维护工具：提取真实 C++ 函数并生成离线快照。无需第三方依赖。 */
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const crypto = require('node:crypto');
const cp = require('node:child_process');
const root = path.resolve(__dirname, '../..');
const context = {window: {}};
vm.runInNewContext(fs.readFileSync(path.join(__dirname, 'content.js'), 'utf8'), context);
const {refs, chapters} = context.window.TUTORIAL;

function extract(text, symbol) {
    const escaped = symbol.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    // Definitions in these source files start in column zero and have a return type.
    // Exclude assignments/call statements so an earlier use cannot masquerade as a definition.
    const match = new RegExp('^[A-Za-z_][A-Za-z0-9_:<>*& \\t]*[ \\t]+' + escaped + '\\s*\\(', 'm').exec(text);
    if (!match) throw new Error('Definition not found: ' + symbol);
    const start = match.index;
    const brace = text.indexOf('{', start);
    let level = 0, mode = 'code', end = -1;
    for (let i = brace; i < text.length; i++) {
        const a = text[i], b = text[i + 1];
        if (mode === 'line') {
            if (a === '\n') mode = 'code';
            continue;
        }
        if (mode === 'comment') {
            if (a === '*' && b === '/') {
                mode = 'code';
                i++;
            }
            continue;
        }
        if (mode === 'string' || mode === 'char') {
            if (a === '\\') {
                i++;
                continue;
            }
            if ((mode === 'string' && a === '"') || (mode === 'char' && a === "'")) mode = 'code';
            continue;
        }
        if (a === '/' && b === '/') {
            mode = 'line';
            i++;
            continue;
        }
        if (a === '/' && b === '*') {
            mode = 'comment';
            i++;
            continue;
        }
        if (a === '"') {
            mode = 'string';
            continue;
        }
        if (a === "'") {
            mode = 'char';
            continue;
        }
        if (a === '{') level++;
        if (a === '}' && --level === 0) {
            end = i + 1;
            break;
        }
    }
    if (end < 0) throw new Error('Unclosed definition: ' + symbol);
    return {
        line: text.slice(0, start).split('\n').length,
        endLine: text.slice(0, end).split('\n').length,
        code: text.slice(start, end).replace(/\r\n/g, '\n')
    };
}

const files = {}, functions = {};
for (const [id, ref] of Object.entries(refs)) {
    const bytes = fs.readFileSync(path.join(root, ref.file));
    files[ref.file] = crypto.createHash('sha256').update(bytes).digest('hex');
    functions[id] = {...ref, ...extract(bytes.toString('utf8'), ref.symbol)};
}
for (const chapter of chapters) {
    for (const flow of chapter.flows) for (const step of flow.steps) if (!functions[step.ref]) throw new Error('Bad ref: ' + step.ref);
    for (const id of chapter.extra) if (!functions[id]) throw new Error('Bad extra: ' + id);
    for (const test of chapter.tests) if (!fs.existsSync(path.join(root, 'Source/BrokenEdge/Tests', test))) throw new Error('Missing test: ' + test);
}
const snapshot = {
    date: new Date().toISOString(),
    commit: cp.execFileSync('git', ['rev-parse', 'HEAD'], {cwd: root, encoding: 'utf8'}).trim(),
    sourceStatus: cp.execFileSync('git', ['status', '--porcelain', '--', ...Object.keys(files)], {
        cwd: root,
        encoding: 'utf8'
    }).trim(),
    files,
    functions
};
const out = path.join(__dirname, 'sources.js');
fs.writeFileSync(out, '/* 自动提取的工作区源码快照；不要手改。 */\nwindow.SOURCE_SNAPSHOT = ' + JSON.stringify(snapshot, null, 2) + ';\n');
console.log(`Extracted ${Object.keys(functions).length} functions from ${Object.keys(files).length} source files. All chapter/test references resolved.`);
