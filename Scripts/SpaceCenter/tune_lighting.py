"""Brighten interiors of the production Space Center map without touching geometry.

Diagnosis (2026-11-03 dump): one 5 lux sun, sky light 1.0, nine point lights for the whole
map, and the unbound SC_Exposure volume forcing auto exposure bias -1. Interiors only get
Lumen bounce light and then the -1 bias darkens them further.

Changes (all on existing actors SC_Exposure / SC_Sky):
- auto exposure: histogram, bias 0, faster adaptation when walking indoors
- Lumen skylight leaking 0.15 so enclosed rooms keep some ambient light
- local exposure shadow contrast 0.6 to lift dark corners without flattening outdoors
- sky light intensity 1.5

    UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript \
        -script="Scripts/SpaceCenter/tune_lighting.py" -NullRHI -unattended -nosound
"""
import json

import unreal

LEVEL = '/Game/Maps/SpaceCenter/L_SpaceCenterExtraction'
POST = {
    'auto_exposure_method': unreal.AutoExposureMethod.AEM_HISTOGRAM,
    'auto_exposure_bias': 0.0,
    'auto_exposure_speed_up': 3.0,
    'auto_exposure_speed_down': 1.5,
    'lumen_skylight_leaking': 0.15,
    'local_exposure_shadow_contrast_scale': 0.6,
}
SKY_INTENSITY = 1.5

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level(LEVEL)
actors = {a.get_actor_label(): a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()}
post = actors['SC_Exposure']
settings = post.get_editor_property('settings')
for key, value in POST.items():
    settings.set_editor_property('override_' + key, True)
    settings.set_editor_property(key, value)
post.set_editor_property('settings', settings)
sky = actors['SC_Sky'].light_component
sky.set_editor_property('intensity', SKY_INTENSITY)
assert levels.save_current_level()
print('LIGHTING_TUNED', json.dumps({k: str(v) for k, v in POST.items()} | {'sky_intensity': SKY_INTENSITY}))
