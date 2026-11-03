"""Read Delta Force gunsmith socket data straight out of the unpacked-share zips.

Source of truth: DataTables/WeaponPart/WeaponPartsMeshSocketNameInfo.json inside
`可用资源_DeltaForce_DataTables_001.zip`. Every weapon part mesh carries its own
mount sockets, and the values are in that part's local frame:
    +Y = towards the muzzle, +Z = up, +X = right, units = cm.

Attachment meshes are authored with their pivot on the attach face (a red dot's
`Riser_point`, a compensator's muzzle face), so a mount transform equals the
socket position expressed in the weapon component frame -- no extra offset.

The SCAR-H candidate mesh is rebuilt from these same source GLBs by
`assemble_scar_candidate.py`, which swaps Y/Z on the way into glTF; the import
swaps them back, so the merged mesh keeps the source frame (+Y forward, +Z up).
That is the frame the AK mount constants in `optic_calibration.py` already use:
the calibrated AK fore grip `(0, 34, 5.5)` lands on the same axis slots as the
computed `(0, 33.883, 7.631)`. `--frame raw` (default) therefore matches the
project; `--frame x-forward` only exists for a mesh imported without the swap.

Usage:
    python df_socket_mounts.py --json
    python df_socket_mounts.py --mount Optic --mount Han_ScarH_std_066_1P:For_point
"""
import argparse
import io
import json
import os
import zipfile

# 本机副本优先；网络共享在引擎自带 Python 中可能无法访问。
SHARES = [r'D:\司晨\源代码和素材\三角洲解包资产', r'\\Desktop-m9pa1sf\三角洲解包资产']
SHARE = next((s for s in SHARES if os.path.isdir(s)), SHARES[-1])
ZIP = '网盘上传压缩包/可用资源_DeltaForce_DataTables_001.zip'
MEMBER = 'WeaponPartsMeshSocketNameInfo.json'

# slot name -> (child part holding the socket, socket name)
# sockets live on the handguard / barrel, which are positioned by a receiver socket
SLOTS = {
    'Optic':       ('Rec_AR_ScarH_045_1P', 'Sco_point'),
    'OpticAlt':    ('Rec_AR_ScarH_045_1P', 'Mou_point'),
    'RearSight':   ('Rec_AR_ScarH_045_1P', 'Rea_point'),
    'ForeGrip':    ('Han_ScarH_std_066_1P', 'UnderBar_point'),
    'ForeGripAlt': ('Han_ScarH_std_066_1P', 'For_point'),
    'Bipod':       ('Han_ScarH_std_066_1P', 'Bip_point'),
    'Muzzle':      ('Bar_M-400_scarH_055_1P', 'Muzzle_point'),
    'MuzzleFlash': ('Bar_M-400_scarH_055_1P', 'MuzzleFlashSocket'),
}
# which receiver socket each child part hangs off
PARENT_SOCKET = {'Han_ScarH_std_066_1P': 'Han_point', 'Bar_M-400_scarH_055_1P': 'Bar_point'}


def load(path=None):
    path = path or os.path.join(SHARE, ZIP)
    with zipfile.ZipFile(path) as archive:
        name = next(n for n in archive.namelist() if n.endswith(MEMBER))
        return json.loads(archive.read(name))


def sockets(rows, mesh):
    for key, row in rows.items():
        if key.rsplit('/', 1)[-1].split('.')[0] == mesh:
            return dict(zip(row['SocketNames'], row['SocketLocation']))
    raise KeyError('mesh not found: ' + mesh)


def to_frame(point, frame):
    x, y, z = point['X'], point['Y'], point['Z']
    if frame == 'raw':
        return (x, y, z)
    if frame == 'x-forward':
        # glTF swap then Y-up -> Z-up import: source +Y forward becomes UE +X.
        return (y, x, z)
    raise ValueError(frame)


def resolve(rows, receiver, mesh, socket):
    """Socket position in the receiver's local frame."""
    point = sockets(rows, mesh)[socket]
    if mesh == receiver:
        base = {'X': 0.0, 'Y': 0.0, 'Z': 0.0}
    else:
        parent = PARENT_SOCKET.get(mesh)
        if parent is None:
            raise KeyError('no parent socket registered for ' + mesh)
        base = sockets(rows, receiver)[parent]
    return {axis: base[axis] + point[axis] for axis in 'XYZ'}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--receiver', default='Rec_AR_ScarH_045_1P')
    parser.add_argument('--frame', default='raw', choices=('x-forward', 'raw'))
    parser.add_argument('--mount', action='append',
                        help='SLOT from SLOTS, or MESH:SOCKET for an ad-hoc pair')
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    rows = load()['Rows']

    if args.mount:
        pairs = []
        for spec in args.mount:
            if spec in SLOTS:
                pairs.append((spec,) + SLOTS[spec])
            else:
                mesh, socket = spec.split(':')
                pairs.append((spec, mesh, socket))
    else:
        pairs = [(slot,) + target for slot, target in SLOTS.items()]

    result = {}
    for label, mesh, socket in pairs:
        point = resolve(rows, args.receiver, mesh, socket)
        result[label] = {
            'socket': socket, 'mesh': mesh,
            'source_cm': [round(point[a], 3) for a in 'XYZ'],
            'component_cm': [round(v, 3) for v in to_frame(point, args.frame)],
        }
    if args.json:
        print(json.dumps(result, indent=2))
    else:
        print('receiver %s  frame=%s' % (args.receiver, args.frame))
        for label, row in result.items():
            print('  %-12s %-26s %-20s -> (%8.3f, %8.3f, %8.3f) cm'
                  % (label, row['socket'], row['mesh'].replace('_1P', ''),
                     *row['component_cm']))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
