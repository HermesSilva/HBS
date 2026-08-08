#!/usr/bin/env python3
"""Build the complete human skeleton (.mass) from BodyParts3D.

A skeleton with a fused trunk is not a human skeleton: it cannot flex, twist or
carry a shoulder. This builds every bone that moves — 24 vertebrae, 24 ribs, the
sternum, both shoulder girdles, both hands and both feet — as its own body.

It works because BodyParts3D is one segmented body: every bone is a separate
mesh and they all share a frame, so each bone's position comes from the data and
nothing has to be registered or estimated. What the source does not have is
joints, and those come from tools/skeleton_tree.py, which is anatomy written
down: who hangs off whom, how the joint moves and how far.

Joint positions are not guessed either. A joint is placed at the interface
between the two bones' meshes — where they physically articulate.

Bones fused in the adult (the skull's 19) stay one rigid body carrying every
mesh, by this project's rule: if it does not move, it is not a joint. Their
geometry is all still there.

The skeleton is a generated artefact: fix this tool, never the .mass.

    python tools/build_skeleton_full.py <out.mass>
           [--bones data/atlas/bp3d] [--profile data/profiles/x.json] [--check]
"""
import sys
import os
import json

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import skeleton_tree as tree_mod

M_TO_CM = 100.0
BONE_DENSITY = 1900.0        # kg/m3, cortical+trabecular average


def load_mesh(path):
    pts, lines = [], []
    with open(path) as f:
        for line in f:
            lines.append(line)
            if line.startswith("v "):
                pts.append([float(x) for x in line.split()[1:4]])
    return pts, lines


def bounds(pts):
    lo = [1e9] * 3
    hi = [-1e9] * 3
    for p in pts:
        for i in range(3):
            lo[i] = min(lo[i], p[i])
            hi[i] = max(hi[i], p[i])
    return lo, hi


def interface(a, b):
    """Midpoint of the closest pair of points: where two bones articulate."""
    sa = max(1, len(a) // 300)
    sb = max(1, len(b) // 300)
    best, pa, pb = 1e18, a[0], b[0]
    for p in a[::sa]:
        for q in b[::sb]:
            d = (p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 + (p[2] - q[2]) ** 2
            if d < best:
                best, pa, pb = d, p, q
    return [(pa[i] + pb[i]) / 2.0 for i in range(3)]


def write_mesh(sources, out, lift_cm, scale):
    """Merge a body's meshes into one placed OBJ; returns its bounds in metres."""
    verts, norms, faces = [], [], []
    lo = [1e9] * 3
    hi = [-1e9] * 3
    for src in sources:
        vbase = nbase = 0
        with open(src) as f:
            content = f.readlines()
        vbase, nbase = len(verts), len(norms)
        for line in content:
            if line.startswith("v "):
                v = [float(x) * scale for x in line.split()[1:4]]
                v[1] += lift_cm
                for i in range(3):
                    lo[i] = min(lo[i], v[i]); hi[i] = max(hi[i], v[i])
                verts.append("v %.4f %.4f %.4f\n" % tuple(v))
            elif line.startswith("vn "):
                norms.append(line)
            elif line.startswith("f "):
                toks = []
                for tok in line.split()[1:]:
                    vi, _, ni = tok.partition("//")
                    toks.append("%d//%d" % (int(vi) + vbase, int(ni) + nbase)
                                if ni else str(int(vi) + vbase))
                faces.append("f " + " ".join(toks) + "\n")
    if not verts:
        return None
    with open(out, "w") as f:
        f.write("# %s\n" % ", ".join(os.path.basename(s) for s in sources))
        f.writelines(verts)
        f.writelines(norms)
        f.writelines(faces)
    return [x / M_TO_CM for x in lo], [x / M_TO_CM for x in hi]


def build(bonedir, outdir, profile):
    spec = tree_mod.tree()
    fused = tree_mod.FUSED

    # source meshes, in centimetres in the frame bp3d_prepare put them in
    raw = {}
    for bone, _p, _j, _l in spec:
        srcs = ([os.path.join(bonedir, m + ".obj") for m in fused[bone]]
                if bone in fused else [os.path.join(bonedir, bone + ".obj")])
        srcs = [s for s in srcs if os.path.exists(s)]
        if not srcs:
            continue
        pts = []
        for s in srcs:
            pts.extend(load_mesh(s)[0])
        raw[bone] = (srcs, pts)

    # scale to the profile, then stand it on the floor
    all_pts = [p for _s, pts in raw.values() for p in pts]
    lo, hi = bounds(all_pts)
    built_h = (hi[1] - lo[1]) / M_TO_CM
    scale = profile["height_m"] / built_h if built_h > 1e-6 else 1.0
    lift_cm = -lo[1] * scale

    nodes = []
    for bone, parent, jtype, limit in spec:
        if bone not in raw:
            continue
        srcs, pts = raw[bone]
        fname = "_%s.obj" % bone
        box = write_mesh(srcs, os.path.join(outdir, fname), lift_cm, scale)
        if not box:
            continue
        size = [max(1e-4, box[1][i] - box[0][i]) for i in range(3)]
        centre = [(box[0][i] + box[1][i]) / 2.0 for i in range(3)]

        if parent and parent in raw:
            mid = interface(pts, raw[parent][1])
            jpos = [mid[0] * scale / M_TO_CM,
                    (mid[1] * scale + lift_cm) / M_TO_CM,
                    mid[2] * scale / M_TO_CM]
        else:
            jpos = centre

        latin, pt = tree_mod.names(bone)
        node = {
            "id": bone,
            "parent": parent or "",
            "anatomy": {"latin": latin, "pt": pt},
            "body": {
                "type": "Box",
                "mass": BONE_DENSITY * size[0] * size[1] * size[2] * 0.35,
                "obj": "../atlas/bp3d/" + fname,
                "size": size,
                "contact": "calcaneus" in bone or "phalanx" in bone and "toe" in bone,
                "color": [0.92, 0.90, 0.85, 1.0],
                "transform": {"linear": [1, 0, 0, 0, 1, 0, 0, 0, 1],
                              "translation": centre},
            },
            "joint": {
                "type": jtype, "bvh": "",
                "transform": {"linear": [1, 0, 0, 0, 1, 0, 0, 0, 1],
                              "translation": jpos},
            },
        }
        if jtype == "Revolute":
            node["joint"].update({"axis": [0, 0, 1], "lower": -limit, "upper": 0.0})
        elif jtype == "Ball":
            node["joint"].update({"lower": [-limit] * 3, "upper": [limit] * 3})
        nodes.append(node)

    # bone mass is a fraction of body mass (~15%); scale the estimates to it
    target = profile["mass_kg"] * 0.15
    got = sum(n["body"]["mass"] for n in nodes) or 1.0
    for n in nodes:
        n["body"]["mass"] *= target / got
    return nodes, scale


def check(nodes):
    problems = []
    ids = {n["id"] for n in nodes}
    ys = [n["body"]["transform"]["translation"][1] for n in nodes]
    sizes = [n["body"]["size"][1] for n in nodes]
    floor = min(y - s / 2 for y, s in zip(ys, sizes))
    top = max(y + s / 2 for y, s in zip(ys, sizes))

    print("bones      : %d (%d named)" % (len(nodes),
          sum(1 for n in nodes if n["anatomy"]["pt"])))
    print("mass       : %.1f kg of bone" % sum(n["body"]["mass"] for n in nodes))
    print("height     : %.2f m" % (top - floor))
    joints = {}
    for n in nodes:
        joints[n["joint"]["type"]] = joints.get(n["joint"]["type"], 0) + 1
    print("joints     : %s" % ", ".join("%s %d" % kv for kv in sorted(joints.items())))

    if floor < -0.05 or floor > 0.30:
        problems.append("lowest bone at y=%.3f: not standing on the floor" % floor)
    for n in nodes:
        if n["parent"] and n["parent"] not in ids:
            problems.append("%s: parent %s missing" % (n["id"], n["parent"]))
        if not n["anatomy"]["pt"]:
            problems.append("%s has no anatomical name" % n["id"])
        if n["body"]["mass"] <= 0:
            problems.append("%s has no mass" % n["id"])
    # left/right symmetry across the sagittal plane (x=0)
    by = {n["id"]: n for n in nodes}
    for n in nodes:
        if not n["id"].startswith("right_"):
            continue
        other = by.get("left_" + n["id"][6:])
        if other is None:
            continue
        a = n["body"]["transform"]["translation"]
        b = other["body"]["transform"]["translation"]
        if abs(a[0] + b[0]) > 0.02 or abs(a[1] - b[1]) > 0.02:
            problems.append("%s / %s not mirrored" % (n["id"], other["id"]))

    if problems:
        print("\nPROBLEMS (%d) - fix the tool, not the .mass:" % len(problems))
        for p in problems[:25]:
            print("  - %s" % p)
    else:
        print("\nno problems detected")
    return problems


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        print(__doc__)
        return 1
    bonedir, profile_path = "data/atlas/bp3d", ""
    for i, a in enumerate(sys.argv):
        if a == "--bones" and i + 1 < len(sys.argv):
            bonedir = sys.argv[i + 1]
        if a == "--profile" and i + 1 < len(sys.argv):
            profile_path = sys.argv[i + 1]

    profile = {"name": "reference", "height_m": 1.75, "mass_kg": 75.0,
               "specific_tension_N_cm2": 60.0}
    if profile_path:
        with open(profile_path, encoding="utf-8") as f:
            profile.update({k: v for k, v in json.load(f).items()
                            if not k.startswith("_")})

    nodes, scale = build(bonedir, bonedir, profile)
    print("profile   : %s - %.2f m, %.1f kg (geometry x%.4f)"
          % (profile["name"], profile["height_m"], profile["mass_kg"], scale))

    model = {
        "massVersion": 1,
        "meta": {"name": "HBS skeleton - " + profile["name"], "unit": "m",
                 "specificTension_N_cm2": profile.get("specific_tension_N_cm2", 60.0),
                 "profile": profile},
        "skeleton": nodes,
        "muscles": [], "motions": [], "fills": [],
        "params": {}, "scene": {}, "training": {}, "env": {},
    }
    with open(args[0], "w", encoding="utf-8") as f:
        json.dump(model, f, indent=2, ensure_ascii=False)
    print("-> %s" % args[0])

    if "--check" in sys.argv:
        return 2 if check(nodes) else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
