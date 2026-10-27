"""只读校验 DA_SpaceCenter_Ma1_Loot 里三类点位是否同时齐备（在编辑器里跑）。

运行（编辑器 Output Log，Cmd 模式）：
    py "D:/Users/SCSJ/BrokenEdge2.0/Scripts/Extraction/verify_loot_table.py"

为什么需要它：place_official_safes.py 和 add_loose_loot.py 都是
「保留其它点 + 重建自己那一类」的写法。如果编辑器内存里还挂着旧版本的资产
（git 把磁盘文件换过、但编辑器没有 Reload），脚本会读到旧的点列表，
保存时把别人新加的点一起写没。这个脚本不写任何东西，只报告现状。

三类点位来自三个不同来源，缺一类就是合并出了问题：
  key   —— 6 个钥匙房容器，钥匙卡体系（recipe 指向 DA_ProjectKey*Recipe）
  safe  —— 12 个官方保险箱（8 大 4 小），place_official_safes.py 写入，id 以 SRC_ 开头
  loose —— 9 个露天散落候选，add_loose_loot.py 写入，id 为 LOOSE_xx

用法上的两个时机：
  1) 跑脚本之前先跑一次：key=6 说明编辑器读的是磁盘上的新版本；key=0 说明是旧内存版本，
     此时任何保存都会覆盖别人的点，必须先 Reload。
  2) 两个脚本跑完再跑一次：三类全齐才会 PASS。
"""
import unreal

LAYOUT = '/Game/Extraction/Loot/DA_SpaceCenter_Ma1_Loot'
EXPECT = {'key': 6, 'large': 8, 'small': 4, 'loose': 9}


def text_of(value):
    """FName / soft object / asset 的 str() 形式随版本变化，统一按子串判断。"""
    return '' if value is None else str(value)


def main():
    layout = unreal.load_asset(LAYOUT)
    assert layout, '找不到布局资产：%s' % LAYOUT

    points = layout.get_editor_property('points')
    counts = {'key': 0, 'large': 0, 'small': 0, 'loose': 0}
    loose_enabled = 0
    legacy_safes = 0
    key_rows, safe_rows = [], []

    for point in points:
        kind = text_of(point.get_editor_property('kind'))
        recipe = text_of(point.get_editor_property('recipe'))
        point_id = text_of(point.get_editor_property('id'))

        if 'Key' in recipe:
            counts['key'] += 1
            key_rows.append(point_id)
        if 'LargeSafe' in kind or 'SmallSafe' in kind:
            # 官方点的 id 是 SRC_ 前缀；没有前缀的是导入时留下的旧保险箱点，脚本会替换掉。
            if 'SRC_' in point_id:
                counts['large' if 'LargeSafe' in kind else 'small'] += 1
                safe_rows.append(point_id)
            else:
                legacy_safes += 1
        if 'Loose' in kind:
            counts['loose'] += 1
            if point.get_editor_property('enabled'):
                loose_enabled += 1

    print('LOOT_TABLE_AUDIT layout=%s total_points=%d' % (LAYOUT, len(points)))
    print('  key   = %d (期望 %d)  例：%s' % (
        counts['key'], EXPECT['key'], ', '.join(key_rows[:3]) or '—'))
    print('  safe  = 大 %d / 小 %d (期望 %d / %d)  例：%s' % (
        counts['large'], counts['small'], EXPECT['large'], EXPECT['small'],
        ', '.join(safe_rows[:3]) or '—'))
    print('  loose = %d (期望 %d)，其中 enabled=%d' % (
        counts['loose'], EXPECT['loose'], loose_enabled))
    if legacy_safes:
        print('  注意：另有 %d 个未迁移的旧保险箱点（无 SRC_ 前缀），跑 place_official_safes.py 会替换掉'
              % legacy_safes)

    failed = []
    if counts['key'] != EXPECT['key']:
        failed.append('钥匙房容器 key=%d，期望 %d' % (counts['key'], EXPECT['key']))
    if counts['large'] != EXPECT['large'] or counts['small'] != EXPECT['small']:
        failed.append('官方保险箱 %d 大 + %d 小，期望 %d 大 + %d 小'
                      % (counts['large'], counts['small'], EXPECT['large'], EXPECT['small']))
    if counts['loose'] != EXPECT['loose']:
        failed.append('露天点 loose=%d，期望 %d' % (counts['loose'], EXPECT['loose']))
    if counts['loose'] and loose_enabled != counts['loose']:
        failed.append('有 %d 个露天点 enabled=False，抽取时会被跳过' % (counts['loose'] - loose_enabled))

    if not failed:
        print('LOOT_TABLE_AUDIT PASS 三类点位齐备，可以提交。')
        return

    print('LOOT_TABLE_AUDIT FAIL')
    for item in failed:
        print('  FAIL', item)
    if counts['key'] == 0:
        print('  提示：key=0 通常是编辑器在用旧的内存版本。在内容浏览器里对 '
              'DA_SpaceCenter_Ma1_Loot 执行 Reload（或关掉编辑器重开工程），再跑两个脚本。')
    if counts['loose'] == 0 and counts['key'] == EXPECT['key']:
        print('  提示：key 正常但 loose=0，说明 add_loose_loot.py 还没跑、或之后又被别的保存覆盖了。')


if __name__ == '__main__':
    main()
