(function () {
    'use strict';
    const {chapters, refs} = window.TUTORIAL;
    const snapshot = window.SOURCE_SNAPSHOT;
    const sim = window.TutorialSimulations;
    const main = document.querySelector('#main');
    document.querySelector('.skip').addEventListener('click', e => {
        e.preventDefault();
        main.focus();
        main.scrollIntoView({block: 'start'});
    });
    const esc = value => String(value ?? '').replace(/[&<>"']/g, c => ({
        '&': '&amp;',
        '<': '&lt;',
        '>': '&gt;',
        '"': '&quot;',
        "'": '&#39;'
    }[c]));
    const sideNames = {client: '客户端', server: '服务器', storage: '服务器存储'};
    const edgeNames = {
        call: '普通调用',
        'server-rpc': 'Server RPC →',
        'client-rpc': '← Client RPC',
        multicast: 'NetMulticast →',
        tick: 'Tick / 时间推进',
        callback: '引擎 / 异步回调',
        travel: '关卡切换后',
        replication: '属性复制 · OnRep',
        event: '独立事件',
        branch: '条件 / 决策分支',
        return: '返回外层调用'
    };
    const storage = {
        get(key, fallback) {
            try {
                return JSON.parse(localStorage.getItem('be-tutorial:' + key)) ?? fallback;
            } catch {
                return fallback;
            }
        }, set(key, value) {
            try {
                localStorage.setItem('be-tutorial:' + key, JSON.stringify(value));
            } catch {
            }
        }
    };
    let completed = storage.get('completed', []);
    if (!Array.isArray(completed)) completed = [];
    completed = completed.filter(x => chapters.some(c => c.id === x));
    let chapter = null, flowIndex = 0, stepIndex = 0, timer = null, toastTimer = null;
    let labState = {inventory: sim.inventory(), extraction: sim.extraction(), persistence: sim.persistence()};
    let theme = storage.get('theme', matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light');

    function setTheme() {
        document.documentElement.dataset.theme = theme;
        const b = document.querySelector('#theme');
        b.innerHTML = '◐ <span>' + (theme === 'dark' ? '浅色' : '深色') + '</span>';
        b.setAttribute('aria-label', '切换' + (theme === 'dark' ? '浅色' : '深色') + '主题');
    }

    setTheme();
    document.querySelector('#theme').addEventListener('click', () => {
        theme = theme === 'dark' ? 'light' : 'dark';
        storage.set('theme', theme);
        setTheme();
    });

    function toast(text) {
        const e = document.querySelector('#toast');
        e.textContent = text;
        e.classList.add('show');
        clearTimeout(toastTimer);
        toastTimer = setTimeout(() => e.classList.remove('show'), 2600);
    }

    function navigation() {
        document.querySelector('#navigation').innerHTML = `<a class="nav-link ${!chapter ? 'active' : ''}" href="#overview" ${!chapter ? 'aria-current="page"' : ''}><span class="nav-num">⌂</span>工程总览</a>` + chapters.map(c => `<a class="nav-link ${chapter === c ? 'active' : ''}" href="#${c.id}" ${chapter === c ? 'aria-current="page"' : ''}><span class="nav-num">${c.icon}</span>${c.short}${completed.includes(c.id) ? '<span class="nav-done" aria-label="已读">✓</span>' : ''}</a>`).join('');
        document.querySelector('#progress-text').textContent = `${completed.length} / 8`;
        document.querySelector('#progress').value = completed.length;
    }

    document.querySelector('#clear-progress').addEventListener('click', () => {
        completed = [];
        storage.set('completed', completed);
        navigation();
        if (chapter) updateReadButton();
        toast('阅读记录已重置');
    });

    function footer() {
        return `<footer class="page-footer"><span>BROKENEDGE / 搜打撤工程手册</span><span>工作区源码快照 · ${snapshot.date.slice(0, 10)} · ${snapshot.commit.slice(0, 8)}</span></footer>`;
    }

    const breadcrumb = name => `<div class="breadcrumb"><span>BROKENEDGE</span><span>/</span><span>搜打撤源码教学</span><span>/</span><span>${esc(name)}</span></div>`;

    function overview() {
        const last = storage.get('last', 'lifecycle');
        const resume = chapters.find(c => c.id === last) || chapters[0];
        const stages = [['lifecycle', '01', '⌂', '备战入局', 'Raid / Phase'], ['loot', '02', '◇', '搜索物资', 'Loot / Inventory'], ['combat', '03', '⌖', '战斗生存', 'Combat / Health'], ['extraction', '04', '↗', '条件撤离', 'Exit / Outcome'], ['persistence', '05', '▤', '仓库成长', 'Store / Progress']];
        return `${breadcrumb('工程总览')}
      <section class="hero"><div><div class="eyebrow">SOURCE CODE → GAMEPLAY</div><h1>读懂一场搜打撤，<br><em>从入局到带出。</em></h1><p class="lead">一份基于真实 C++ 实现的交互式工程手册。沿着玩家操作追踪函数调用，理解客户端、服务器与存档之间的协作，把“做了什么”讲成“为什么这样实现”。</p><div class="hero-actions"><a class="primary" href="#${resume.id}">${last === 'lifecycle' ? '开始源码之旅' : '继续阅读'} <span>↗</span></a><button class="secondary" data-scroll="chapters">浏览全部章节 ↓</button></div></div>
      <div class="hero-map" aria-hidden="true"><svg viewBox="0 0 280 280"><g fill="none" stroke="currentColor" stroke-width=".8"><circle cx="140" cy="140" r="112" opacity=".25"/><circle cx="140" cy="140" r="83" opacity=".2" stroke-dasharray="3 7"/><path d="M140 15v250M15 140h250" opacity=".2"/><path d="M62 174L95 105L158 80L209 128L180 201L120 212Z" stroke-dasharray="4 4"/><path d="M62 174L95 105L158 80L209 128" stroke-width="2"/><rect x="115" y="116" width="50" height="49" rx="3" fill="var(--bg)"/><path d="M128 142h24m-12-12v24" stroke-width="2"/></g><g fill="currentColor"><circle cx="62" cy="174" r="4"/><circle cx="95" cy="105" r="4"/><circle cx="158" cy="80" r="4"/><circle cx="209" cy="128" r="5"/></g><text x="38" y="193" font-size="8">SPAWN</text><text x="171" y="69" font-size="8">ENGAGE</text><text x="195" y="149" font-size="8">EXTRACT</text><text x="100" y="250" font-size="8" letter-spacing="2">RAID LIFECYCLE</text></svg></div></section>
      <div class="stats"><div class="stat"><b>08</b><span>功能章节<br>覆盖完整玩法链路</span></div><div class="stat"><b>${Object.keys(refs).length}</b><span>关键函数<br>可查看真实源码</span></div><div class="stat"><b>03</b><span>交互实验<br>动手理解边界条件</span></div><div class="stat"><b>C++</b><span>UNREAL ENGINE<br>源码学习 × 面试复盘</span></div></div>
      <div class="section-title"><h2>一件物品的旅程</h2><span>THE GAMEPLAY LOOP</span></div>
      <section class="journey-panel" aria-label="完整玩法流程"><div class="journey-heading"><p>从出战装备，到下一局的长期资产。</p><span class="badge">SERVER AUTHORITATIVE</span></div><div class="journey">${stages.map(s => `<a href="#${s[0]}"><span class="stage-number">${s[1]}</span><span class="stage-icon">${s[2]}</span><strong>${s[3]}</strong><small class="mono">${s[4]}</small></a>`).join('')}</div><div class="journey-footer"><b>↳ 贯穿全程</b> 稳定身份 GUID · 权威校验 · 网络复制 · 持久化后再发布结果</div></section>
      <div class="section-title" id="chapters"><h2>按模块，拆开看</h2><span>CHOOSE YOUR CHAPTER</span></div>
      <div class="chapter-grid">${chapters.map(c => `<a class="chapter-card" href="#${c.id}"><span class="num">${c.icon}</span><div><h3>${c.short}</h3><p>${c.summary}</p><div class="tags"><span class="tag">${c.category}</span><span class="tag">调用链 × ${c.flows.length}</span>${c.lab ? '<span class="tag">含交互实验</span>' : ''}</div></div><span class="arrow">↗</span></a>`).join('')}</div>
      <div class="section-title"><h2>先分清四种职责</h2><span>ARCHITECTURE AT A GLANCE</span></div><div class="architecture"><div><b>Client / UI</b><p>发出操作意图，预测表现，读取自己的快照。</p></div><div><b>GameMode</b><p>只在服务器编排规则与对局，不复制给客户端。</p></div><div><b>GameState / PlayerState</b><p>可复制的整局与单人状态，给界面提供读模型。</p></div><div><b>Persistence</b><p>服务器跨关卡存储，成功刷盘后发布资产变化。</p></div></div>
      <div class="note"><strong>如何使用：</strong>先读行为与类职责，再逐步播放调用链，点击函数查看实现；用实验观察失败分支，最后尝试独立回答面试问题。实验是浏览器教学模拟，不是 Unreal 运行证据。</div>${evidence(null)}${footer()}`;
    }

    function evidence(c) {
        const ids = c ? [...new Set([...c.flows.flatMap(f => f.steps.map(s => s.ref)), ...c.extra])] : Object.keys(refs);
        const files = [...new Set(ids.map(id => refs[id].file))];
        return `<details class="evidence"><summary>源码依据与验证边界 · ${files.length} 个引用文件</summary><p>提取时间：<span class="snapshot-time">${esc(snapshot.date)}</span><br>Git 提交：<code>${esc(snapshot.commit)}</code><br>引用源码状态：${snapshot.sourceStatus ? '含未提交源码，以下哈希对应提取时工作区。' : '提取时引用的源码文件无未提交修改。'} 页面正文为人工解释，代码块为逐字提取的函数快照。</p><p>源码能确认实现路径，不能证明所有网络条件、资产配置和运行边界均正常。测试文件的存在不等于测试已通过；本页面未重新执行 UE 游戏测试。</p>${c ? `<p>关联测试源码：</p><ul>${c.tests.map(t => `<li><a href="../../Source/BrokenEdge/Tests/${encodeURIComponent(t)}">${esc(t)}</a></li>`).join('')}</ul>` : ''}<p>引用文件 SHA-256（源码变化后运行本目录的 <code>node refresh-sources.cjs</code> 更新快照，并重新核对文字与调用链）：</p>${files.map(file => `<div class="hash-row"><b>${esc(file)}</b>${snapshot.files[file]}</div>`).join('')}</details>`;
    }

    function lesson(c) {
        const next = chapters[chapters.indexOf(c) + 1];
        return `${breadcrumb(c.short)}<header class="lesson-header"><div class="eyebrow">CHAPTER ${c.icon} / ${esc(c.subtitle)}</div><h1>${esc(c.title)}</h1><p class="lead">${esc(c.summary)}</p><div class="lesson-links"><button data-scroll="call-chain">函数调用链 ↓</button><button data-scroll="implementation">实现原理 ↓</button><button data-scroll="technology">使用的技术 ↓</button>${c.lab ? '<button data-scroll="experiment">动手实验 ↓</button>' : ''}<button data-scroll="interview">面试复盘 ↓</button></div></header>
      <div class="roles">${c.roles.map(([name, body]) => `<div class="role"><b>${esc(name)}</b><p>${esc(body)}</p></div>`).join('')}</div>
      <section id="call-chain"><div class="section-kicker">01 / FOLLOW THE CALL</div><h2>从一个操作，追到真实实现</h2><div id="workbench"></div></section>
      <section id="implementation"><div class="section-kicker">02 / UNDER THE HOOD</div><h2>功能是怎么实现的</h2><div class="topic-grid">${c.topics.map(([title, text], i) => `<article class="topic"><span class="topic-index">${String(i + 1).padStart(2, '0')} / IMPLEMENTATION</span><h3>${esc(title)}</h3><p>${esc(text)}</p></article>`).join('')}</div></section>
      <section id="technology"><div class="section-kicker">03 / ENGINEERING CHOICES</div><h2>用了什么技术，解决什么问题</h2><div class="tech-list">${c.tech.map(t => `<div class="tech-item"><b>${esc(t.name)}</b><p>${esc(t.why)}</p></div>`).join('')}</div></section>
      ${c.lab ? '<section id="experiment"><div id="lab-content"></div></section>' : ''}
      <div class="pitfalls"><h3>容易讲错的地方</h3><ul>${c.pitfalls.map(p => `<li>${esc(p)}</li>`).join('')}</ul></div>
      <section id="interview"><div class="section-kicker">04 / EXPLAIN IT YOURSELF</div><h2>试着把实现讲给另一个人</h2>${c.questions.map(([q, a]) => `<details class="qa"><summary>${esc(q)}</summary><p>${esc(a)}</p></details>`).join('')}</section>
      <section class="extra-panel"><div class="section-title"><h2>继续沿着源码探索</h2><span>RELATED FUNCTIONS</span></div><div class="function-list">${c.extra.map(id => `<button data-extra="${id}">${esc(refs[id].symbol)}</button>`).join('')}</div><div id="extra-source"></div></section>
      ${evidence(c)}<div class="chapter-end"><button id="mark-read"></button>${next ? `<a class="secondary" href="#${next.id}">下一章 · ${next.short} →</a>` : '<a class="secondary" href="#overview">返回工程总览 ↗</a>'}</div>${footer()}`;
    }

    function highlight(line) {
        const token = /(\/\/.*$|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|\b(?:void|bool|int32|uint32|float|double|const|auto|return|if|else|for|while|true|false|nullptr|static|class|struct|enum|this|break|continue|new)\b)/g;
        let html = '', cursor = 0;
        for (const m of line.matchAll(token)) {
            html += esc(line.slice(cursor, m.index));
            const cls = m[0].startsWith('//') ? 'comment' : /^["']/.test(m[0]) ? 'str' : 'kw';
            html += `<span class="${cls}">${esc(m[0])}</span>`;
            cursor = m.index + m[0].length;
        }
        return html + esc(line.slice(cursor));
    }

    function sourceView(id, step) {
        const f = snapshot.functions[id];
        return `<article class="function-detail">${step ? `<span class="badge ${step.side}">${sideNames[step.side]} / ${edgeNames[step.edge]}</span><h3>${esc(step.title)}</h3><p>${esc(step.detail)}</p><div class="io"><div><span>INPUT / 读取</span>${esc(step.input)}</div><div><span>OUTPUT / 状态变化</span>${esc(step.output)}</div></div>` : `<span class="badge">源码拓展阅读</span><h3>${esc(f.symbol.split('::').pop())}</h3>`}<div class="source-label"><code>${esc(f.symbol)}</code><br>${esc(f.file)}:${f.line}–${f.endLine}</div><div class="source-tools"><button data-copy="${id}">复制定位</button><a href="../../${f.file}" target="_blank" rel="noopener">打开源码文件 ↗</a></div><details class="source-details" open><summary>真实 C++ · ${f.endLine - f.line + 1} 行 · 点击折叠</summary><pre class="code-scroll" tabindex="0" aria-label="${esc(f.symbol)} 源码"><code>${f.code.split('\n').map((l, i) => `<span class="code-line" data-line="${f.line + i}">${highlight(l) || ' '}</span>`).join('')}</code></pre></details><details class="source-details"><summary>手动复制与来源</summary><input class="manual-copy" readonly aria-label="源码定位信息" value="${esc(f.file + ':' + f.line + ' · ' + f.symbol)}"><p class="source-label">本地文件链接由浏览器决定打开或下载；行号请在编辑器中定位。文件 SHA-256：<br><code>${snapshot.files[f.file]}</code></p></details></article>`;
    }

    function updateHash() {
        if (!chapter) return;
        const target = '#' + chapter.id + '/' + flowIndex + '/' + stepIndex;
        if (location.hash === target) return;
        try {
            history.replaceState(null, '', target);
        } catch {
            // Some file:// environments reject History API writes; fragment navigation remains available.
            location.replace(target);
        }
    }

    function renderWorkbench(preserveFocus = false) {
        const flow = chapter.flows[flowIndex];
        const step = flow.steps[stepIndex];
        const active = document.activeElement;
        const oldAction = preserveFocus && active ? active.dataset.control : null;
        document.querySelector('#workbench').innerHTML = `<div class="workbench"><div class="bench-head"><label>查看路径 <select id="flow-select" aria-label="选择函数调用路径">${chapter.flows.map((f, i) => `<option value="${i}" ${i === flowIndex ? 'selected' : ''}>${esc(f.title)}</option>`).join('')}</select></label><div class="play-controls"><button data-control="previous" aria-label="上一步" ${stepIndex === 0 ? 'disabled' : ''}>← 上一步</button><button data-control="play">${timer ? 'Ⅱ 暂停' : '▶ 播放'}</button><button data-control="next" aria-label="下一步" ${stepIndex === flow.steps.length - 1 ? 'disabled' : ''}>下一步 →</button><button data-control="reset">重置</button><output aria-label="当前步骤">${stepIndex + 1}/${flow.steps.length}</output></div></div><div class="legend"><span class="client"><i class="dot"></i>客户端</span><span class="server"><i class="dot"></i>服务器</span><span class="storage"><i class="dot"></i>服务器存储</span><span class="network">虚线标记 = 异步 / 调度</span></div><div class="bench-grid"><div class="steps" aria-label="函数调用步骤">${flow.steps.map((s, i) => `<button class="step ${i === stepIndex ? 'active' : ''}" data-step="${i}" aria-pressed="${i === stepIndex}"><span class="number">${i + 1}</span><span><span class="edge ${['server-rpc', 'client-rpc', 'multicast', 'replication'].includes(s.edge) ? 'remote' : ''} ${['server-rpc', 'client-rpc', 'multicast', 'replication', 'tick', 'callback', 'travel', 'event', 'branch'].includes(s.edge) ? 'async' : ''}">${edgeNames[s.edge]}</span><b>${esc(s.title)}</b><code>${esc(refs[s.ref].symbol)}</code><span class="step-meta ${s.side}"><i class="dot"></i>${sideNames[s.side]}</span></span></button>`).join('')}</div>${sourceView(step.ref, step)}</div><div class="flow-note">${esc(flow.note)}</div></div>`;
        if (oldAction) document.querySelector(`[data-control="${oldAction}"]`)?.focus({preventScroll: true});
        const list = document.querySelector('.steps');
        const selected = list.querySelector('.step.active');
        list.scrollTop = Math.max(0, selected.offsetTop - list.offsetTop - list.clientHeight / 2 + selected.clientHeight / 2);
        updateHash();
    }

    function stopPlayback() {
        if (timer) clearInterval(timer);
        timer = null;
    }

    function updateReadButton() {
        const b = document.querySelector('#mark-read');
        if (b && chapter) b.textContent = completed.includes(chapter.id) ? '✓ 已读 · 点击取消' : '标记本章已读 ✓';
    }

    function route() {
        stopPlayback();
        const [id, flow, at] = location.hash.replace(/^#/, '').split('/');
        chapter = chapters.find(c => c.id === id) || null;
        flowIndex = chapter && Number.isInteger(Number(flow)) && Number(flow) >= 0 && Number(flow) < chapter.flows.length ? Number(flow) : 0;
        stepIndex = chapter && Number.isInteger(Number(at)) && Number(at) >= 0 && Number(at) < chapter.flows[flowIndex].steps.length ? Number(at) : 0;
        main.innerHTML = chapter ? lesson(chapter) : overview();
        document.title = (chapter ? chapter.short : '搜打撤工程手册') + ' · BrokenEdge';
        navigation();
        if (chapter) {
            storage.set('last', chapter.id);
            renderWorkbench();
            updateReadButton();
            if (chapter.lab) renderLab();
        }
        window.scrollTo(0, 0);
        main.focus({preventScroll: true});
    }

    window.addEventListener('hashchange', () => {
        // Internal step changes must not rebuild the chapter or stop a running player.
        if (chapter && location.hash === '#' + chapter.id + '/' + flowIndex + '/' + stepIndex) return;
        route();
    });
    document.addEventListener('visibilitychange', () => {
        if (document.hidden && timer) {
            stopPlayback();
            if (chapter) renderWorkbench();
        }
    });

    function labLog(s) {
        return `<div class="lab-status" role="status" aria-live="polite">${esc(s.message)}</div><details><summary>查看本次实验记录 · ${s.log.length} 条</summary><ol class="lab-log">${s.log.map(l => `<li>${esc(l)}</li>`).join('')}</ol></details>`;
    }

    function renderLab(focusId) {
        const id = chapter.lab;
        const s = labState[id];
        let body, title, description;
        if (id === 'inventory') {
            title = '实验 01 / 两个人，同时拿一件物品';
            description = '教学模拟：两人都已揭示物品、空间充足、会话有效，每次提交使用新序号。这里只隔离观察容器版本，不模拟完整网格或网络。';
            const player = p => `<div class="lab-card"><h3 class="client">玩家 ${p}</h3><span class="big">v${s.clients[p]}</span><p>本地快照 · 拥有物品 ${s.counts[p]} 件</p><button id="take-${p}" data-lab-action="take" data-player="${p}">提交拾取</button><button id="refresh-${p}" data-lab-action="refresh" data-player="${p}">刷新快照</button></div>`;
            body = `<div class="lab-board">${player('A')}<div class="lab-card server-container"><h3 class="server">服务器容器</h3><span class="big">v${s.version}</span><div class="loot-token ${s.item ? '' : 'empty'}">${s.item ? '◇ 任务物品 × 1' : '∅ 物品已取走'}</div><p>物品归属：${s.owner ? '玩家 ' + s.owner : '容器'}<br>总数量始终为 1</p></div>${player('B')}</div>`;
        } else if (id === 'extraction') {
            title = '实验 02 / 为什么还不能撤离';
            description = '教学模拟：单个撤离点 t=3s 开放、t=45s 关闭、需要一个电闸和丢包，停留 5s；假定存活且保存成功。数值为教学场景，未读取地图资产。';
            const done = s.phase === 'extracted';
            const toggle = (key, text) => `<label><input id="exit-${key}" type="checkbox" data-exit="${key}" ${s[key] ? 'checked' : ''} ${done ? 'disabled' : ''}>${text}</label>`;
            body = `<div class="lab-controls">${toggle('inside', '位于撤离区')}${toggle('switched', '电闸已拉下')}${toggle('pack', '仍装备背包')}<label>可用名额 <select id="exit-capacity" data-exit="capacity" ${done ? 'disabled' : ''}><option value="0" ${s.capacity === 0 ? 'selected' : ''}>0</option><option value="1" ${s.capacity === 1 ? 'selected' : ''}>1</option></select></label></div><div class="exit-visual"><div class="exit-ring ${s.inside ? 'inside' : ''}">${done ? '✓' : s.now + 's'}</div><div><strong>${done ? '已撤离' : s.phase === 'counting' ? '停留计时中' : '等待条件'}</strong><p>服务器 t=${s.now}s · 截止 ${s.deadline === null ? '未设置' : 't=' + s.deadline + 's'}</p><progress aria-label="撤离停留进度" max="5" value="${done ? 5 : s.deadline === null ? 0 : 5 - (s.deadline - s.now)}"></progress></div></div><div class="lab-buttons"><button id="exit-ready" data-lab-action="ready" ${done ? 'disabled' : ''}>载入可撤离场景</button><button id="exit-tick" data-lab-action="tick" ${done ? 'disabled' : ''}>推进服务器时间 +1s</button><button id="exit-leave" data-lab-action="leave" ${done ? 'disabled' : ''}>离开区域，观察归零</button></div>`;
        } else {
            title = '实验 03 / 成功究竟发生在哪一步';
            description = '教学模拟：一次撤离带出 3 件物资，只展示单成员提交边界与重试。写入失败后保持 Faulted；更换场景或重置才开启新的模拟。';
            body = `<label class="lab-controls">选择场景 <select id="persistence-mode"><option value="normal" ${s.mode === 'normal' ? 'selected' : ''}>正常保存</option><option value="failure" ${s.mode === 'failure' ? 'selected' : ''}>写入 / Flush 失败</option><option value="duplicate" ${s.mode === 'duplicate' ? 'selected' : ''}>已保存，但确认未到达</option></select></label><div class="commit-pipeline">${sim.commitStages.map((t, i) => `<div class="commit-stage ${s.stage >= i ? (s.faulted && i === 2 ? 'failed' : 'passed') : ''}"><b>0${i + 1} ${s.stage === i ? '←' : ''}</b>${t}</div>`).join('')}</div><div class="store-values"><div>持久日志中的资产<strong>${s.durable} 件</strong></div><div>权威内存中的资产<strong>${s.memory} 件</strong></div><div>角色与背包<strong>${s.clean ? '已清理' : '保留'}</strong></div></div><div class="lab-buttons"><button id="store-next" data-lab-action="next" ${s.done ? 'disabled' : ''}>推进一步 →</button><button id="store-retry" data-lab-action="retry">重试同一成员结算</button></div>`;
        }
        document.querySelector('#lab-content').innerHTML = `<div class="lab"><div class="lab-badge">INTERACTIVE LAB / 浏览器教学模拟</div><div class="lab-top"><h2>${title}</h2><button id="lab-reset" data-lab-action="reset">重置实验</button></div><p class="lab-desc">${description}</p>${body}${labLog(s)}</div>`;
        if (focusId) document.getElementById(focusId)?.focus({preventScroll: true});
    }

    main.addEventListener('change', e => {
        if (e.target.id === 'flow-select') {
            stopPlayback();
            flowIndex = Number(e.target.value);
            stepIndex = 0;
            renderWorkbench();
            document.querySelector('#flow-select').focus({preventScroll: true});
        }
        if (e.target.dataset.exit) {
            const key = e.target.dataset.exit;
            labState.extraction = sim.extractionAct(labState.extraction, key, key === 'capacity' ? Number(e.target.value) : e.target.checked);
            renderLab(e.target.id);
        }
        if (e.target.id === 'persistence-mode') {
            labState.persistence = sim.persistence(e.target.value);
            renderLab(e.target.id);
        }
    });
    main.addEventListener('click', async e => {
        const b = e.target.closest('button');
        if (!b) return;
        if (b.dataset.scroll) document.getElementById(b.dataset.scroll)?.scrollIntoView({
            behavior: matchMedia('(prefers-reduced-motion: reduce)').matches ? 'instant' : 'smooth',
            block: 'start'
        });
        if (b.dataset.step !== undefined) {
            stopPlayback();
            stepIndex = Number(b.dataset.step);
            renderWorkbench();
            document.querySelector(`[data-step="${stepIndex}"]`).focus({preventScroll: true});
        }
        if (b.dataset.control) {
            const action = b.dataset.control;
            if (action === 'play') {
                if (timer) stopPlayback();
                else {
                    if (stepIndex === chapter.flows[flowIndex].steps.length - 1) stepIndex = 0;
                    timer = setInterval(() => {
                        if (stepIndex < chapter.flows[flowIndex].steps.length - 1) stepIndex++;
                        if (stepIndex === chapter.flows[flowIndex].steps.length - 1) stopPlayback();
                        renderWorkbench(true);
                    }, 3200);
                }
            } else {
                stopPlayback();
                if (action === 'reset') stepIndex = 0; else stepIndex = Math.max(0, Math.min(chapter.flows[flowIndex].steps.length - 1, stepIndex + (action === 'next' ? 1 : -1)));
            }
            renderWorkbench(true);
        }
        if (b.dataset.copy) {
            const f = snapshot.functions[b.dataset.copy];
            const text = `${f.file}:${f.line} · ${f.symbol}`;
            try {
                if (!navigator.clipboard) throw new Error('clipboard unavailable');
                await navigator.clipboard.writeText(text);
                toast('已复制源码定位');
            } catch {
                const input = b.closest('.function-detail').querySelector('.manual-copy');
                input.closest('details').open = true;
                input.focus();
                input.select();
                toast('已选中定位信息，请按 Ctrl+C 复制');
            }
        }
        if (b.dataset.extra) {
            document.querySelector('#extra-source').innerHTML = sourceView(b.dataset.extra);
            document.querySelector('#extra-source').scrollIntoView({block: 'nearest'});
        }
        if (b.id === 'mark-read') {
            if (completed.includes(chapter.id)) completed = completed.filter(id => id !== chapter.id); else completed.push(chapter.id);
            storage.set('completed', completed);
            navigation();
            updateReadButton();
        }
        if (b.dataset.labAction) {
            const id = chapter.lab, action = b.dataset.labAction;
            if (action === 'reset') labState[id] = sim[id](id === 'persistence' ? labState[id].mode : undefined);
            else if (id === 'inventory') labState[id] = sim.inventoryAct(labState[id], action, b.dataset.player);
            else if (id === 'extraction') labState[id] = sim.extractionAct(labState[id], action === 'leave' ? 'inside' : action, action === 'leave' ? false : undefined);
            else labState[id] = sim.persistenceAct(labState[id], action);
            renderLab(b.id);
        }
    });
    const search = document.querySelector('#search');
    search.addEventListener('input', () => {
        const q = search.value.trim().toLowerCase();
        const out = document.querySelector('#search-results');
        document.querySelector('#navigation').hidden = !!q;
        out.hidden = !q;
        if (!q) {
            out.innerHTML = '';
            return;
        }
        const result = [];
        for (const c of chapters) {
            if ([c.title, c.short, c.summary, ...c.topics.flat(), ...c.tech.flatMap(t => [t.name, t.why])].join(' ').toLowerCase().includes(q)) result.push({
                url: '#' + c.id,
                title: c.short,
                detail: c.subtitle
            });
            c.flows.forEach((f, fi) => f.steps.forEach((s, si) => {
                if ((refs[s.ref].symbol + ' ' + s.title + ' ' + s.detail).toLowerCase().includes(q)) result.push({
                    url: `#${c.id}/${fi}/${si}`,
                    title: refs[s.ref].symbol,
                    detail: c.short + ' · ' + s.title
                });
            }));
            c.extra.forEach(id => {
                if (refs[id].symbol.toLowerCase().includes(q)) result.push({
                    url: '#' + c.id,
                    title: refs[id].symbol,
                    detail: c.short + ' · 在「继续沿着源码探索」中查看'
                });
            });
        }
        const unique = result.filter((r, i, a) => a.findIndex(v => v.url === r.url && v.title === r.title) === i);
        out.innerHTML = `<p class="muted" style="font-size:10px" role="status">${unique.length} 个结果</p>` + (unique.length ? unique.slice(0, 35).map(r => `<a class="search-result" href="${r.url}">${esc(r.title)}<small>${esc(r.detail)}</small></a>`).join('') : '<p class="empty-search">没有匹配结果。试试“背包”、ServerRequestShot 或 Commit。</p>');
    });
    document.querySelector('#search-results').addEventListener('click', e => {
        const link = e.target.closest('a');
        if (!link) return;
        e.preventDefault();
        const target = link.getAttribute('href');
        search.value = '';
        search.dispatchEvent(new Event('input'));
        if (location.hash === target) route();
        else location.hash = target;
    });
    document.addEventListener('keydown', e => {
        if (e.key === '/' && !['INPUT', 'TEXTAREA', 'SELECT'].includes(document.activeElement.tagName)) {
            e.preventDefault();
            search.focus();
        }
        if (e.key === 'Escape' && document.activeElement === search) {
            search.value = '';
            search.dispatchEvent(new Event('input'));
            search.blur();
        }
    });
    route();
})();
