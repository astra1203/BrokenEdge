"""Place the Space Center key-room doors on their real loot anchors.

Anchors are the safe positions recorded in
Docs/Extraction/RestorationEvidence/Loot/SourcePoints.json (already converted to
project coordinates and floor-checked against L_SpaceCenterExtraction). Only the
rooms that have an anchor are placed here; 东区吊桥 / 员工通道 still need a
position because this source dump does not contain them.

Each door gets: required key id, room label, loot tier, a point light and the
container that spawns behind it. Existing actors tagged 'ExtractionKeyRooms' are
removed first so the script is idempotent.

Run with the editor closed:

    UnrealEditor-Cmd.exe BrokenEdge.uproject -unattended -NullRHI -run=pythonscript \
        -script="Scripts/Extraction/place_key_rooms.py"
"""
import hashlib
import json
import shutil
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
LEVEL = '/Game/Maps/SpaceCenter/L_SpaceCenterExtraction'
MAP = ROOT / 'Content/Maps/SpaceCenter/L_SpaceCenterExtraction.umap'
BACKUP = ROOT / 'Saved/Extraction/KeyRoomsBackup/L_SpaceCenterExtraction.umap'
TAG = 'ExtractionKeyRooms'

# room id, label, loot tier, x, y, floor z[, door yaw]
# EastBridge was placed in editor at the position the level designer stood on, so its
# yaw is pinned to 45.2 (door front facing the approach) instead of the auto probe.
ROOMS = [
    ('President', '总裁室会客室', 3, 6121, -748, 2604),
    ('Core', 'H3核心室', 3, 12015, -6350, 1899),
    ('Monitor', 'H1二楼监控室', 2, 1285, 1047, 1482),
    ('Contract', 'H4合同房', 1, 10177, 2366, 362),
    ('EastBridge', '东区吊桥', 2, 21221, 1076, 704.4, 45.2),
]
PENDING = ['StaffPassage']

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
door_class = unreal.load_class(None, '/Script/BrokenEdge.ExtractionKeyRoom')
assert door_class, 'ExtractionKeyRoom class missing (rebuild the module first)'

BACKUP.parent.mkdir(parents=True, exist_ok=True)
if not BACKUP.exists():
    shutil.copy2(MAP, BACKUP)

assert levels.load_level(LEVEL)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
for actor in actors.get_all_level_actors():
    if TAG in [str(t) for t in actor.tags]:
        actors.destroy_actor(actor)


def blocked(start, end):
    """True when something solid sits between the two points; unknown API state counts as free."""
    try:
        hit, _ = unreal.SystemLibrary.line_trace_single_by_channel(
            world, start, end, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [],
            unreal.DrawDebugTrace.NONE, True)
        return bool(hit)
    except Exception:
        return False


def approach_yaw(origin, reach=420.0, step=60.0):
    """Pick the facing with the most free space so the player can walk up to the door."""
    best_yaw, best_free = 90.0, -1.0
    for yaw in (0, 45, 90, 135, 180, 225, 270, 315):
        direction = unreal.Rotator(yaw=yaw).get_forward_vector()
        free = reach
        probe = step
        while probe <= reach:
            if blocked(origin, origin + direction * probe):
                free = probe - step
                break
            probe += step
        if free > best_free:
            best_yaw, best_free = float(yaw), free
    # Door front is local -Y; yaw + 90 points local -Y down the chosen direction.
    return best_yaw + 90.0, best_free


placements = []
for room, label, tier, x, y, floor_z, *fixed_yaw in ROOMS:
    if fixed_yaw:
        yaw, free = float(fixed_yaw[0]), -1.0
    else:
        yaw, free = approach_yaw(unreal.Vector(x, y, floor_z + 90))
    door = actors.spawn_actor_from_class(door_class, unreal.Vector(x, y, floor_z + 145),
                                        unreal.Rotator(yaw=yaw))
    assert door, room
    door.set_actor_label('KeyRoom_' + room)
    door.set_editor_property('tags', [TAG])
    door.set_editor_property('required_key', room)
    door.set_editor_property('room_label', label)
    door.set_editor_property('loot_tier', tier)
    door.set_editor_property('cache_label', label + ' · 保险柜')
    door.set_editor_property('loot_offset', unreal.Vector(0, 300, -120))
    door.set_editor_property('slide', unreal.Vector(180, 0, 0))

    light = actors.spawn_actor_from_class(unreal.PointLight, unreal.Vector(x, y, floor_z + 230))
    light.set_actor_label('KeyRoom_' + room + '_Light')
    light.set_editor_property('tags', [TAG])
    light.point_light_component.set_editor_property('intensity', 400.0)
    light.point_light_component.set_editor_property('attenuation_radius', 500.0)
    placements.append({'room': room, 'label': label, 'tier': tier,
                       'location': [x, y, floor_z + 145], 'yaw': round(yaw, 1), 'free_cm': free})

# The Phase 6B room predates card keys; give it a name so its prompt is not generic.
for actor in actors.get_all_level_actors():
    if actor.get_class().get_name() == 'ExtractionKeyRoom' and actor.get_editor_property('required_key').is_none():
        if str(actor.get_editor_property('room_label')) == '':
            actor.set_editor_property('room_label', '机密储藏室')

assert unreal.ExtractionNavigation.build_navigation(world)
assert levels.save_current_level()

report = {'map': LEVEL, 'placed': placements, 'pending': PENDING,
          'note': 'Anchors are loot-safe positions; doors stand in the room, not at an authored doorway.',
          'backup_sha256': hashlib.sha256(BACKUP.read_bytes()).hexdigest()}
out = ROOT / 'Saved/Extraction/KeyRooms.json'
out.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print('EXTRACTION_KEY_ROOMS_PLACED', len(placements))
