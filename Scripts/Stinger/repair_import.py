"""Correct CUE4Parse PSA decoding. Keep source tracks and old assets for comparison."""
import sys, os, json, math, hashlib, struct
from pathlib import Path
import unreal
sys.path.insert(0, unreal.Paths.project_dir()+'Scripts/Stinger')
from import_assets import SOURCE, DEST, read_psa, compatible_frame_rate

SOURCE = Path(os.environ.get('STINGER_REPAIR_SOURCE', str(SOURCE)))
ANIMATION_ROOT = os.environ.get('STINGER_REPAIR_DEST', DEST+'/Animations/Corrected')
FIXED_LIMBS = os.environ.get('STINGER_REPAIR_FIXED_LIMBS') == '1'
MAPPING_REVISION = 5 if FIXED_LIMBS else 4

LIB=unreal.EditorAssetLibrary
REPORT=Path(os.environ.get('STINGER_REPAIR_REPORT', str(Path(unreal.Paths.project_saved_dir())/'Stinger'/'import_manifest_v3.json')))
manifest=json.loads(REPORT.read_text(encoding='utf-8')) if REPORT.exists() else {'version':3,'entries':{}}
targets=[]
for folder in ['Gameplay','Display']:
    for path in LIB.list_assets(DEST+'/Meshes/'+folder,recursive=True):
        mesh=unreal.load_asset(path)
        if isinstance(mesh,unreal.SkeletalMesh):
            component=unreal.SkeletalMeshComponent()
            component.set_skeletal_mesh_asset(mesh)
            names=[str(component.get_bone_name(i)) for i in range(component.get_num_bones())]
            targets.append((mesh,component,{name.casefold():component.get_ref_pose_transform(i) for i,name in enumerate(names)}))

def quat(values):
    length=math.sqrt(sum(v*v for v in values))
    if not math.isfinite(length) or length<1e-8: raise ValueError('Invalid quaternion')
    return unreal.Quat(*(v/length for v in values))

def decode_rotation(values,index):
    # Inverse of ActorXAnim.Export: mirror quaternion Y; root alone has W inverted.
    x,y,z,w=values
    return quat((x,-y,z,-w if index==0 else w))

def import_file(path):
    rel=path.relative_to(SOURCE).as_posix()
    data,chunks,bones,sequences=read_psa(path)
    signature=hashlib.sha256(json.dumps(bones,separators=(',',':')).encode()).hexdigest()[:16]
    bone_start,bone_size,_=chunks['BONENAMES']
    refs=[unreal.Transform(location=unreal.Vector(*b[2]),rotation=quat(b[3]).rotator(),
                          scale=unreal.Vector(*struct.unpack_from('<3f',data,bone_start+i*bone_size+108)))
          for i,b in enumerate(bones)]
    skeleton_path=ANIMATION_ROOT+'/Skeletons/SK_'+signature
    source_skeleton=unreal.load_asset(skeleton_path)
    if source_skeleton is None:
        source_skeleton=unreal.StingerAssetLibrary.create_source_skeleton(skeleton_path,
                          [b[0] for b in bones],[b[1] for b in bones],refs)
        if source_skeleton: assert LIB.save_loaded_asset(source_skeleton), skeleton_path
    if not source_skeleton: raise ValueError('Invalid source hierarchy')
    # Choose by names AND parent relationships, never by number of bones.
    def score(target):
        mesh,component,poses=target
        matched=sum(b[0].casefold() in poses and (i==0 or str(component.get_parent_bone(b[0])).casefold()==bones[b[1]][0].casefold())
                    for i,b in enumerate(bones))
        return matched/len(bones)
    target=max(targets,key=score)
    coverage=score(target)
    key_start,key_size,key_count=chunks['ANIMKEYS']
    if key_size!=32: raise ValueError('Invalid key record size')
    result={'status':'written','mapping_revision':MAPPING_REVISION,'fixed_limb_translations':FIXED_LIMBS,'source_sha256':hashlib.sha256(data).hexdigest(),'skeleton_signature':signature,
            'source_skeleton':source_skeleton.get_path_name(),'coverage':coverage,'assets':[],'sequences':[]}
    for name,fps,first,frames in sequences:
        tracks={};zero_bones=[]
        for index,(bone,parent,position,rotation) in enumerate(bones):
            positions=[];rotations=[];scales=[]
            for frame in range(max(2,frames)):
                key_index=(first+min(frame,frames-1))*len(bones)+index
                if key_index>=key_count: raise ValueError('Key index out of range')
                values=struct.unpack_from('<8f',data,key_start+key_index*32)
                positions.append(unreal.Vector(values[0],-values[1],values[2]))
                rotations.append(decode_rotation(values[3:7],index))
                scale=(1,1,1)
                if 'SCALEKEYS' in chunks:
                    start,size,count=chunks['SCALEKEYS']
                    if size!=16 or key_index>=count: raise ValueError('Invalid scale chunk')
                    scale=struct.unpack_from('<3f',data,start+key_index*16)
                if not all(math.isfinite(v) for v in values+scale): raise ValueError('Non-finite animation key')
                if min(scale)<=0 and bone not in zero_bones: zero_bones.append(bone)
                scales.append(unreal.Vector(*scale))
            tracks[bone]=(positions,rotations,scales,index)
        modes=[('Source',source_skeleton,None)]
        if coverage>=.9 and not zero_bones:
            modes.append(('Mapped',target[0].get_editor_property('skeleton'),target[2]))
        entry={'name':name,'source_fps':fps,'frames':frames,'zero_scale_bones':zero_bones,
               'validation':'needs_additive_or_visibility_metadata' if zero_bones else 'numeric_passed',
               'visual_validation':'pending','assets':{}}
        for mode,skeleton,target_refs in modes:
            folder=ANIMATION_ROOT+'/'+('MappedV4' if mode=='Mapped' else mode)+'/'+hashlib.sha1(rel.encode()).hexdigest()[:8]
            asset_path=folder+'/'+name
            seq=unreal.load_asset(asset_path)
            if mode=='Source' and seq is not None:
                entry['assets'][mode]=asset_path;result['assets'].append(asset_path)
                continue
            if seq is None:
                factory=unreal.AnimSequenceFactory();factory.target_skeleton=skeleton
                if target_refs: factory.preview_skeletal_mesh=target[0]
                seq=unreal.AssetToolsHelpers.get_asset_tools().create_asset(name,folder,unreal.AnimSequence,factory)
            controller=seq.get_editor_property('controller');controller.open_bracket('Correct PSA',should_transact=False)
            try:
                unreal.StingerAssetLibrary.reset_animation(seq)
                rate=compatible_frame_rate(fps,frames)
                controller.set_frame_rate(unreal.FrameRate(rate.numerator,rate.denominator),should_transact=False)
                controller.set_number_of_frames(unreal.FrameNumber(max(1,frames-1)),should_transact=False)
                for bone,(positions,rotations,scales,index) in tracks.items():
                    if target_refs:
                        if bone.casefold() not in target_refs: continue
                        source_ref=refs[index];target_ref=target_refs[bone.casefold()]
                        # Preserve the motion delta in parent space and apply it to the target bind pose.
                        rotations=[q*source_ref.rotation.inversed()*target_ref.rotation for q in rotations]
                        positions=[target_ref.translation+(p-source_ref.translation) for p in positions]
                        if FIXED_LIMBS and bone in ('LeftForeArm','RightForeArm','LeftHand','RightHand'):
                            positions=[target_ref.translation for _ in positions]
                        scales=[s*target_ref.scale3d for s in scales]
                    controller.add_bone_curve(bone,should_transact=False)
                    if not controller.set_bone_track_keys(bone,positions,rotations,scales,should_transact=False):
                        raise ValueError('Failed track '+bone)
                unreal.StingerAssetLibrary.finalize_animation(seq)
            finally: controller.close_bracket(should_transact=False)
            LIB.set_metadata_tag(seq,'StingerSource',rel)
            LIB.set_metadata_tag(seq,'StingerValidation',entry['validation'])
            assert LIB.save_loaded_asset(seq), asset_path
            entry['assets'][mode]=asset_path;result['assets'].append(asset_path)
        entry['imported_fps']=float(compatible_frame_rate(fps,frames))
        result['sequences'].append(entry)
    return result

def main():
    selected=set(os.environ.get('STINGER_REPAIR_NAMES','AR_M_1P_Idle01,AR_M_1P_AimIdle_Pose,AR_M_1P_Fire,AR_M_1P_ChangeClipFull,AR_M_1P_GetWeapon,AR_M_1P_Jumploop').split(','))
    processed=0
    for path in sorted(SOURCE.rglob('*.psa')):
        if '*' not in selected and path.stem not in selected: continue
        rel=path.relative_to(SOURCE).as_posix()
        previous=manifest['entries'].get(rel,{})
        if previous.get('status')=='written' and previous.get('mapping_revision')==MAPPING_REVISION and previous.get('source_sha256')==hashlib.sha256(path.read_bytes()).hexdigest():
            if all(LIB.does_asset_exist(p) for p in previous.get('assets',[])):
                continue
        try: manifest['entries'][rel]=import_file(path)
        except Exception as error:
            manifest['entries'][rel]={'status':'failed','error':str(error)}
            unreal.log_error('STINGER_REPAIR_FAILED '+path.name+' '+str(error))
        temporary=REPORT.with_suffix('.tmp.json')
        temporary.write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
        temporary.replace(REPORT)
        print('STINGER_REPAIR',path.name,manifest['entries'][rel]['status'])
        processed+=1
        if processed%20==0: unreal.SystemLibrary.collect_garbage()


if __name__ == '__main__':
    main()
