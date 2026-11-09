"""Fit Stinger's hit bodies and write them into the mesh Physics Asset.

Run inside Unreal Editor (Python), after the StingerTools module is built:
    exec(open(r"<project>/Scripts/Stinger/fit_hit_physics.py", encoding="utf-8").read())

The Physics Asset is the single source of hit shapes: bullets hit it directly and server-side rewind
(ULagCompensationComponent) records the same bodies. The project does not use ragdolls, so every body and
constraint is replaced.

Shapes, fitted to the LOD0 reference-pose vertices:
  * capsules for head, neck and limbs, along the bone -> child segment;
  * component-aligned boxes for the four torso bones (Hips/Spine/Spine1/Spine2, also covering the collar);
  * the backpack (vertices behind BACKPACK_Y) is not a hit zone, matching common extraction shooters.
Damage zones key off these bone names (ExtractionHealth::ArmorSlotForBone): Head -> helmet, Hips/Spine* -> armor.
"""
import json
import math
import os

MESH = "/Game/Characters/Stinger/Meshes/Gameplay/Stinger_3P_Textured/SkeletalMeshes/Stinger_3P_Textured"
BACKPACK_Y = -17.0  # component space: +Y is forward; vertices behind the back plate belong to the backpack
TORSO_X = 21.0  # torso boxes stop at the arm pits
# "joint": cylinder spans the full bone segment, caps overlap the neighbours (no gaps at elbows/knees).
# "fit": the whole capsule stays inside the segment ends (head top, finger tips, toes). Second value caps radius.
CAPSULES = {"Head": ("fit", 12.0), "Neck": ("joint", 6.5), "LeftArm": ("joint", 8.5), "RightArm": ("joint", 8.5),
            "LeftForeArm": ("joint", 7.0), "RightForeArm": ("joint", 7.0), "LeftHand": ("fit", 5.0),
            "RightHand": ("fit", 5.0), "LeftUpLeg": ("joint", 11.0), "RightUpLeg": ("joint", 11.0),
            "LeftLeg": ("joint", 8.5), "RightLeg": ("joint", 8.5), "LeftFoot": ("fit", 6.0),
            "RightFoot": ("fit", 6.0)}


def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def mul(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def norm(a): return math.sqrt(dot(a, a))
def unit(a): return mul(a, 1 / norm(a))


def qrot(q, v):
    x, y, z, w = q
    tx, ty, tz = 2 * (y * v[2] - z * v[1]), 2 * (z * v[0] - x * v[2]), 2 * (x * v[1] - y * v[0])
    return (v[0] + w * tx + (y * tz - z * ty), v[1] + w * ty + (z * tx - x * tz), v[2] + w * tz + (x * ty - y * tx))


def qinv(q): return (-q[0], -q[1], -q[2], q[3])


def q_from_z(d):
    """Shortest rotation taking +Z onto unit vector d."""
    c = d[2]
    if c < -0.9999: return (1.0, 0.0, 0.0, 0.0)
    q = (-d[1], d[0], 0.0, 1 + c)
    n = math.sqrt(sum(x * x for x in q))
    return tuple(x / n for x in q)


def pct(values, p):
    s = sorted(values)
    return s[min(len(s) - 1, max(0, int(round(p / 100 * (len(s) - 1)))))]


def seg_dist(p, a, b):
    ab = sub(b, a)
    t = max(0.0, min(1.0, dot(sub(p, a), ab) / dot(ab, ab)))
    return norm(sub(p, add(a, mul(ab, t))))


def fit(vertices, bones):
    """vertices: component-space ref pose points; bones: name -> {"t": xyz, "q": xyzw} component space."""
    P = lambda n: tuple(bones[n]["t"])
    head_top = (P("Head")[0], P("Head")[1] - 1.0, max(v[2] for v in vertices) - .5)
    toe = {s: add(P(s + "ToeBase"), mul(unit(sub(P(s + "ToeBase"), P(s + "Foot"))), 8)) for s in ("Left", "Right")}
    spec = {
        "Head": ("capsule", [(P("Head"), head_top)]),
        "Neck": ("capsule", [(P("Neck"), P("Head"))]),
        "Hips": ("box", [(P("Hips"), P("Spine")), (P("Hips"), P("LeftUpLeg")), (P("Hips"), P("RightUpLeg"))]),
        "Spine": ("box", [(P("Spine"), P("Spine1"))]),
        "Spine1": ("box", [(P("Spine1"), P("Spine2"))]),
        "Spine2": ("box", [(P("Spine2"), P("Neck")), (P("Spine2"), P("LeftArm")), (P("Spine2"), P("RightArm"))]),
    }
    for s in ("Left", "Right"):
        spec[s + "Arm"] = ("capsule", [(P(s + "Arm"), P(s + "ForeArm"))])
        spec[s + "ForeArm"] = ("capsule", [(P(s + "ForeArm"), P(s + "Hand"))])
        spec[s + "Hand"] = ("capsule", [(P(s + "Hand"), P(s + "HandMiddle3"))])
        spec[s + "UpLeg"] = ("capsule", [(P(s + "UpLeg"), P(s + "Leg"))])
        spec[s + "Leg"] = ("capsule", [(P(s + "Leg"), P(s + "Foot"))])
        spec[s + "Foot"] = ("capsule", [(P(s + "Foot"), toe[s])])

    # Each vertex belongs to the nearest bone segment; only capsules use this assignment.
    assigned = {k: [] for k in spec}
    for v in vertices:
        best, best_d = None, 1e9
        for k, (_, segs) in spec.items():
            for a, b in segs:
                d = seg_dist(v, a, b)
                if d < best_d: best, best_d = k, d
        assigned[best].append(v)

    torso_z = {"Hips": (P("Hips")[2] - 8.5, P("Spine")[2] + 2.3), "Spine": (P("Spine")[2] + 2.3, P("Spine1")[2]),
               "Spine1": (P("Spine1")[2], P("Spine2")[2]), "Spine2": (P("Spine2")[2], P("Neck")[2] + 1)}
    bodies = []
    for k, (kind, segs) in spec.items():
        bt, iq = P(k), qinv(tuple(bones[k]["q"]))
        if kind == "capsule":
            a, b = segs[0]
            length, axis = norm(sub(b, a)), unit(sub(b, a))
            mode, cap = CAPSULES[k]
            inner = [p for p in assigned[k] if 0 <= dot(sub(p, a), axis) <= length]
            radial = [norm(sub(sub(p, a), mul(axis, dot(sub(p, a), axis)))) for p in inner]
            r = min(cap, pct(radial, 75)) if radial else cap
            cylinder = length if mode == "joint" else max(0.0, length - 2 * r)
            centre = add(a, mul(axis, length / 2))
            bodies.append({"bone": k, "kind": "capsule", "center": qrot(iq, sub(centre, bt)),
                           "quat": q_from_z(qrot(iq, axis)), "radius": r, "length": cylinder})
        else:
            z0, z1 = torso_z[k]
            band = [p for p in vertices if z0 <= p[2] <= z1 and abs(p[0]) <= TORSO_X + 4 and p[1] >= BACKPACK_Y]
            xs, ys = [p[0] for p in band], [p[1] for p in band]
            lo = (max(pct(xs, 2), -TORSO_X), max(pct(ys, 2), BACKPACK_Y), z0)
            hi = (min(pct(xs, 98), TORSO_X), pct(ys, 98), z1)
            # Box axes follow the component (upright character) in the reference pose.
            bodies.append({"bone": k, "kind": "box", "center": qrot(iq, sub(mul(add(lo, hi), .5), bt)), "quat": iq,
                           "size": list(sub(hi, lo))})
    return bodies


def reference_pose(unreal, mesh):
    actor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(
        unreal.SkeletalMeshActor, unreal.Vector(0, 0, -100000))
    try:
        component = actor.skeletal_mesh_component
        component.set_skinned_asset_and_update(mesh)
        bones = {}
        for i in range(component.get_num_bones()):
            name = component.get_bone_name(i)
            t = component.get_socket_transform(name, unreal.RelativeTransformSpace.RTS_COMPONENT)
            q = t.rotation
            bones[str(name)] = {"t": [t.translation.x, t.translation.y, t.translation.z], "q": [q.x, q.y, q.z, q.w]}
        return bones
    finally:
        actor.destroy_actor()


def main():
    import unreal
    mesh = unreal.load_asset(MESH)
    physics = mesh.get_editor_property("physics_asset")
    vertices = json.loads(unreal.StingerAssetLibrary.describe_skeletal_vertices(mesh))
    bones = reference_pose(unreal, mesh)
    bodies = fit(vertices, bones)
    kinds, transforms, sizes = [], [], []
    for b in bodies:
        rotation = unreal.Quat(*b["quat"]).rotator()
        transforms.append(unreal.Transform(unreal.Vector(*b["center"]), rotation, unreal.Vector(1, 1, 1)))
        if b["kind"] == "capsule":
            kinds.append(1)
            sizes.append(unreal.Vector(b["radius"], b["length"], 0))
        else:
            kinds.append(2)
            sizes.append(unreal.Vector(*b["size"]))
    if not unreal.StingerAssetLibrary.replace_physics_bodies(physics, [b["bone"] for b in bodies], kinds, transforms,
                                                              sizes):
        raise RuntimeError("ReplacePhysicsBodies failed")
    unreal.EditorAssetLibrary.save_loaded_asset(physics, False)
    preview = os.path.join(unreal.Paths.project_saved_dir(), "HitPhysics", "stinger_hit_bodies.json")
    os.makedirs(os.path.dirname(preview), exist_ok=True)
    json.dump({"bones": bones, "bodies": bodies}, open(preview, "w"), indent=1)
    for b in bodies:
        print("HIT_BODY", b["bone"], b["kind"],
              "r=%.1f len=%.1f" % (b["radius"], b["length"]) if b["kind"] == "capsule"
              else "size=%.1f,%.1f,%.1f" % tuple(b["size"]))
    print("HIT_PHYSICS_WRITTEN", physics.get_path_name(), len(bodies))


if __name__ == "__main__":
    main()
