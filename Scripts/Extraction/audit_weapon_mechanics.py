"""Offline evidence audit; never writes runtime snapshots or source archives."""
import hashlib
import json
from pathlib import Path
import zipfile


def walk(value, path=''):
    if isinstance(value, dict):
        for key, child in value.items():
            yield from walk(child, f'{path}/{key}')
    elif isinstance(value, list):
        for index, child in enumerate(value):
            yield from walk(child, f'{path}/{index}')
    else:
        yield path, value


def forbidden_mode(path):
    # A reference is checked explicitly, not selected by a broad filename search.
    lower = path.lower()
    return any(token in lower for token in ('/mp/', '_mp', '/mobile/', 'breakthrough'))


def assembly_graph(rows):
    nodes, edges, issues = {}, [], []
    for key, row in sorted(rows.items()):
        index = row['Index']
        identity = {field: row[field] for field in ('ItemId', 'ParentIndex', 'ParentSocketId')}
        node = nodes.setdefault(index, dict(identity, index=index, row_keys=[]))
        if any(node[field] != value for field, value in identity.items()):
            issues.append(f'conflicting_node:{index}:{key}')
        node['row_keys'].append(key)
        if row['Child']:
            edge = dict(parent=index, child=row['Child'], socket=row['SocketId'], row_key=key)
            if any(all(e[f] == edge[f] for f in ('parent', 'child', 'socket')) for e in edges):
                issues.append(f'duplicate_connection:{key}')
            if any(e['parent'] == index and e['socket'] == row['SocketId'] for e in edges):
                issues.append(f'duplicate_socket:{key}')
            edges.append(edge)
    for edge in edges:
        child = nodes.get(edge['child'])
        if child is None:
            issues.append(f'missing_child:{edge["row_key"]}')
        elif (child['ParentIndex'], child['ParentSocketId']) != (edge['parent'], edge['socket']):
            issues.append(f'parent_socket_mismatch:{edge["row_key"]}')
    for index, node in nodes.items():
        if node['ParentIndex'] and not any(e['child'] == index for e in edges):
            issues.append(f'orphan:{index}')
    visited, active = set(), set()

    def visit(index):
        if index in active:
            issues.append(f'cycle:{index}')
            return
        if index in visited:
            return
        active.add(index)
        for edge in edges:
            if edge['parent'] == index:
                visit(edge['child'])
        active.remove(index)
        visited.add(index)

    for index in nodes:
        visit(index)
    roots = [n['index'] for n in nodes.values() if not n['ParentIndex']]
    if len(roots) != 1:
        issues.append(f'root_count:{len(roots)}')
    return dict(nodes=list(nodes.values()), edges=edges, roots=roots, issues=sorted(set(issues)))


class Audit:
    def __init__(self, source):
        self.archives = {}
        self.inventory = {}
        self.cache = {}
        self.evidence = {}
        self.issues = []
        for path in sorted((source / '网盘上传压缩包').glob('*.zip')):
            archive = zipfile.ZipFile(path)
            self.archives[path.name] = archive
            self.inventory[path.name] = archive.namelist()

    def close(self):
        for archive in self.archives.values():
            archive.close()

    def document(self, archive, member):
        key = archive + '::' + member
        if key not in self.cache:
            raw = self.archives[archive].read(member)
            self.cache[key] = json.loads(raw)
        if key not in self.evidence:
            raw = self.archives[archive].read(member)
            self.evidence[key] = dict(archive=archive, member=member,
                                     sha256=hashlib.sha256(raw).hexdigest(), rows={})
        return key, self.cache[key]

    def table(self, name, family='main'):
        matches = [(a, n) for a, names in self.inventory.items() for n in names
                   if n.endswith('/' + name + '.json') and
                   ((family == 'main' and a.endswith('DataTables_001.zip') and 'Redirect' not in a)
                    or (family == 'base' and a.endswith('R13N_001.zip') and '/Base/' in n)
                    or (family == 'pc' and a.endswith('R13N_001.zip') and '/PC/' in n))]
        if len(matches) != 1:
            self.issues.append(dict(kind='table_candidates', table=name, family=family, candidates=matches))
            return None, {}
        key, data = self.document(*matches[0])
        return key, data.get('Rows', {})

    def select(self, table, field, values, family='main', required=True):
        key, rows = self.table(table, family)
        selected = {k: v for k, v in rows.items() if v.get(field) in values}
        if key:
            self.evidence[key]['rows'].update(selected)
        missing = [v for v in values if not any(r.get(field) == v for r in selected.values())]
        if required and missing:
            self.issues.append(dict(kind='missing_rows', table=table, field=field, values=missing))
        return [dict(source=key, row_key=k, value=v, status='source_direct') for k, v in sorted(selected.items())]

    def curve(self, reference, source, field_path):
        value = reference.get('ObjectPath', reference.get('AssetPathName', ''))
        if not value or value == 'None':
            return None
        path = value.split('.')[0]
        if path.startswith('/Game/'):
            path = 'DeltaForce/Content/' + path[len('/Game/'):]
        candidates = [(a, n) for a, names in self.inventory.items() for n in names
                      if n.endswith('/' + path + '.json')]
        result = dict(reference=reference, source=source, field_path=field_path,
                      candidates=candidates, status='reference_derived', runtime_semantics='unverified')
        if forbidden_mode(field_path + '/' + path):
            result['status'] = 'mode_conflict'
        elif len(candidates) != 1:
            result['status'] = 'missing_reference' if not candidates else 'ambiguous_reference'
        else:
            key, data = self.document(*candidates[0])
            self.evidence[key]['data'] = data  # Retain keys, tangents and extrapolation verbatim.
            result['target'] = key
            if data.get('Type') not in ('CurveFloat', 'CurveVector'):
                result['status'] = 'unexpected_asset_type'
        if result['status'] != 'reference_derived':
            self.issues.append(dict(kind='curve_reference', **result))
        return result

    def curves_in(self, record):
        results = []

        def scan(value, path):
            if isinstance(value, dict):
                if 'ObjectPath' in value or 'AssetPathName' in value:
                    if 'Curve' in path:
                        result = self.curve(value, record['source'], '/Rows/' + record['row_key'] + path)
                        if result:
                            results.append(result)
                    return
                for key, child in value.items():
                    scan(child, path + '/' + key)
            elif isinstance(value, list):
                for i, child in enumerate(value):
                    scan(child, path + '/' + str(i))

        scan(record['value'], '')
        return results

    def weapon(self, item, receiver):
        start_issues = len(self.issues)
        rec = self.select('RecFunctionTable', 'RecFunctionId', [receiver], 'base')
        if len(rec) != 1:
            return dict(status='unresolved_receiver', records=rec)
        preset_id = rec[0]['value']['DefaultPreset']
        presets = self.select('GunPresetTable', 'ItemId', [preset_id])
        if len(presets) != 1:
            return dict(status='unresolved_preset', records=presets)
        assembly = self.select('GunDescTable', 'DesId', [presets[0]['value']['DescId']])
        graph = assembly_graph({r['row_key']: r['value'] for r in assembly})
        base = self.select('WeaponAttributeTable', 'RecId', [receiver])
        parts, curves = [], []
        for node in graph['nodes']:
            definitions = self.select('PartsDataTable', 'ItemId', [node['ItemId']], 'base')
            part = dict(node=node['index'], item_id=node['ItemId'], definitions=definitions)
            if len(definitions) == 1:
                definition = definitions[0]['value']
                fid = definition['FunctionId_SOL']
                functions = self.select('PartsFunctionDataTable', 'FunctionId', [fid], required=False)
                rules = self.select('PartsRuleTable', 'RuleId', [definition['RuleId']], 'base', required=False)
                group_source, group_rows = self.table('PartsRuleGroupTable')
                groups = [dict(source=group_source, row_key=k, value=v, status='source_direct')
                          for k, v in sorted(group_rows.items()) if definition['RuleId'] in v['InstallRuleId']]
                if group_source:
                    self.evidence[group_source]['rows'].update({g['row_key']: g['value'] for g in groups})
                tunes = self.select('TunePartFunctionTable', 'TuneId', definition['TuneIds'], required=False)
                for tune in tunes:
                    curves.extend(self.curves_in(tune))
                part.update(sol_function_id=fid, functions=functions, install_rules=rules,
                            rule_groups=groups, tunes=tunes,
                            function_status='rows_found_semantics_unverified' if functions else 'no_rows_not_proven_noop')
                if not functions:
                    self.issues.append(dict(kind='unresolved_function', item=node['ItemId'], function_id=fid))
                for r in rules:
                    rule = r['value']
                    if rule['bIsGroup'] and not any(rule['ItemId'] in g['value']['ItemIds'] and
                                                   rule['SocketId'] == g['value']['SocketId'] for g in groups):
                        self.issues.append(dict(kind='unresolved_group_membership', row_key=r['row_key'],
                                                source=r['source'], rule_id=definition['RuleId']))
            parts.append(part)
        socket_ids = sorted({e['socket'] for e in graph['edges']})
        sockets = self.select('PartSocketsTable', 'SocketId', socket_ids)
        main_attributes = self.select('WeaponMainAttributeTable', 'RecId', [0, receiver], required=False)
        if not any(a['value']['RecId'] == 0 for a in main_attributes):
            self.issues.append(dict(kind='missing_common_attribute_rows', receiver=receiver))
        if len(base) == 1 and base[0]['value'].get('bIndependentMainAttributeCurve') and not any(
                a['value']['RecId'] == receiver for a in main_attributes):
            self.issues.append(dict(kind='missing_independent_attribute_rows', receiver=receiver))
        for attribute in main_attributes:
            curves.extend(self.curves_in(attribute))
        attackers, damage = [], []
        bullet, recoil = [], []
        if len(base) == 1:
            b = base[0]['value']
            attackers = self.select('AttackerValueTable', 'AttackerValueId', [b['AttackerValueId']])
            damage_ids = sorted({v for a in attackers for k, v in a['value'].items()
                                 if 'DamageId' in k and isinstance(v, int) and v})
            damage = self.select('DamagePointValue', 'AttackerValueId', damage_ids)
            bullet = self.select('WeaponBulletTable', 'BulletId', [b['BulletFlyingId']])
            aim = self.select('WeaponAimingTable', 'AimingId', [b['AimingId']])
            recoil_ids = [b['WaistShootRecoilId']] + [a['value']['RecoilId'] for a in aim]
            recoil = self.select('WeaponRecoilTable', 'RecoilId', recoil_ids, 'pc')
        fields = []
        for record in base + bullet + recoil + main_attributes:
            for path, value in walk(record['value']):
                fields.append(dict(source=record['source'], row_key=record['row_key'],
                                   field_path='/Rows/' + record['row_key'] + path, value=value,
                                   status='source_direct', units='not_proven_by_this_audit'))
        forbids = self.select('PartsForbidRuleTable', 'WeaponID', [receiver], required=False)
        return dict(item=item, receiver=receiver, preset_id=preset_id, receiver_records=rec,
                    preset_records=presets, assembly_records=assembly, graph=graph, base=base,
                    parts=parts, sockets=sockets, main_attributes=main_attributes, curves=curves,
                    forbid_rules=forbids, attackers=attackers, damage_candidates=damage,
                    bullet=bullet, recoil=recoil, fields=fields, issues=self.issues[start_issues:],
                    interpretation='References are derived from explicit IDs. Selection/operation order and mode dispatch are not proven.')

    def logic_search(self):
        terms = ['AbsoluteMappingThenScale', 'DeltaMappingThenScale', 'FunctionId_SOL',
                 'BaseMainAttributeValues', 'BlendMode_Dest', 'SourceRecoilToDegrees']
        result = dict(terms=terms, searched=[], hits=[])
        for archive, names in self.inventory.items():
            if not any(t in archive for t in ('独立文件', '客户端静态')):
                continue
            members = [n for n in names if n.endswith(('.lua', '.cpp', '.h', '.md', '.txt'))]
            result['searched'].append(dict(archive=archive, member_count=len(members),
                                          scope='lua/cpp/h/md/txt, at most 4 MiB per member'))
            for member in members:
                if self.archives[archive].getinfo(member).file_size > 4 * 1024 * 1024:
                    continue
                raw = self.archives[archive].read(member)
                found = [t for t in terms if t.encode() in raw]
                if found:
                    result['hits'].append(dict(archive=archive, member=member,
                                               sha256=hashlib.sha256(raw).hexdigest(), terms=found,
                                               encoding='lua_bytecode' if raw.startswith(b'\x1bLua') else 'not_interpreted',
                                               status='token_presence_only_not_execution_semantics'))
        return result


def build_report(source):
    audit = Audit(source)
    try:
        weapons = {item: audit.weapon(item, rec) for item, rec in [('AK', 18010000006), ('SCARH', 18010000021)]}
        return dict(schema=1, mode='PC_SOL_audit', source_build_date='unknown', weapons=weapons,
                    evidence=audit.evidence, issues=audit.issues, logic_search=audit.logic_search(),
                    unknowns=[
                        dict(topic='默认件功能空引用', missing='空功能行等同无操作的证明，或缺失的重定向／覆盖数据', policy='不凭空启用配件，也不将缺失功能默认为零修正'),
                        dict(topic='评分组合', missing='线性函数、曲线及配件操作的运行时选择和计算顺序', policy='保留现有基础参数，不把曲线试算值作为官方最终性能'),
                        dict(topic='DFM 后座', missing='角度单位与运行时组合实现', policy='保留明确标注的 0.1 兼容系数'),
                        dict(topic='BlendMode_Dest', missing='运行时插值与区间边界语义', policy='保留目标阶梯兼容规则'),
                        dict(topic='SOL 伤害', missing='GameMode01/02 的模式映射以及护甲／弹药应用顺序', policy='保留现有伤害流程，不把候选行直接认定为 SOL 数值')])
    finally:
        audit.close()


def markdown(report):
    lines = ['# AKM / SCAR-H 枪械数据取证报告', '',
             '本报告由离线审计生成；只证明快照中的记录与引用，不证明正式服运行公式。', '',
             '完整原始行、字段、成员路径及 SHA-256 见 [关系报告](audit.json)。`source_direct` 表示原始数据；'
             '`reference_derived` 表示按引用路径解析；运行语义未证实项不会用于修改游戏参数。', '',
             '## 默认配装与功能引用', '']
    for item, weapon in report['weapons'].items():
        if 'graph' not in weapon:
            lines.extend([f'### {item}', '', f'无法解析：{weapon["status"]}', ''])
            continue
        lines.extend([f'### {item}：机匣 {weapon["receiver"]}，默认预设 {weapon["preset_id"]}', '',
                      f'装配表 {len(weapon["assembly_records"])} 行归并为 {len(weapon["graph"]["nodes"])} 个节点、'
                      f'{len(weapon["graph"]["edges"])} 条连接；图检查问题：{len(weapon["graph"]["issues"])}。', '',
                      '| 节点 | 物品 ID | 父节点 / 槽位 | SOL 功能 ID | 功能行数 | 调校条数 |',
                      '| --- | --- | --- | --- | --- | --- |'])
        for part, node in zip(weapon['parts'], weapon['graph']['nodes']):
            lines.append(f'| {node["index"]} | {part["item_id"]} | {node["ParentIndex"]} / {node["ParentSocketId"]} | '
                         f'{part.get("sol_function_id", "未解析")} | {len(part.get("functions", []))} | {len(part.get("tunes", []))} |')
        lines.extend(['', '找到的 SOL 功能记录：', ''])
        found = [f for p in weapon['parts'] for f in p.get('functions', [])]
        for function in found:
            r = function['value']
            lines.append(f'- 行 `{function["row_key"]}`，功能 `{r["FunctionId"]}`：`{r["PartFunctionType"]}`，'
                         f'参数 `{r["Param1"]}` / `{r["Param2"]}` / `{r["Param3"]}`。')
        if not found:
            lines.append('- 无匹配行，不能推断所有默认件均不影响属性。')
        if weapon['base']:
            b = weapon['base'][0]['value']
            scores = ', '.join(f'{v["Key"]}={v["Value"]}' for v in b['BaseMainAttributeValues'])
            lines.extend(['', f'原始基础评分：{scores}；独立曲线开关：`{b["bIndependentMainAttributeCurve"]}`。'
                          '以下通用属性行是审计候选；函数与曲线的执行顺序仍未确认。'])
        lines.extend(['', '功能行数为 0 不代表已证明没有属性影响。所有函数参数保持原样，不推定运算优先级。', '',
                      '评分与曲线的关联：', '', '| 属性 ID | 名称 | 记录 RecId | 曲线规则 |', '| --- | --- | --- | --- |'])
        for record in weapon['main_attributes']:
            r = record['value']
            rules = ', '.join(f'{x["RuleKey"]}: {x["CurvingType"].split("::")[-1]}' for x in r['FunctionCurvingItems'])
            lines.append(f'| {r["MainAttrId"]} | {r["MainAttrName"]} | {r["RecId"]} | {rules or "无曲线；查看原始 FunctionItems"} |')
        lines.extend(['', f'曲线引用 {len(weapon["curves"])} 条；原始关键帧、切线和外推配置保存在 JSON evidence 中。', '',
                      '伤害仅列候选分支，尚不选择 SOL 最终值：', '', '| 行键 | 候选 ID | Damage | HealthDamage | ArmorDamage |',
                      '| --- | --- | --- | --- | --- |'])
        for record in weapon['damage_candidates']:
            r = record['value']
            lines.append(f'| {record["row_key"]} | {r["AttackerValueId"]} | {r.get("Damage")} | {r.get("HealthDamage")} | {r.get("ArmorDamage")} |')
        for issue in weapon['issues']:
            if issue['kind'] == 'missing_rows':
                lines.append(f'\n未解析引用：`{issue["table"]}.{issue["field"]}` → `{issue["values"]}`。')
        lines.append('')
    lines.extend(['## 未确认项与下一步', ''])
    for unknown in report['unknowns']:
        lines.append(f'- **{unknown["topic"]}**：缺少 {unknown["missing"]}。当前策略：{unknown["policy"]}。')
    lines.extend(['', '已检查 DataTables 与 R13N/Base、R13N/PC 表；按明确对象路径在压缩包目录中查找曲线。'
                  '逻辑检索覆盖独立文件与客户端静态逆向包中的限定文本／Lua 文件；命中只说明字节串存在，'
                  '不视为已读到计算实现，未执行源包脚本或解包二进制。', '',
                  f'审计问题 {len(report["issues"])} 项，逻辑关键词命中文件 {len(report["logic_search"]["hits"])} 个。'
                  '全部细节见 audit.json 的 issues、logic_search、unknowns。', '',
                  '下一步应先解决功能空引用与评分组合语义，再接入运行时；目前可可靠使用的是装配拓扑和显式引用，'
                  '不能承诺默认装配后的官方最终性能。', '', '## 复现', '', '```powershell',
                  'python Scripts/Extraction/import_weapon_mechanics.py --audit',
                  'python Scripts/Extraction/import_weapon_mechanics.py --audit --check',
                  'python -m unittest discover -s Scripts/Extraction -p test_weapon_mechanics_audit.py',
                  '```', '', '`--audit` 仅写 Docs/Extraction/WeaponMechanicsEvidence；`--audit --check` 重新读取源包并比对报告，不写文件。', ''])
    return '\n'.join(lines)


def run(source, output, check=False):
    report = build_report(source)
    artifacts = {'audit.json': json.dumps(report, ensure_ascii=False, sort_keys=True, indent=2) + '\n',
                 'README.md': markdown(report)}
    for name, text in artifacts.items():
        path = output / name
        if check:
            if not path.exists() or path.read_text(encoding='utf-8') != text:
                raise ValueError(f'Audit drift: {path}')
        else:
            output.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding='utf-8', newline='\n')
    print(f'Audit {"verified" if check else "written"}: {len(report["evidence"])} sources, {len(report["issues"])} explicit issues')
