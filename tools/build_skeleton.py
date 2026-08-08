#!/usr/bin/env python3
"""Build an HBS skeleton (.mass) from an OpenSim model.

The skeleton is a generated artefact: fix this tool, never the .mass it writes,
and re-run. `--check` reports what came out so the tool can be corrected until
the skeleton is right.

Everything anatomical comes from the .osim — bone hierarchy, masses, joint
frames, axes and limits — and the meshes are the per-bone ones converted by
tools/vtp2obj.py. Bones and muscle paths therefore share one coordinate frame,
which is the whole reason for using this source.

Conventions this tool reconciles:
  * OpenSim meshes are local to their body; this project's loader wants them in
    the rest world pose, so each body's transform is baked into its mesh.
  * OpenSim measures in metres; the loader scales OBJ by 0.01, so meshes are
    written in centimetres while the .mass stays in metres.
  * OpenSim puts the pelvis at the origin, leaving the model below the floor;
    the whole skeleton is lifted so the lowest bone rests on y=0.
  * Joint types map onto what the engine supports (Free/Ball/Revolute/Weld).
    The number of *coordinates* decides: one is a hinge, three a ball. A Walker
    knee drives three rotations from a single coordinate and is still a hinge;
    a PinJoint names no axis because it is implicitly the joint frame's Z.

    python tools/build_skeleton.py <model.osim> <out.mass>
           [--bones DIR] [--profile data/profiles/x.json] [--check]
"""
import sys
import os
import json
import math
import xml.etree.ElementTree as ET

# ---------------------------------------------------------------- anatomy ----
# Keyed by the .osim body name with any side suffix removed. Latin follows
# Terminologia Anatomica; `pt` is the everyday Portuguese term.
NAMES = {
    "pelvis":       ("Os coxae", "Pelve"),
    "femur":        ("Os femoris", "Fêmur"),
    "tibia":        ("Tibia et fibula", "Tíbia e fíbula"),
    "patella":      ("Patella", "Patela"),
    "talus":        ("Talus", "Tálus"),
    "calcn":        ("Calcaneus", "Calcâneo"),
    "toes":         ("Ossa digitorum pedis", "Dedos do pé"),
    "torso":        ("Columna vertebralis et thorax", "Tronco"),
    "humerus":      ("Humerus", "Úmero"),
    "ulna":         ("Ulna", "Ulna"),
    "radius":       ("Radius", "Rádio"),
    "hand":         ("Ossa manus", "Mão"),
    "scapula":      ("Scapula", "Escápula"),
    "clavicle":     ("Clavicula", "Clavícula"),
    "skull":        ("Cranium", "Crânio"),
    "jaw":          ("Mandibula", "Mandíbula"),
    "scaphoid":     ("Os scaphoideum", "Escafoide"),
    "lunate":       ("Os lunatum", "Semilunar"),
    "triquetrum":   ("Os triquetrum", "Piramidal"),
    "pisiform":     ("Os pisiforme", "Pisiforme"),
    "metacarpal1":  ("Os metacarpi I", "1º metacarpo"),
    "index_proximal":  ("Phalanx proximalis II", "Falange proximal do indicador"),
    "index_medial":    ("Phalanx media II", "Falange média do indicador"),
    "index_distal":    ("Phalanx distalis II", "Falange distal do indicador"),
    "middle_proximal": ("Phalanx proximalis III", "Falange proximal do médio"),
    "middle_medial":   ("Phalanx media III", "Falange média do médio"),
    "middle_distal":   ("Phalanx distalis III", "Falange distal do médio"),
    "ring_proximal":   ("Phalanx proximalis IV", "Falange proximal do anelar"),
    "ring_medial":     ("Phalanx media IV", "Falange média do anelar"),
    "ring_distal":     ("Phalanx distalis IV", "Falange distal do anelar"),
    "little_proximal": ("Phalanx proximalis V", "Falange proximal do mínimo"),
    "little_medial":   ("Phalanx media V", "Falange média do mínimo"),
    "little_distal":   ("Phalanx distalis V", "Falange distal do mínimo"),
    "thumb_proximal":  ("Phalanx proximalis I", "Falange proximal do polegar"),
    "thumb_distal":    ("Phalanx distalis I", "Falange distal do polegar"),
    "trapezium":    ("Os trapezium", "Trapézio"),
    "trapezoid":    ("Os trapezoideum", "Trapezoide"),
    "capitate":     ("Os capitatum", "Capitato"),
    "hamate":       ("Os hamatum", "Hamato"),
    "metacarpal2":  ("Os metacarpi II", "2º metacarpo"),
    "metacarpal3":  ("Os metacarpi III", "3º metacarpo"),
    "metacarpal4":  ("Os metacarpi IV", "4º metacarpo"),
    "metacarpal5":  ("Os metacarpi V", "5º metacarpo"),
    "thumb":        ("Pollex", "Polegar"),
    "midfinger":    ("Digitus medius", "Dedo médio"),
    "ringfinger":   ("Digitus anularis", "Dedo anelar"),
    "littlefinger": ("Digitus minimus", "Dedo mínimo"),
}
SIDE_PT = {"r": " direito", "l": " esquerdo"}
M_TO_CM = 100.0


def anatomical(body):
    stem, side = body, ""
    for suffix in ("_r", "_l"):
        if body.endswith(suffix):
            stem, side = body[:-2], suffix[1]
            break
    latin, pt = NAMES.get(stem, ("", ""))
    if pt and side:
        pt += SIDE_PT[side]
    return latin, pt


# ------------------------------------------------------------------ maths ----
IDENT = [1, 0, 0, 0, 1, 0, 0, 0, 1]


def euler_xyz(a, b, c):
    """OpenSim body-fixed XYZ Euler angles -> row-major 3x3."""
    ca, sa = math.cos(a), math.sin(a)
    cb, sb = math.cos(b), math.sin(b)
    cc, sc = math.cos(c), math.sin(c)
    return [cb * cc,                -cb * sc,               sb,
            sa * sb * cc + ca * sc, -sa * sb * sc + ca * cc, -sa * cb,
            -ca * sb * cc + sa * sc, ca * sb * sc + sa * cc,  ca * cb]


def mat_mul(A, B):
    return [sum(A[r * 3 + k] * B[k * 3 + c] for k in range(3))
            for r in range(3) for c in range(3)]


def transpose(A):
    return [A[0], A[3], A[6], A[1], A[4], A[7], A[2], A[5], A[8]]


def mat_vec(A, v):
    return [sum(A[r * 3 + k] * v[k] for k in range(3)) for r in range(3)]


def floats(text, n=3):
    if not text:
        return [0.0] * n
    v = [float(x) for x in text.split()]
    return (v + [0.0] * n)[:n]


# ------------------------------------------------------------------ parse ----
JOINT_TAGS = ("CustomJoint", "PinJoint", "BallJoint", "FreeJoint",
              "WeldJoint", "UniversalJoint", "SliderJoint")


def parse_osim(path):
    root = ET.parse(path).getroot()

    bodies = {}
    for b in root.iter("Body"):
        bodies[b.get("name")] = {
            "mass": float(b.findtext("mass", "1") or 1),
            "meshes": [m.findtext("mesh_file", "").strip()
                       for m in b.iter("Mesh") if m.findtext("mesh_file")],
        }

    joints = []
    for tag in JOINT_TAGS:
        for j in root.iter(tag):
            frames = {f.get("name"): f for f in j.iter("PhysicalOffsetFrame")}
            pf = frames.get(j.findtext("socket_parent_frame", "").strip())
            cf = frames.get(j.findtext("socket_child_frame", "").strip())
            if pf is None or cf is None:
                continue
            owner = lambda f: f.findtext("socket_parent", "").strip().split("/")[-1]
            axes = [floats(ta.findtext("axis"))
                    for ta in j.iter("TransformAxis")
                    if (ta.get("name") or "").startswith("rotation")
                    and (ta.findtext("coordinates", "") or "").strip()]
            joints.append({
                "name": j.get("name"), "tag": tag,
                "parent": owner(pf), "child": owner(cf),
                "p_trans": floats(pf.findtext("translation")),
                "p_orient": floats(pf.findtext("orientation")),
                "c_trans": floats(cf.findtext("translation")),
                "c_orient": floats(cf.findtext("orientation")),
                "ranges": [floats(c.findtext("range"), 2) for c in j.iter("Coordinate")],
                "axes": axes,
            })
    return bodies, joints


# ------------------------------------------------------------------ place ----
def world_poses(bodies, joints):
    """(R, t) per body, and the world frame of each joint.

    At the default pose the parent's and child's joint frames coincide, so
    child_world = parent_world * parent_offset * inverse(child_offset).
    """
    by_child = {j["child"]: j for j in joints}
    roots = [b for b in bodies
             if b not in by_child or by_child[b]["parent"] in ("ground", "")]

    pose, order = {}, []

    def walk(name, R, t):
        pose[name] = (R, t)
        order.append(name)
        for j in joints:
            if j["parent"] != name or j["child"] in pose:
                continue
            Rp = mat_mul(R, euler_xyz(*j["p_orient"]))
            tp = [t[i] + mat_vec(R, j["p_trans"])[i] for i in range(3)]
            Rc = mat_mul(Rp, transpose(euler_xyz(*j["c_orient"])))
            tc = [tp[i] - mat_vec(Rc, j["c_trans"])[i] for i in range(3)]
            j["world"] = (Rp, tp)
            walk(j["child"], Rc, tc)

    for r in roots:
        walk(r, IDENT, [0.0, 0.0, 0.0])
    return pose, order, by_child


def joint_of(j):
    """(type, axis, lower, upper) for the engine."""
    if j is None or j["parent"] in ("ground", ""):
        return "Free", [0, 0, 1], None, None
    ndof = len(j["ranges"])
    if j["tag"] == "WeldJoint" or ndof == 0:
        return "Weld", [0, 0, 1], None, None
    if ndof == 1:
        axis = j["axes"][0] if j["axes"] else [0, 0, 1]
        return "Revolute", axis, j["ranges"][0][0], j["ranges"][0][1]
    lo = [r[0] for r in j["ranges"][:3]]
    hi = [r[1] for r in j["ranges"][:3]]
    while len(lo) < 3:
        lo.append(-1.5)
    while len(hi) < 3:
        hi.append(1.5)
    return "Ball", [0, 0, 1], lo, hi


# ------------------------------------------------------------------ meshes ---
def write_placed_mesh(sources, out, R, t, lift):
    """Merge a body's meshes into one OBJ, in world centimetres.

    Returns (min, max) per axis in metres, or None if nothing was written.
    """
    verts, norms, faces = [], [], []
    lo = [1e9] * 3
    hi = [-1e9] * 3
    for src in sources:
        vbase, nbase = len(verts), len(norms)
        with open(src) as f:
            for line in f:
                if line.startswith("v "):
                    v = [float(x) for x in line.split()[1:4]]
                    w = [mat_vec(R, v)[i] + t[i] * M_TO_CM for i in range(3)]
                    w[1] += lift * M_TO_CM
                    for i in range(3):
                        lo[i] = min(lo[i], w[i]); hi[i] = max(hi[i], w[i])
                    verts.append("v %.6f %.6f %.6f\n" % tuple(w))
                elif line.startswith("vn "):
                    n = mat_vec(R, [float(x) for x in line.split()[1:4]])
                    norms.append("vn %.6f %.6f %.6f\n" % tuple(n))
                elif line.startswith("f "):
                    out_tokens = []
                    for tok in line.split()[1:]:
                        vi, _, ni = tok.partition("//")
                        vi = int(vi) + vbase
                        out_tokens.append("%d//%d" % (vi, int(ni) + nbase) if ni else str(vi))
                    faces.append("f " + " ".join(out_tokens) + "\n")
    if not verts:
        return None
    with open(out, "w") as f:
        f.write("# %s\n" % ", ".join(os.path.basename(s) for s in sources))
        f.writelines(verts)
        f.writelines(norms)
        f.writelines(faces)
    return [lo[i] / M_TO_CM for i in range(3)], [hi[i] / M_TO_CM for i in range(3)]


def sources_for(body, bonedir):
    out = []
    for m in body["meshes"]:
        p = os.path.join(bonedir, os.path.splitext(m)[0] + ".obj")
        if os.path.exists(p):
            out.append(p)
    return out


# ------------------------------------------------------------------- build ---
def build(osim, bonedir, meshpref="../atlas/bones/"):
    bodies, joints = parse_osim(osim)
    pose, order, by_child = world_poses(bodies, joints)

    # How far to lift everything so the lowest bone rests on the floor. Measured
    # from the meshes: a body's translation is its origin, not the centre of its
    # shape, so translation minus half the size is not the floor.
    lowest = 1e9
    for name in order:
        R, t = pose[name]
        for src in sources_for(bodies[name], bonedir):
            with open(src) as f:
                for line in f:
                    if line.startswith("v "):
                        v = [float(x) for x in line.split()[1:4]]
                        y = (mat_vec(R, v)[1] + t[1] * M_TO_CM) / M_TO_CM
                        lowest = min(lowest, y)
    lift = -lowest if lowest < 1e8 else 0.0

    nodes = []
    for name in order:
        R, t = pose[name]
        j = by_child.get(name)
        jtype, axis, lo, hi = joint_of(j)
        latin, pt = anatomical(name)

        # A fused body anatomy does not fuse gets split into its own bones.
        if name in EXPLODE:
            nodes.extend(explode_body(name, bodies[name], R, t, lift,
                                      bonedir, meshpref, jtype, jw_of(j, R, t),
                                      "" if j is None else j["parent"]))
            continue

        obj, size = "", [0.05, 0.05, 0.05]
        srcs = sources_for(bodies[name], bonedir)
        if srcs:
            fname = "_%s.obj" % name
            box = write_placed_mesh(srcs, os.path.join(bonedir, fname), R, t, lift)
            if box:
                obj = meshpref + fname
                size = [max(1e-3, box[1][i] - box[0][i]) for i in range(3)]

        tb = [t[0], t[1] + lift, t[2]]
        jw = j.get("world", (R, t)) if j else (R, t)
        tj = [jw[1][0], jw[1][1] + lift, jw[1][2]]

        node = {
            "id": name,
            "parent": "" if j is None or j["parent"] in ("ground", "") else j["parent"],
            "anatomy": {"latin": latin, "pt": pt},
            "body": {
                "type": "Box", "mass": bodies[name]["mass"], "obj": obj, "size": size,
                "contact": name.startswith(("calcn", "toes")),
                "color": [0.92, 0.90, 0.85, 1.0],
                "transform": {"linear": R, "translation": tb},
            },
            "joint": {
                "type": jtype, "bvh": "",
                "transform": {"linear": jw[0], "translation": tj},
            },
        }
        if jtype == "Revolute":
            node["joint"].update({"axis": axis, "lower": lo, "upper": hi})
        elif jtype == "Ball":
            node["joint"].update({"lower": lo, "upper": hi})
        nodes.append(node)
    return nodes


# ---------------------------------------------------------------- explode ----
# Bodies the source model fuses that anatomy does not. Each entry lists the
# child bones as (mesh stem, parent, joint type, limit), where the parent is
# another entry or the fused body itself. Positions come free — the meshes are
# already placed in the fused body's frame — and each joint is put at the
# interface between the two bones, which is where the articulation is.
#
# The wrist and the intercarpal joints are synovial and do move, so by the
# project's rule they are joints, not welds; the ranges are small but real.
FINGERS = [("index", "metacarpal2"), ("middle", "metacarpal3"),
           ("ring", "metacarpal4"), ("little", "metacarpal5")]

def hand_tree():
    t = [
        # proximal row, off the forearm
        ("scaphoid",   None,        "Ball",     0.35),
        ("lunate",     None,        "Ball",     0.35),
        ("triquetrum", "lunate",    "Ball",     0.20),
        ("pisiform",   "triquetrum", "Ball",    0.15),
        # distal row
        ("trapezium",  "scaphoid",  "Ball",     0.20),
        ("trapezoid",  "scaphoid",  "Ball",     0.15),
        ("capitate",   "scaphoid",  "Ball",     0.20),
        ("hamate",     "triquetrum", "Ball",    0.20),
        # carpometacarpal: the thumb's is a saddle and mobile, 2-5 much less so
        ("metacarpal1", "trapezium", "Ball",    0.90),
        ("metacarpal2", "trapezoid", "Ball",    0.10),
        ("metacarpal3", "capitate",  "Ball",    0.10),
        ("metacarpal4", "hamate",    "Ball",    0.25),
        ("metacarpal5", "hamate",    "Ball",    0.35),
        # thumb: two phalanges
        ("thumb_proximal", "metacarpal1",    "Ball",     1.00),
        ("thumb_distal",   "thumb_proximal", "Revolute", 1.40),
    ]
    for finger, mc in FINGERS:
        t += [
            (finger + "_proximal", mc,                   "Ball",     1.60),
            (finger + "_medial",   finger + "_proximal", "Revolute", 1.90),
            (finger + "_distal",   finger + "_medial",   "Revolute", 1.50),
        ]
    return t


EXPLODE = {"hand_r": hand_tree(), "hand_l": hand_tree()}


def mesh_points(path):
    return [[float(x) for x in l.split()[1:4]]
            for l in open(path) if l.startswith("v ")]


def interface(a, b):
    """Midpoint of the closest pair between two meshes: where they articulate."""
    best, pa, pb = 1e18, a[0], b[0]
    step_a = max(1, len(a) // 400)
    step_b = max(1, len(b) // 400)
    for p in a[::step_a]:
        for q in b[::step_b]:
            d = (p[0]-q[0])**2 + (p[1]-q[1])**2 + (p[2]-q[2])**2
            if d < best:
                best, pa, pb = d, p, q
    return [(pa[i] + pb[i]) / 2.0 for i in range(3)]



def jw_of(j, R, t):
    return j.get("world", (R, t)) if j else (R, t)


def explode_body(name, body, R, t, lift, bonedir, meshpref, root_jtype, root_jw, root_parent):
    """Split a fused body into the bones anatomy actually articulates.

    The meshes are already positioned in the fused body's frame, so each bone's
    place comes for free; the joint between two bones is put at the interface
    between their meshes, which is where the articulation physically is.
    """
    side = name[-2:] if name[-2:] in ("_r", "_l") else ""
    stems = {os.path.splitext(m)[0]: m for m in body["meshes"]}

    def find(stem):
        """The mesh whose name starts with this bone's stem, whatever the suffix."""
        for k in stems:
            if k.startswith(stem + "_") or k == stem:
                return k
        return None

    tree = EXPLODE[name]
    pts, placed = {}, {}
    for stem, _parent, _jt, _lim in tree:
        f = find(stem)
        if not f:
            continue
        src = os.path.join(bonedir, f + ".obj")
        if os.path.exists(src):
            pts[stem] = mesh_points(src)
            placed[stem] = src

    total_pts = sum(len(v) for v in pts.values()) or 1
    out = []
    for stem, parent, jt, lim in tree:
        if stem not in placed:
            continue
        bone = stem + side
        fname = "_%s.obj" % bone
        box = write_placed_mesh([placed[stem]], os.path.join(bonedir, fname), R, t, lift)
        if not box:
            continue
        centre = [(box[0][i] + box[1][i]) / 2.0 for i in range(3)]

        # joint at the interface with the parent; the root bones of the tree
        # keep the fused body's own joint with the forearm
        if parent is None or parent + side not in [n["id"] for n in out]:
            jt_type, jw = (jt, (R, [c for c in centre])) if parent is None else (jt, (R, centre))
            if parent is None:
                jt_type, jw = root_jtype, root_jw
                jpos = [root_jw[1][0], root_jw[1][1] + lift, root_jw[1][2]]
                jparent = root_parent
            else:
                jpos, jparent = centre, parent + side
        else:
            mid = interface(pts[stem], pts[parent])
            w = [mat_vec(R, mid)[i] + t[i] for i in range(3)]
            jpos = [w[0], w[1] + lift, w[2]]
            jparent = parent + side
            jt_type = jt

        latin, pt = anatomical(bone)
        node = {
            "id": bone, "parent": jparent,
            "anatomy": {"latin": latin, "pt": pt},
            "body": {
                "type": "Box",
                "mass": body["mass"] * len(pts[stem]) / total_pts,
                "obj": meshpref + fname,
                "size": [max(1e-3, box[1][i] - box[0][i]) for i in range(3)],
                "contact": False, "color": [0.92, 0.90, 0.85, 1.0],
                "transform": {"linear": R, "translation": centre},
            },
            "joint": {
                "type": jt_type, "bvh": "",
                "transform": {"linear": R, "translation": jpos},
            },
        }
        if jt_type == "Revolute":
            node["joint"].update({"axis": [0, 0, 1], "lower": -lim, "upper": 0.0})
        elif jt_type == "Ball":
            node["joint"].update({"lower": [-lim, -lim, -lim], "upper": [lim, lim, lim]})
        out.append(node)
    return out



def body_extent(nodes, bonedir):
    """(floor, top) in metres, measured from the meshes themselves.

    The bone boxes are a loose bound — a body's translation is its origin, not
    the centre of its shape — so anything that depends on real height has to
    read the geometry.
    """
    lo, hi = 1e9, -1e9
    seen = set()
    for n in nodes:
        obj = n["body"]["obj"]
        if not obj or obj in seen:
            continue
        seen.add(obj)
        path = os.path.join(bonedir, os.path.basename(obj))
        if not os.path.exists(path):
            continue
        with open(path) as f:
            for line in f:
                if line.startswith("v "):
                    y = float(line.split()[2]) / M_TO_CM
                    lo = min(lo, y); hi = max(hi, y)
    return (lo, hi) if lo < hi else (0.0, 0.0)


# ---------------------------------------------------------------- profile ----
# The source models are one individual. A profile says which body we want, and
# every derived quantity follows from it: segment lengths from the height ratio,
# segment masses so they sum to the profile's mass. Muscle f0 will follow the
# same way, via the volume-scaling relation in Handsfield et al. 2014.
DEFAULT_PROFILE = {
    "name": "reference", "sex": "male", "height_m": 1.75, "mass_kg": 75.0,
    "specific_tension_N_cm2": 60.0,
}


def load_profile(path):
    if not path:
        return dict(DEFAULT_PROFILE)
    with open(path, encoding="utf-8") as f:
        p = json.load(f)
    out = dict(DEFAULT_PROFILE)
    out.update({k: v for k, v in p.items() if not k.startswith("_")})
    return out


def apply_profile(nodes, profile, bonedir):
    """Scale the built skeleton to the profile, meshes included.

    Geometry scales by the height ratio and mass is redistributed so the
    segments sum to the profile's mass, keeping the source model's proportions.
    Per-segment anthropometric ratios are the refinement; a uniform factor is
    honest about being one, and is exact when only the size changes.
    """
    if not nodes:
        return 1.0, 1.0
    floor, top = body_extent(nodes, bonedir)
    built_h = top - floor
    k = profile["height_m"] / built_h if built_h > 1e-6 else 1.0

    built_m = sum(n["body"]["mass"] for n in nodes) or 1.0
    km = profile["mass_kg"] / built_m

    seen = set()
    for n in nodes:
        for key in ("body", "joint"):
            t = n[key]["transform"]["translation"]
            n[key]["transform"]["translation"] = [c * k for c in t]
        n["body"]["size"] = [c * k for c in n["body"]["size"]]
        n["body"]["mass"] *= km
        obj = n["body"]["obj"]
        if not obj or obj in seen:
            continue
        seen.add(obj)
        path = os.path.join(bonedir, os.path.basename(obj))
        out = []
        with open(path) as f:
            for line in f:
                if line.startswith("v "):
                    v = [float(x) * k for x in line.split()[1:4]]
                    out.append("v %.6f %.6f %.6f\n" % tuple(v))
                else:
                    out.append(line)
        with open(path, "w") as f:
            f.writelines(out)
    return k, km


def check(nodes, bonedir):
    """Report what came out, so the tool can be corrected until it is right."""
    problems = []
    by_id = {n["id"]: n for n in nodes}

    floor, top = body_extent(nodes, bonedir)
    print("bones      : %d (%d with a mesh, %d named)"
          % (len(nodes),
             sum(1 for n in nodes if n["body"]["obj"]),
             sum(1 for n in nodes if n["anatomy"]["pt"])))
    print("mass       : %.1f kg" % sum(n["body"]["mass"] for n in nodes))
    print("height     : %.2f m" % (top - floor))

    lowest = floor
    if lowest < -0.05 or lowest > 0.35:
        problems.append("lowest bone at y=%.3f: model is not standing on the floor" % lowest)

    for n in nodes:
        if not n["anatomy"]["pt"]:
            problems.append("%s has no anatomical name" % n["id"])
        if not n["body"]["obj"]:
            problems.append("%s has no mesh" % n["id"])
        if n["body"]["mass"] <= 0:
            problems.append("%s has no mass" % n["id"])

    # left/right pairs should mirror across x=0... which is z here: the model's
    # sagittal plane is z=0, matching the muscle atlas.
    for n in nodes:
        if not n["id"].endswith("_r"):
            continue
        other = by_id.get(n["id"][:-2] + "_l")
        if other is None:
            problems.append("%s has no left counterpart" % n["id"])
            continue
        a = n["body"]["transform"]["translation"]
        b = other["body"]["transform"]["translation"]
        if abs(a[2] + b[2]) > 1e-6 or abs(a[1] - b[1]) > 1e-6 or abs(a[0] - b[0]) > 1e-6:
            problems.append("%s / %s are not mirrored" % (n["id"], other["id"]))

    joints = {}
    for n in nodes:
        joints[n["joint"]["type"]] = joints.get(n["joint"]["type"], 0) + 1
    print("joints     : %s" % ", ".join("%s %d" % kv for kv in sorted(joints.items())))

    if problems:
        print("\nPROBLEMS (%d) - fix the tool, not the .mass:" % len(problems))
        for p in problems:
            print("  - %s" % p)
    else:
        print("\nno problems detected")
    return problems


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) < 2:
        print(__doc__)
        return 1
    bonedir = "data/atlas/bones"
    for i, a in enumerate(sys.argv):
        if a == "--bones" and i + 1 < len(sys.argv):
            bonedir = sys.argv[i + 1]

    profile_path = ""
    for i, a in enumerate(sys.argv):
        if a == "--profile" and i + 1 < len(sys.argv):
            profile_path = sys.argv[i + 1]
    profile = load_profile(profile_path)

    nodes = build(args[0], bonedir)
    k, km = apply_profile(nodes, profile, bonedir)
    print("profile   : %s — %.2f m, %.1f kg (geometry x%.4f, mass x%.4f)"
          % (profile["name"], profile["height_m"], profile["mass_kg"], k, km))

    model = {
        "massVersion": 1,
        "meta": {"name": "HBS skeleton - " + profile["name"], "unit": "m",
                 "specificTension_N_cm2": profile.get("specific_tension_N_cm2", 60.0),
                 "profile": profile},
        "skeleton": nodes,
        "muscles": [], "motions": [], "fills": [],
        "params": {}, "scene": {}, "training": {}, "env": {},
    }
    with open(args[1], "w", encoding="utf-8") as f:
        json.dump(model, f, indent=2, ensure_ascii=False)
    print("-> %s" % args[1])

    if "--check" in sys.argv:
        return 2 if check(nodes, bonedir) else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
