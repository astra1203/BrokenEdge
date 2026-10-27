"""在 UE 编辑器 Python 中执行；只写入 /Game/Characters/Stinger。

GLB 保留蒙皮；PSA 使用 AnimationDataController，近似源帧率并保留缩放轨道。
不导入 cooked uasset/uexp 或尚未下载完成的文件。
"""
from pathlib import Path
import json
import os
import struct
import hashlib
from fractions import Fraction
import unreal

# 源目录不再写死：原始素材盘符会变（本地盘 / 共享盘），用 STINGER_SOURCE 覆盖。
SOURCE = Path(os.environ.get('STINGER_SOURCE', r"D:\司晨\源代码和素材\蜂医的人物模型和相关动画资产"))
DEST = "/Game/Characters/Stinger"
REPORT = Path(unreal.Paths.project_saved_dir()) / "Stinger" / "import_manifest.json"
IMPORT_VERSION = 2


def compatible_frame_rate(fps, frames):
    """用可精确表示的时长规避 UE 压缩器的小数帧断言。"""
    numerator = 65536
    while (frames - 1) * round(numerator / fps) >= 2**24:
        numerator //= 2
    return Fraction(numerator, max(1, round(numerator / fps)))


def import_models():
    tasks = []
    for name, folder in [("Stinger_3P_Textured", "Gameplay"),
                         ("Stinger_UI_BodyAndFace_Textured", "Display")]:
        path = next(SOURCE.rglob(name + ".glb"))
        dest = DEST + "/Meshes/" + folder
        if unreal.EditorAssetLibrary.list_assets(dest, recursive=True):
            continue
        task = unreal.AssetImportTask()
        task.filename = str(path)
        task.destination_path = dest
        task.automated = True
        task.save = True
        task.replace_existing = False
        tasks.append(task)
    if tasks:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    print("MODELS", unreal.EditorAssetLibrary.list_assets(DEST + "/Meshes", recursive=True))


def read_psa(path):
    data = path.read_bytes()
    chunks = {}
    offset = 0
    while offset + 32 <= len(data):
        name, _, size, count = struct.unpack_from("<20siii", data, offset)
        name = name.split(b"\0")[0].decode("ascii")
        start = offset + 32
        end = start + size * count
        if size < 0 or count < 0 or end > len(data):
            raise ValueError("Invalid PSA chunk " + name)
        chunks[name] = (start, size, count)
        offset = end
    if offset != len(data):
        raise ValueError("Trailing or truncated PSA data")
    start, size, count = chunks["BONENAMES"]
    if size != 120:
        raise ValueError("Unsupported bone record size")
    bones = []
    for i in range(count):
        off = start + size * i
        name = data[off:off + 64].split(b"\0")[0].decode("utf-8")
        parent = struct.unpack_from("<i", data, off + 72)[0]
        quat = struct.unpack_from("<4f", data, off + 76)
        pos = struct.unpack_from("<3f", data, off + 92)
        bones.append((name, parent, pos, quat))
    start, size, count = chunks["ANIMINFO"]
    if size != 168:
        raise ValueError("Unsupported animation record size")
    sequences = []
    for i in range(count):
        off = start + size * i
        name = data[off:off + 64].split(b"\0")[0].decode("utf-8")
        num_bones = struct.unpack_from("<i", data, off + 128)[0]
        fps = struct.unpack_from("<f", data, off + 152)[0]
        first, frames = struct.unpack_from("<ii", data, off + 160)
        if num_bones != len(bones) or frames < 1 or fps <= 0:
            raise ValueError("Invalid sequence metadata")
        sequences.append((name, fps, first, frames))
    return data, chunks, bones, sequences


def gameplay_mesh():
    for path in unreal.EditorAssetLibrary.list_assets(DEST + "/Meshes/Gameplay", recursive=True):
        obj = unreal.load_asset(path)
        if isinstance(obj, unreal.SkeletalMesh):
            return obj
    raise RuntimeError("Import gameplay GLB first")


def load_manifest():
    if REPORT.exists():
        return json.loads(REPORT.read_text(encoding="utf-8"))
    return {"source": str(SOURCE), "entries": {}}


def import_psa(path, mesh, manifest, overwrite=False):
    rel = path.relative_to(SOURCE).as_posix()
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    previous = manifest["entries"].get(rel, {})
    if not overwrite and previous.get("version") == IMPORT_VERSION and previous.get("sha256") == digest and previous.get("status") == "success":
        if all(unreal.EditorAssetLibrary.does_asset_exist(x) for x in previous["assets"]):
            return previous
    data, chunks, bones, sequences = read_psa(path)
    # 面部动画有独立的 352 骨架，不能丢掉面部轨道后写入游戏身体骨架。
    if len(bones) > 200:
        for candidate_path in unreal.EditorAssetLibrary.list_assets(DEST + "/Meshes/Display", recursive=True):
            candidate = unreal.load_asset(candidate_path)
            if isinstance(candidate, unreal.SkeletalMesh):
                probe = unreal.SkeletalMeshComponent()
                probe.set_skeletal_mesh_asset(candidate)
                if probe.get_num_bones() > 200:
                    mesh = candidate
                    break
    skeleton = mesh.get_editor_property("skeleton")
    component = unreal.SkeletalMeshComponent()
    component.set_skeletal_mesh_asset(mesh)
    mesh_bones = {str(component.get_bone_name(i)) for i in range(component.get_num_bones())}
    group = path.parts[len(SOURCE.parts)].split("_")[0]
    view = "FirstPerson" if "1P" in path.parts else "ThirdPerson" if "3P" in path.parts else "Other"
    # 目录层级哈希防止同名动画相互覆盖；文件名保留原始识别信息。
    folder = DEST + "/Animations/Imported/" + group + "/" + view + "/" + hashlib.sha1(rel.encode()).hexdigest()[:8]
    assets = []
    missing = [name for name, *_ in bones if name not in mesh_bones]
    key_start, key_size, key_count = chunks["ANIMKEYS"]
    if key_size != 32:
        raise ValueError("Unsupported animation key size")
    for seq_name, fps, first, frames in sequences:
        name = unreal.Name(seq_name)
        asset_path = folder + "/" + str(name)
        seq = unreal.load_asset(asset_path)
        if seq is None:
            factory = unreal.AnimSequenceFactory()
            factory.target_skeleton = skeleton
            factory.preview_skeletal_mesh = mesh
            seq = unreal.AssetToolsHelpers.get_asset_tools().create_asset(str(name), folder, unreal.AnimSequence, factory)
        controller = seq.get_editor_property("controller")
        controller.open_bracket("Import PSA", should_transact=False)
        try:
            unreal.StingerAssetLibrary.reset_animation(seq)
            # UE 的压缩器要求 float 时长乘以帧率后没有小数帧。
            # 以二次幂分子近似源帧率，确保 float 时长可以精确表示；误差写入清单。
            rate = compatible_frame_rate(fps, frames)
            controller.set_frame_rate(unreal.FrameRate(rate.numerator, rate.denominator), should_transact=False)
            controller.set_number_of_frames(unreal.FrameNumber(max(1, frames - 1)), should_transact=False)
            for bone_index, (bone_name, _, _, _) in enumerate(bones):
                if bone_name not in mesh_bones:
                    continue
                positions, rotations, scales = [], [], []
                for frame in range(max(2, frames)):
                    key_index = (first + min(frame, frames - 1)) * len(bones) + bone_index
                    if key_index >= key_count:
                        raise ValueError("Animation key index outside file")
                    x, y, z, qx, qy, qz, qw, _ = struct.unpack_from("<8f", data, key_start + key_index * 32)
                    # PSA 动画键与骨骼参考记录不同：键需镜像 Y，旋转同步变换。
                    positions.append(unreal.Vector(x, -y, z))
                    rotations.append(unreal.Quat(-qx, qy, -qz, qw))
                    if "SCALEKEYS" in chunks:
                        ss, sz, sc = chunks["SCALEKEYS"]
                        if sz != 16 or key_index >= sc:
                            raise ValueError("Invalid scale keys")
                        sx, sy, sz_, _ = struct.unpack_from("<4f", data, ss + key_index * 16)
                        scales.append(unreal.Vector(sx, sy, sz_))
                    else:
                        scales.append(unreal.Vector(1, 1, 1))
                controller.add_bone_curve(bone_name, should_transact=False)
                if not controller.set_bone_track_keys(bone_name, positions, rotations, scales, should_transact=False):
                    raise RuntimeError("Failed track " + bone_name)
            unreal.StingerAssetLibrary.finalize_animation(seq)
        finally:
            controller.close_bracket(should_transact=False)
        unreal.EditorAssetLibrary.set_metadata_tag(seq, "StingerSource", rel)
        unreal.EditorAssetLibrary.save_loaded_asset(seq)
        assets.append(asset_path)
    entry = {"status": "success", "version": IMPORT_VERSION, "sha256": digest, "assets": assets, "missing_bones": missing,
             "sequences": [{"name": n, "fps": f, "frames": count,
                            "imported_fps": float(compatible_frame_rate(f, count)),
                            "source_duration": max(0, count-1)/f,
                            "imported_duration": max(1, count-1)/float(compatible_frame_rate(f, count))}
                           for n, f, _, count in sequences]}
    manifest["entries"][rel] = entry
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    return entry


def batch(start=0, count=8):
    mesh = gameplay_mesh()
    manifest = load_manifest()
    files = sorted(SOURCE.rglob("*.psa"))
    for path in files[start:start + count]:
        try:
            entry = import_psa(path, mesh, manifest)
            print("IMPORTED", path.name, entry["sequences"])
        except Exception as error:
            manifest["entries"][path.relative_to(SOURCE).as_posix()] = {"status": "failed", "error": str(error)}
            REPORT.parent.mkdir(parents=True, exist_ok=True)
            REPORT.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
            unreal.log_error(str(path) + ": " + str(error))
    print("PROGRESS", min(start + count, len(files)), len(files))
