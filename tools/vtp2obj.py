#!/usr/bin/env python3
"""Convert OpenSim bone meshes (VTK PolyData, .vtp) to the .obj this project loads.

The OpenSim model repository ships one mesh per bone — every carpal, every
vertebra, the jaw — in the same coordinate frame as the .osim models we already
use as the muscle atlas. Converting them gives HBS an anatomically real skeleton
that is consistent with its muscle paths, instead of the simplified boxes and
fused meshes it started from.

Two conventions matter and are handled here:
  * .vtp stores metres; the editor's OBJ loader scales by 0.01, so the .obj is
    written in centimetres.
  * .vtp faces are arbitrary polygons; OBJ consumers here expect triangles, so
    polygons are fan-triangulated. Per-vertex normals are averaged from the
    face normals, since the loader reads `vn` and the .vtp carries none.

    python tools/vtp2obj.py <in-dir-or-file> <out-dir> [--scale 100]
"""
import sys
import os
import re
import xml.etree.ElementTree as ET


def read_vtp(path):
    """Return (points, polygons) from an ASCII VTK PolyData file."""
    root = ET.parse(path).getroot()
    piece = root.find(".//Piece")
    if piece is None:
        raise ValueError("no <Piece>")

    def array(parent, name=None):
        if parent is None:
            return []
        for da in parent.findall("DataArray"):
            if name is None or da.get("Name") == name:
                if da.get("format", "ascii") != "ascii":
                    raise ValueError("only ascii DataArray is supported")
                return [float(x) for x in da.text.split()]
        return []

    flat = array(piece.find("Points"))
    pts = [tuple(flat[i:i + 3]) for i in range(0, len(flat), 3)]

    polys = piece.find("Polys")
    conn = [int(v) for v in array(polys, "connectivity")]
    offs = [int(v) for v in array(polys, "offsets")]

    faces, start = [], 0
    for end in offs:
        faces.append(conn[start:int(end)])
        start = int(end)
    return pts, faces


def write_obj(path, pts, faces, scale, name):
    """Write triangles with averaged per-vertex normals."""
    def sub(a, b):
        return (a[0] - b[0], a[1] - b[1], a[2] - b[2])

    def cross(a, b):
        return (a[1] * b[2] - a[2] * b[1],
                a[2] * b[0] - a[0] * b[2],
                a[0] * b[1] - a[1] * b[0])

    normals = [[0.0, 0.0, 0.0] for _ in pts]
    tris = []
    for f in faces:
        for k in range(2, len(f)):        # fan-triangulate
            tri = (f[0], f[k - 1], f[k])
            tris.append(tri)
            n = cross(sub(pts[tri[1]], pts[tri[0]]), sub(pts[tri[2]], pts[tri[0]]))
            for i in tri:
                normals[i][0] += n[0]; normals[i][1] += n[1]; normals[i][2] += n[2]

    with open(path, "w", encoding="ascii") as f:
        f.write("# %s - converted from OpenSim %s by tools/vtp2obj.py\n" % (name, name))
        for p in pts:
            f.write("v %.6f %.6f %.6f\n" % (p[0] * scale, p[1] * scale, p[2] * scale))
        for n in normals:
            L = (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) ** 0.5 or 1.0
            f.write("vn %.6f %.6f %.6f\n" % (n[0] / L, n[1] / L, n[2] / L))
        for t in tris:
            f.write("f %d//%d %d//%d %d//%d\n"
                    % (t[0] + 1, t[0] + 1, t[1] + 1, t[1] + 1, t[2] + 1, t[2] + 1))
    return len(pts), len(tris)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) < 2:
        print(__doc__)
        return 1
    scale = 100.0
    for a in sys.argv[1:]:
        m = re.match(r"--scale[= ]?([0-9.]+)", a)
        if m:
            scale = float(m.group(1))

    src, dst = args[0], args[1]
    os.makedirs(dst, exist_ok=True)
    files = ([src] if os.path.isfile(src)
             else [os.path.join(src, f) for f in sorted(os.listdir(src)) if f.endswith(".vtp")])

    done = failed = skipped = 0
    for path in files:
        name = os.path.splitext(os.path.basename(path))[0]
        try:
            pts, faces = read_vtp(path)
            # Some files in Geometry/ are line art (axes, outlines) rather than
            # surfaces. Nothing to convert, and not a failure.
            if not pts or not faces:
                skipped += 1
                continue
            nv, nt = write_obj(os.path.join(dst, name + ".obj"), pts, faces, scale, name)
            done += 1
            if nv > 20000:
                print("  %-28s %6d verts %7d tris" % (name, nv, nt))
        except Exception as e:                       # noqa: BLE001 - report and continue
            failed += 1
            print("  FAILED %-24s %s" % (name, e))
    print("converted %d, skipped %d (not surfaces), failed %d -> %s"
          % (done, skipped, failed, dst))
    return 0 if failed == 0 else 2


if __name__ == "__main__":
    sys.exit(main())
