#!/usr/bin/env python3
"""Name BodyParts3D meshes by anatomy and put them in this project's frame.

BodyParts3D ships one mesh per anatomical structure, keyed by an opaque element
id (FJ3158), all of them in a single coordinate frame — which is what makes a
whole skeleton possible without registering sources against each other. This
turns that into files a human can read (`first_thoracic_vertebra.obj`) and into
the convention the rest of HBS uses.

Two frames are reconciled:
  * BodyParts3D is millimetres with Z up; this project is metres with Y up, and
    its OBJ loader scales by 0.01, so meshes are written in centimetres.
  * The axis swap (x, y, z) -> (x, z, -y) keeps the body facing the same way as
    the OpenSim-derived models, so both can be compared side by side.

Only bones are kept: the source also carries organs, vessels and brain regions.

    python tools/bp3d_prepare.py <extracted-dir> <element_parts.txt> <out-dir>
"""
import sys
import os
import re
import glob

# What counts as a bone. The source mixes in soft tissue whose names share
# words with bones ("intervertebral disk", "costal cartilage"), hence the veto.
BONE = re.compile(
    r"\b(atlas|axis|vertebra|rib|sternum|manubrium|xiphoid|sacrum|coccyx|hip bone"
    r"|femur|patella|tibia|fibula|talus|calcaneus|navicular bone|cuboid bone"
    r"|cuneiform bone|metatarsal bone|humerus|radius|ulna|scapula|clavicle"
    r"|scaphoid|lunate|triquetral|pisiform|trapezium|trapezoid|capitate|hamate"
    r"|metacarpal bone|phalanx|mandible|maxilla|hyoid bone|frontal bone"
    r"|parietal bone|occipital bone|temporal bone|sphenoid bone|ethmoid"
    r"|zygomatic bone|nasal bone|lacrimal bone|palatine bone|vomer"
    r"|malleus|incus|stapes)\b", re.I)
# Word boundaries matter on the short ones: an unanchored "sac" vetoed the
# sacrum, and "vein"/"duct" would reach into other names the same way.
NOT_BONE = re.compile(
    r"(disk|cartilage|joint|ligament|muscle|membrane|cavity|marrow|artery"
    r"|vein|nerve|lobe|gyrus|cortex|apparatus|gland|duct"
    r"|sac|canaliculus|lake|symphysis|part of|side of|set of"
    r"|skeleton|girdle|limb|cage$|column)", re.I)

MM_TO_CM = 0.1


def slug(name):
    return re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 1
    src, mapping, dst = sys.argv[1], sys.argv[2], sys.argv[3]
    os.makedirs(dst, exist_ok=True)

    files = {os.path.basename(p)[:-4]: p for p in glob.glob(os.path.join(src, "*", "*.obj"))}
    if not files:
        files = {os.path.basename(p)[:-4]: p for p in glob.glob(os.path.join(src, "*.obj"))}

    # element id -> anatomical names it belongs to; keep the most specific one
    names = {}
    with open(mapping, encoding="utf-8") as f:
        next(f, None)
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if len(parts) < 3:
                continue
            _fma, name, fj = parts[0], parts[1], parts[2]
            if fj not in files:
                continue
            if not BONE.search(name) or NOT_BONE.search(name):
                continue
            prev = names.get(fj)
            # a mesh can belong to "rib cage" and to "right third rib"; the
            # longer, more specific name is the bone itself
            if prev is None or len(name) > len(prev):
                names[fj] = name

    written = skipped = 0
    for fj, name in sorted(names.items(), key=lambda kv: kv[1]):
        out = os.path.join(dst, slug(name) + ".obj")
        if os.path.exists(out):
            skipped += 1
            continue
        n = 0
        with open(files[fj]) as fin, open(out, "w") as fo:
            fo.write("# %s (BodyParts3D %s) - CC BY-SA 2.1 JP, DBCLS\n" % (name, fj))
            for line in fin:
                if line.startswith("v "):
                    x, y, z = (float(v) for v in line.split()[1:4])
                    # mm -> cm, and Z-up -> Y-up
                    fo.write("v %.4f %.4f %.4f\n"
                             % (x * MM_TO_CM, z * MM_TO_CM, -y * MM_TO_CM))
                    n += 1
                elif line.startswith("vn "):
                    x, y, z = (float(v) for v in line.split()[1:4])
                    fo.write("vn %.6f %.6f %.6f\n" % (x, z, -y))
                elif line.startswith("f "):
                    fo.write(line)
        if n:
            written += 1
        else:
            os.remove(out)
    print("bones written: %d (%d duplicate names skipped) -> %s" % (written, skipped, dst))
    return 0


if __name__ == "__main__":
    sys.exit(main())
