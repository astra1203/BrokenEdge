"""Decode bounded source foliage records with independent physics cross-checks."""
import argparse
import collections
import math
import struct
from restoration_common import *
from source_packages import read_names


def quaternion(roll,pitch,yaw):
    # Unreal rotator convention: negative roll/pitch axes, positive yaw.
    r,p,y=[math.radians(v)/2 for v in [-roll,-pitch,yaw]]
    cr,sr,cp,sp,cy,sy=math.cos(r),math.sin(r),math.cos(p),math.sin(p),math.cos(y),math.sin(y)
    return dict(zip('XYZW',[sr*cp*cy-cr*sp*sy,cr*sp*cy+sr*cp*sy,cr*cp*sy-sr*sp*cy,cr*cp*cy+sr*sp*sy]))


def decode(package):
    obj=next(o for o in package['objects'] if o['Type']=='OpenWorldFoliageAsset')
    props=obj['Properties'];payload=Path(obj['source_tail']['file']).read_bytes();offset=0
    source=BASE/'Source'/ (package['package'].replace('/Game/','DeltaForce/Content/')+'.uasset')
    names=read_names(io_path(source).read_bytes())
    def take(fmt):
        nonlocal offset
        values=struct.unpack_from(fmt,payload,offset);offset+=struct.calcsize(fmt);return values
    rows=[];groups=[]
    for entry in props['FoliageData']:
        group_count=take('<i')[0]
        if not 0<=group_count<=1024:raise ValueError('Invalid group count')
        for group in range(group_count):
            count=take('<i')[0]
            if not 0<=count<=2000000:raise ValueError('Invalid instance count')
            if not count:continue
            x,y,z,radius,max_scale=take('<5f')
            assert all(math.isfinite(v) for v in [x,y,z,radius,max_scale]) and radius>0 and 0<max_scale<100
            packed=[take('<7h') for _ in range(count)]
            name_index,number,subpath_length=take('<3i')
            assert 0<=name_index<len(names) and number==0 and subpath_length==0
            mesh=canonical(names[name_index]);assert mesh.startswith('/Game/')
            material_count=take('<i')[0];assert 0<=material_count<64
            material_indices=list(take('<%di'%material_count))
            metadata=payload[offset:offset+33];offset+=33
            groups.append({'mesh':mesh,'count':count,'source_type':entry['SourcePath']['AssetPathName'],
                           'material_indices':material_indices,'opaque_flags':metadata.hex(),'radius':radius,'max_scale':max_scale})
            for index,values in enumerate(packed):
                position=[v+values[i]*radius/32767 for i,v in enumerate([x,y,z])]
                scale=values[6]*max_scale/127
                if abs(scale)>max_scale*1.01:raise ValueError('Invalid foliage scale '+str((mesh,values,max_scale)))
                q=quaternion(*(v*360/32767 for v in values[3:6]))
                source_id=package['package']+':'+str(entry['Hash'])+':'+str(group)+':'+str(index)
                rows.append({'id':identity(source_id),'source_object':source_id,'mesh':mesh,'source_package':package['package'],
                             'material_indices':material_indices,'properties':{},'existing':False,'foliage':True,
                             'transform':{'Translation':dict(zip('XYZ',position)),'Rotation':q,'Scale3D':dict.fromkeys('XYZ',scale)},
                             'packed_values':list(values),'position_quantization_bound_cm':radius/32767*math.sqrt(3)+.2})
    trailing=payload[offset:]
    trailer_version,trailer_count=struct.unpack_from('<ii',trailing)
    if trailer_version!=1 or trailer_count not in [0,16384] or len(trailing)!=8+trailer_count:
        raise ValueError('Unrecognized foliage trailer at '+str(offset)+': '+trailing[:80].hex())
    physics=props.get('PhysicsData',{});checks=[];used=set()
    if physics:
        base=physics['Position'];by_cell=collections.defaultdict(list)
        for index,body in enumerate(physics['SerialBodyInstances']):
            location=[base[k]+body['Location'][k] for k in 'XYZ']
            cell=tuple(math.floor(v/10) for v in location)
            by_cell[cell].append((index,location,body))
        for row in rows:
            t=row['transform'];position=[t['Translation'][k] for k in 'XYZ'];cell=[math.floor(v/10) for v in position]
            candidates=[b for dx in [-1,0,1] for dy in [-1,0,1] for dz in [-1,0,1]
                        for b in by_cell.get((cell[0]+dx,cell[1]+dy,cell[2]+dz),[]) if b[0] not in used]
            if not candidates:continue
            index,location,body=min(candidates,key=lambda b:math.dist(position,b[1]))
            error=math.dist(position,location)
            if error>max(2.,row['position_quantization_bound_cm']):continue
            # Match both position and independently serialized mesh identity.
            model=body['BodySetup']['ObjectName'].split("'")[1].rstrip("'")
            if not model.lower().startswith(row['mesh'].split('/')[-1].lower()):raise ValueError('Physics mesh mismatch')
            used.add(index)
            q1,q2=t['Rotation'],body['Rotation']
            angle=math.degrees(2*math.acos(min(1,abs(sum(q1[k]*q2[k] for k in 'XYZW')))))
            checks.append({'id':row['id'],'position_error_cm':error,'angle_error_deg':angle,
                           'scale_error':max(abs(t['Scale3D'][k]-body['Scaling'][k]) for k in 'XYZ')})
            if angle>.1:raise ValueError('Foliage rotation does not match independent physics')
            t['Translation']=dict(zip('XYZ',location));t['Rotation']={k:q2[k] for k in 'XYZW'};t['Scale3D']=body['Scaling']
            row['physics_validated']=True
            row['properties']['BodyInstance']={'CollisionProfileName':'BlockAll'}
    for row in rows:
        if not row.get('physics_validated'):row['properties']['BodyInstance']={'CollisionProfileName':'NoCollision'}
    return rows,{'package':package['package'],'groups':groups,'instances':len(rows),'physics_count':len(physics.get('SerialBodyInstances',[])),
                 'physics_matches':len(checks),'max_position_error_cm':max((c['position_error_cm'] for c in checks),default=0),
                 'max_angle_error_deg':max((c['angle_error_deg'] for c in checks),default=0),'checks':checks}


def main(tag,probe):
    out=WORK/'FoliageRuns'/tag;rows=[];reports=[];failures=[]
    packages=read(WORK/'ProbeRuns'/probe/'Inspect/Parsed/Packages.json')
    for p in packages:
        if not any(o['Type']=='OpenWorldFoliageAsset' for o in p.get('objects',[])):continue
        try:
            actors,report=decode(p);rows+=actors;reports.append(report)
        except Exception as ex:failures.append({'package':p['package'],'error':repr(ex)})
    write_json(out/'Decoded.json',rows);write_json(out/'Report.json',{'instances':len(rows),'packages':len(reports),'failures':failures,'validation':reports})
    print('FOLIAGE',len(rows),'PACKAGES',len(reports),'FAILED',failures,flush=True)
    if failures:raise RuntimeError('Foliage layout requires inspection')


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--tag',required=True);p.add_argument('--probe',default='Phase7CProbe05');a=p.parse_args();main(a.tag,a.probe)
