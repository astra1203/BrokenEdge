"""41 号：中控桥（中空桥）主体 SM_H_Building_FutureTech_Bridge_01E/01M 在还原时被设成无碰撞，桥上台阶踩上去会掉下去。
打开它们的碰撞（网格本身是复杂碰撞当简单碰撞用）；随机断桥事件运行时仍会把断开那段单独关掉。"""
import unreal
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level('/Game/Maps/SpaceCenter/L_SpaceCenterExtraction')
fixed = []
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    for c in a.get_components_by_class(unreal.StaticMeshComponent):
        m = c.get_editor_property('static_mesh')
        if m and m.get_name().startswith('SM_H_Building_FutureTech_Bridge_01') and c.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION:
            a.modify(); c.modify()
            c.set_collision_profile_name('BlockAll')
            c.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
            fixed.append('%s/%s' % (a.get_actor_label(), m.get_name()))
        # 还原时这些实例化组件还被设了 bDisableCollision（Python 看不到），不清掉就不会创建物理体。
        if m and m.get_name().startswith('SM_H_Building_FutureTech_Bridge_01') and isinstance(c, unreal.InstancedStaticMeshComponent)                 and 'disableCollision=1' in unreal.StingerAssetLibrary.describe_component_collision(c, unreal.Vector(), unreal.Vector(0, 0, 1)):
            a.modify()
            ok = unreal.StingerAssetLibrary.enable_instanced_collision(c)
            fixed.append('%s/%s instances(physics=%s)' % (a.get_actor_label(), m.get_name(), ok))
assert fixed or True  # 重跑时可能已全部修好
assert levels.save_current_level()
unreal.log('BRIDGEFIX ' + ', '.join(fixed))

# 桥面 01M 的复杂碰撞数据实际不存在（开了碰撞射线仍然穿过），给它一个贴合包围盒的简单盒子碰撞，
# 并让简单碰撞同时当复杂碰撞用。01M 是 3.5 m 宽、68 cm 厚的平板桥面，盒子就是它本身的形状。
lib = unreal.EditorAssetLibrary
meshes = set()
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    for c in a.get_components_by_class(unreal.StaticMeshComponent):
        m = c.get_editor_property('static_mesh')
        if m and m.get_name() == 'SM_H_Building_FutureTech_Bridge_01M':
            meshes.add(m.get_path_name())
sml = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem) or unreal.EditorStaticMeshLibrary
for path in meshes:
    m = unreal.load_asset(path)
    sml.remove_collisions(m)
    sml.add_simple_collisions(m, unreal.ScriptCollisionShapeType.BOX)
    body = m.get_editor_property('body_setup')
    body.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
    assert lib.save_loaded_asset(m, only_if_is_dirty=False)
    unreal.log('BRIDGEFIX box collision ' + path)
