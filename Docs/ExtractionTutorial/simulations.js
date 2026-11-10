/* 仅用于教学的确定性状态模型，不连接 Unreal，也不执行真实物品事务。 */
(function (root) {
  function inventory() { return { version: 7, item: true, owner: null, clients: { A: 7, B: 7 }, counts: { A: 0, B: 0 }, message: 'A 与 B 都已揭示物品，并持有容器 v7。任选一人先提交。', log: [] }; }
  function inventoryAct(s, action, player) {
    const n = JSON.parse(JSON.stringify(s));
    if (action === 'refresh') { n.clients[player] = n.version; n.message = `玩家 ${player} 刷新为 v${n.version}，${n.item ? '物品仍在容器。' : '物品已被取走。'}`; }
    else if (n.clients[player] !== n.version) n.message = `拒绝 ${player}：容器已变化，请重试（请求 v${n.clients[player]} ≠ 服务端 v${n.version}）。`;
    else if (!n.item) n.message = `拒绝 ${player}：物品已变化或数量无效。最新快照中已没有这件物品。`;
    else { n.item = false; n.owner = player; n.counts[player]++; n.version++; n.message = `接受 ${player}：转移物品，容器更新为 v${n.version}。另一玩家仍持有旧快照。`; }
    n.log.push(n.message); return n;
  }
  function extraction() { return { now: 0, inside: false, switched: false, pack: true, capacity: 1, opensAt: 3, closesAt: 45, hold: 5, deadline: null, phase: 'waiting', message: '进入区域，满足条件后才开始停留计时。', log: [] }; }
  function reason(s) {
    if (!s.inside) return '尚未进入撤离区域';
    if (s.capacity <= 0) return '名额已用尽';
    if (s.now < s.opensAt) return `${s.opensAt - s.now} 秒后开放`;
    if (!s.switched) return '需拉下电闸 0/1';
    if (s.now >= s.closesAt) return '已关闭';
    if (s.pack) return '丢包撤：请先丢掉背包';
    return '';
  }
  function extractionAct(s, action, value) {
    const n = JSON.parse(JSON.stringify(s));
    if (n.phase === 'extracted') return n;
    if (action === 'tick') n.now += 1;
    else if (action === 'ready') { n.inside = true; n.switched = true; n.pack = false; n.now = n.opensAt; n.capacity = 1; n.deadline = null; n.log = []; }
    else n[action] = value;
    const blocked = reason(n);
    if (blocked) { n.deadline = null; n.phase = 'waiting'; n.message = blocked + '；停留进度为零。'; }
    else {
      if (n.deadline === null) n.deadline = n.now + n.hold;
      n.phase = 'counting'; n.message = `条件满足，截止 t=${n.deadline}s。还需停留 ${Math.max(0, n.deadline - n.now)} 秒。`;
      if (n.now >= n.deadline) { n.phase = 'extracted'; n.capacity--; n.message = '停留完成 → 冻结 → 结算并刷盘成功 → 消耗名额 → 发布 Extracted → 清理库存。'; }
    }
    if (n.message !== s.message) n.log.push(n.message);
    return n;
  }
  const commitStages = ['校验候选状态', '构造带 CRC 的日志记录', '写入并 Flush', '发布内存 State', '清理角色与背包'];
  function persistence(mode = 'normal') { return { mode, stage: -1, durable: 0, memory: 0, clean: false, faulted: false, outcome: 'None', done: false, message: '一次结算带出 3 件物资。点击“推进一步”观察成功边界。', log: [] }; }
  function persistenceAct(s, action) {
    const n = JSON.parse(JSON.stringify(s));
    if (action === 'retry') {
      if (n.faulted) n.message = '拒绝重试：存储已 Faulted，需恢复存储并重启；不能靠再次点击绕过故障。';
      else if (n.outcome === 'Extracted') n.message = '幂等命中：成员已有 Outcome，返回成功。持久资产仍为 3 件，不会再次入库。';
      else n.message = '尚未提交，先推进当前请求。';
    } else if (!n.done && !n.faulted) {
      n.stage++;
      if (n.stage === 0) n.message = '验证候选数据：身份、物品数量和来源合法；此时没有修改正式 State。';
      if (n.stage === 1) n.message = '构造日志：JSON 候选状态 + Magic / 版本 / 长度 / CRC。';
      if (n.stage === 2) {
        if (n.mode === 'failure') { n.faulted = true; n.done = true; n.message = '写入 / Flush 失败 → Faulted。内存不提交、角色不清理、撤离名额不消耗。'; }
        else { n.durable = 3; n.message = 'Flush 成功：完整结算记录已持久化，但下一行代码还未发布内存快照。'; }
      }
      if (n.stage === 3) { n.memory = 3; n.outcome = 'Extracted'; n.message = 'State = Candidate：权威内存现在与完整日志一致，结算返回成功。'; }
      if (n.stage === 4) { n.clean = true; n.done = true; n.message = n.mode === 'duplicate' ? '首次结算成功，但假设客户端未收到确认。点击“重试同一成员结算”，观察幂等结果。' : '外层收到成功：占用名额、发布撤离、清空背包并转入观战。'; }
    }
    if (n.message !== s.message || action === 'retry') n.log.push(n.message);
    return n;
  }
  const api = { inventory, inventoryAct, extraction, extractionAct, reason, persistence, persistenceAct, commitStages };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.TutorialSimulations = api;
})(typeof window === 'undefined' ? globalThis : window);
