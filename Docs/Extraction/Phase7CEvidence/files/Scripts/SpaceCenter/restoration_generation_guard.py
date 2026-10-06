"""Keep earlier sample generators from overwriting an integrated reconstruction."""
from pathlib import Path
import unreal


def assert_legacy_map_allowed(operation):
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    if any('SpaceCenterRestoration' in [str(tag) for tag in actor.tags] for actor in actors):
        raise RuntimeError(operation+' cannot overwrite a Phase7C reconstruction. Use Scripts/SpaceCenter/run_restoration_editor.py integrate with its verified manifest; explicit restoration_overrides.json entries remain authoritative.')


def assert_legacy_sample_allowed():
    marker=Path(unreal.Paths.project_dir())/'Scripts/SpaceCenter/restoration_active.json'
    if marker.exists():
        raise RuntimeError('The reconstructed production scene is active. The historical sample generator is frozen to preserve its old baseline and shared assets. Use the Phase7C generation entry point.')
