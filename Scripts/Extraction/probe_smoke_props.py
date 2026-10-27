"""列出 AExtractionSmoke 在 Python 侧暴露的属性名，确定 Cloud* 相关字段叫什么。"""
import unreal

cls = unreal.load_class(None, '/Script/BrokenEdge.ExtractionSmoke')
world = unreal.EditorLevelLibrary.get_editor_world()
actor = unreal.EditorLevelLibrary.spawn_actor_from_class(cls, unreal.Vector(0, 0, 200))

names = [n for n in dir(actor) if 'cloud' in n.lower() or 'fx' in n.lower() or 'radius' in n.lower()]
for n in names:
    try:
        v = getattr(actor, n)
        unreal.log('PROBE_ATTR {0} = {1}'.format(n, v))
    except Exception as exc:
        unreal.log('PROBE_ATTR {0} <callable/err {1}>'.format(n, exc))

unreal.log('PROBE_DONE count={0}'.format(len(names)))
unreal.EditorLevelLibrary.destroy_actor(actor)
