"""探查导入的 Soviet Military (UVB80) 关卡：规模、演员组成、碰撞覆盖。只读不保存。"""
import collections
import unreal

MAP = '/Game/UVB80/Levels/UVB80'
OUT = unreal.Paths.project_saved_dir() + 'Extraction/probe_uvb80.txt'
lines = []


def log(s):
    lines.append(str(s))
    unreal.log('[UVB80] ' + str(s))


world = unreal.EditorLoadingAndSavingUtils.load_map(MAP)
log('loaded=%s' % bool(world))
actors = unreal.EditorLevelLibrary.get_all_level_actors()
log('actors=%d' % len(actors))
cls = collections.Counter(a.get_class().get_name() for a in actors)
for k, v in cls.most_common(25):
    log('  %5d %s' % (v, k))

lo = [1e18] * 3
hi = [-1e18] * 3
meshes = collections.Counter()
no_col = collections.Counter()
missing = 0
for a in actors:
    for c in a.get_components_by_class(unreal.StaticMeshComponent):
        sm = c.static_mesh
        if not sm:
            missing += 1
            continue
        meshes[sm.get_path_name()] += 1
        o, e = a.get_actor_bounds(False)
        if e.x > 1e5 or e.y > 1e5:
            continue
        for i, (ov, ev) in enumerate(((o.x, e.x), (o.y, e.y), (o.z, e.z))):
            lo[i] = min(lo[i], ov - ev)
            hi[i] = max(hi[i], ov + ev)
        if c.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION:
            no_col[sm.get_name()] += 1
log('bounds min=%s max=%s size_m=%s' % ([round(x) for x in lo], [round(x) for x in hi],
                                        [round((h - l) / 100) for l, h in zip(lo, hi)]))
log('mesh_instances=%d unique=%d missing=%d' % (sum(meshes.values()), len(meshes), missing))

simple_missing = []
for path in meshes:
    sm = unreal.load_asset(path)
    bs = sm.get_editor_property('body_setup')
    if not bs:
        simple_missing.append(sm.get_name())
        continue
    agg = bs.get_editor_property('agg_geom')
    n = (len(agg.get_editor_property('box_elems')) + len(agg.get_editor_property('convex_elems')) +
         len(agg.get_editor_property('sphere_elems')) + len(agg.get_editor_property('sphyl_elems')))
    ctf = bs.get_editor_property('collision_trace_flag')
    if n == 0 and ctf != unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE:
        simple_missing.append(sm.get_name())
log('unique meshes without simple collision (walk-through for pawns): %d' % len(simple_missing))
for n in sorted(simple_missing)[:60]:
    log('    ' + n)
log('components with collision disabled: %d' % sum(no_col.values()))
for k, v in no_col.most_common(20):
    log('    %d %s' % (v, k))

lands = [a for a in actors if isinstance(a, unreal.Landscape) or a.get_class().get_name().startswith('Landscape')]
log('landscape actors=%d' % len(lands))
for l in lands:
    o, e = l.get_actor_bounds(False)
    log('  landscape %s origin=%s extent=%s' % (l.get_name(), o, e))

with open(OUT, 'w', encoding='utf-8') as f:
    f.write('\n'.join(lines))
