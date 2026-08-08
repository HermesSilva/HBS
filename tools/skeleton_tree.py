#!/usr/bin/env python3
"""The human skeleton as a tree: which bone hangs off which, and how it moves.

This is anatomy written down, not derived from any file. It is separate from the
generator so the anatomy can be read and corrected on its own.

Each entry is (bone, parent, joint, limit):
  * `bone` and `parent` are BodyParts3D mesh names, or a fused-body name defined
    in FUSED below.
  * `joint` is what the engine supports: Ball, Revolute or Weld.
  * `limit` is the range in radians — a scalar for a hinge, applied to all three
    axes for a ball.

Ranges follow the joint's real mobility. Intervertebral joints move a few
degrees each and add up over the column; costovertebral joints move enough to
breathe; the shoulder is the freest joint in the body.

Bones fused in the adult are one rigid body carrying every mesh — the skull's 19
bones are joined by sutures that do not move, so by this project's rule they are
not joints. The geometry of all of them is still there.
"""

# Bones fused in the adult: one body, many meshes.
FUSED = {
    "skull": [
        "frontal_bone", "occipital_bone", "sphenoid_bone", "ethmoid", "vomer",
        "left_parietal_bone", "right_parietal_bone",
        "left_temporal_bone", "right_temporal_bone",
        "left_zygomatic_bone", "right_zygomatic_bone",
        "left_maxilla", "right_maxilla",
        "left_nasal_bone", "right_nasal_bone",
        "left_lacrimal_bone", "right_lacrimal_bone",
        "left_palatine_bone", "right_palatine_bone",
    ],
}

CERVICAL = ["third_cervical_vertebra", "fourth_cervical_vertebra",
            "fifth_cervical_vertebra", "sixth_cervical_vertebra",
            "seventh_cervical_vertebra"]
THORACIC = ["%s_thoracic_vertebra" % n for n in
            ("first", "second", "third", "fourth", "fifth", "sixth", "seventh",
             "eighth", "ninth", "tenth", "eleventh", "twelfth")]
LUMBAR = ["%s_lumbar_vertebra" % n for n in
          ("first", "second", "third", "fourth", "fifth")]
ORDINALS = ("first", "second", "third", "fourth", "fifth", "sixth", "seventh",
            "eighth", "ninth", "tenth", "eleventh", "twelfth")
FINGERS = ("index", "middle", "ring", "little")
TOES = ("second", "third", "fourth", "little")


def tree():
    """[(bone, parent, joint, limit)], root first."""
    t = [("sacrum", None, "Free", 0.0)]

    # --- pelvis: the hip bones meet the sacrum at the sacroiliac joints, which
    # --- are synovial but barely move
    for side in ("right", "left"):
        t.append(("%s_hip_bone" % side, "sacrum", "Ball", 0.05))

    # --- vertebral column, sacrum upwards. Each level moves a little; the
    # --- column moves a lot.
    prev = "sacrum"
    for v in reversed(LUMBAR):                     # L5 sits on the sacrum
        t.append((v, prev, "Ball", 0.12))
        prev = v
    for v in reversed(THORACIC):                   # T12 on L1
        t.append((v, prev, "Ball", 0.06))
        prev = v
    for v in reversed(CERVICAL):                   # C7 on T1
        t.append((v, prev, "Ball", 0.15))
        prev = v
    t.append(("axis", prev, "Ball", 0.15))
    t.append(("atlas", "axis", "Ball", 0.30))      # atlantoaxial: rotation
    t.append(("skull", "atlas", "Ball", 0.40))     # atlanto-occipital: nodding
    t.append(("mandible", "skull", "Ball", 0.50))  # temporomandibular
    t.append(("hyoid_bone", "skull", "Ball", 0.20))

    # --- ribs, each on its own thoracic vertebra; enough travel to breathe
    for i, ordinal in enumerate(ORDINALS):
        for side in ("right", "left"):
            t.append(("%s_%s_rib" % (side, ordinal), THORACIC[i], "Ball", 0.10))
    # sternum, closing the cage at the front
    t.append(("manubrium", "right_first_rib", "Ball", 0.05))
    t.append(("body_of_sternum", "manubrium", "Ball", 0.03))
    t.append(("xiphoid_process", "body_of_sternum", "Ball", 0.03))

    for side in ("right", "left"):
        S = lambda n: "%s_%s" % (side, n)

        # --- shoulder girdle: this is what lets an arm work overhead
        t.append((S("clavicle"), "manubrium", "Ball", 0.50))
        t.append((S("scapula"), S("clavicle"), "Ball", 0.40))
        t.append((S("humerus"), S("scapula"), "Ball", 1.60))
        t.append((S("ulna"), S("humerus"), "Revolute", 2.40))
        t.append((S("radius"), S("ulna"), "Revolute", 2.60))   # pronation

        # --- carpus: proximal row off the forearm, then the distal row
        t.append((S("scaphoid"), S("radius"), "Ball", 0.35))
        t.append((S("lunate"), S("radius"), "Ball", 0.35))
        t.append((S("triquetral"), S("lunate"), "Ball", 0.20))
        t.append((S("pisiform"), S("triquetral"), "Ball", 0.15))
        t.append((S("trapezium"), S("scaphoid"), "Ball", 0.20))
        t.append((S("trapezoid"), S("scaphoid"), "Ball", 0.15))
        t.append((S("capitate"), S("scaphoid"), "Ball", 0.20))
        t.append((S("hamate"), S("triquetral"), "Ball", 0.20))

        # --- metacarpals: the thumb's saddle joint is mobile, 2 and 3 are not
        mc = {"first": (S("trapezium"), 0.90), "second": (S("trapezoid"), 0.10),
              "third": (S("capitate"), 0.10), "fourth": (S("hamate"), 0.25),
              "fifth": (S("hamate"), 0.35)}
        for ordinal, (parent, lim) in mc.items():
            t.append(("%s_%s_metacarpal_bone" % (side, ordinal), parent, "Ball", lim))

        # --- fingers
        t.append(("proximal_phalanx_of_%s_thumb" % side,
                  "%s_first_metacarpal_bone" % side, "Ball", 1.00))
        t.append(("distal_phalanx_of_%s_thumb" % side,
                  "proximal_phalanx_of_%s_thumb" % side, "Revolute", 1.40))
        for i, finger in enumerate(FINGERS):
            mcn = ORDINALS[i + 1]                  # index -> 2nd metacarpal
            t.append(("proximal_phalanx_of_%s_%s_finger" % (side, finger),
                      "%s_%s_metacarpal_bone" % (side, mcn), "Ball", 1.60))
            t.append(("middle_phalanx_of_%s_%s_finger" % (side, finger),
                      "proximal_phalanx_of_%s_%s_finger" % (side, finger), "Revolute", 1.90))
            t.append(("distal_phalanx_of_%s_%s_finger" % (side, finger),
                      "middle_phalanx_of_%s_%s_finger" % (side, finger), "Revolute", 1.50))

        # --- lower limb
        t.append((S("femur"), S("hip_bone"), "Ball", 1.60))
        t.append((S("patella"), S("femur"), "Revolute", 1.20))
        t.append((S("tibia"), S("femur"), "Revolute", 2.40))
        t.append((S("fibula"), S("tibia"), "Ball", 0.05))
        t.append((S("talus"), S("tibia"), "Revolute", 0.90))       # ankle
        t.append((S("calcaneus"), S("talus"), "Revolute", 0.50))   # subtalar
        t.append(("navicular_bone_of_%s_foot" % side, S("talus"), "Ball", 0.15))
        t.append((S("cuboid_bone"), S("calcaneus"), "Ball", 0.15))
        for c in ("medial", "intermediate", "lateral"):
            t.append(("%s_%s_cuneiform_bone" % (side, c),
                      "navicular_bone_of_%s_foot" % side, "Ball", 0.10))
        mt = {"first": S("medial_cuneiform_bone"), "second": S("intermediate_cuneiform_bone"),
              "third": S("lateral_cuneiform_bone"), "fourth": S("cuboid_bone"),
              "fifth": S("cuboid_bone")}
        for ordinal, parent in mt.items():
            t.append(("%s_%s_metatarsal_bone" % (side, ordinal), parent, "Ball", 0.10))

        # --- toes
        t.append(("proximal_phalanx_of_%s_big_toe" % side,
                  "%s_first_metatarsal_bone" % side, "Ball", 0.80))
        t.append(("distal_phalanx_of_%s_big_toe" % side,
                  "proximal_phalanx_of_%s_big_toe" % side, "Revolute", 0.70))
        for i, toe in enumerate(TOES):
            mtn = ORDINALS[i + 1]
            t.append(("proximal_phalanx_of_%s_%s_toe" % (side, toe),
                      "%s_%s_metatarsal_bone" % (side, mtn), "Ball", 0.80))
            t.append(("middle_phalanx_of_%s_%s_toe" % (side, toe),
                      "proximal_phalanx_of_%s_%s_toe" % (side, toe), "Revolute", 0.70))
            t.append(("distal_phalanx_of_%s_%s_toe" % (side, toe),
                      "middle_phalanx_of_%s_%s_toe" % (side, toe), "Revolute", 0.60))
    return t


# Anatomical names. The mesh names are already Terminologia-derived English, so
# only the Portuguese and the Latin need spelling out; anything absent falls
# back to the mesh name with underscores removed.
PT = {
    "sacrum": ("Os sacrum", "Sacro"),
    "hip_bone": ("Os coxae", "Osso do quadril"),
    "skull": ("Cranium", "Crânio"),
    "mandible": ("Mandibula", "Mandíbula"),
    "hyoid_bone": ("Os hyoideum", "Hioide"),
    "atlas": ("Atlas", "Atlas"),
    "axis": ("Axis", "Áxis"),
    "manubrium": ("Manubrium sterni", "Manúbrio do esterno"),
    "body_of_sternum": ("Corpus sterni", "Corpo do esterno"),
    "xiphoid_process": ("Processus xiphoideus", "Processo xifoide"),
    "clavicle": ("Clavicula", "Clavícula"),
    "scapula": ("Scapula", "Escápula"),
    "humerus": ("Humerus", "Úmero"),
    "ulna": ("Ulna", "Ulna"),
    "radius": ("Radius", "Rádio"),
    "femur": ("Os femoris", "Fêmur"),
    "patella": ("Patella", "Patela"),
    "tibia": ("Tibia", "Tíbia"),
    "fibula": ("Fibula", "Fíbula"),
    "talus": ("Talus", "Tálus"),
    "calcaneus": ("Calcaneus", "Calcâneo"),
    "cuboid_bone": ("Os cuboideum", "Cuboide"),
    "scaphoid": ("Os scaphoideum", "Escafoide"),
    "lunate": ("Os lunatum", "Semilunar"),
    "triquetral": ("Os triquetrum", "Piramidal"),
    "pisiform": ("Os pisiforme", "Pisiforme"),
    "trapezium": ("Os trapezium", "Trapézio"),
    "trapezoid": ("Os trapezoideum", "Trapezoide"),
    "capitate": ("Os capitatum", "Capitato"),
    "hamate": ("Os hamatum", "Hamato"),
}
# Portuguese agrees in gender, so the side suffix depends on the bone's gender:
# "escápula esquerda", not "escápula esquerdo".
SIDE_M = {"right": " direito", "left": " esquerdo"}
SIDE_F = {"right": " direita", "left": " esquerda"}
FEMININE = ("costela", "escápula", "clavícula", "vértebra", "falange", "patela",
            "tíbia", "fíbula", "ulna", "mandíbula", "1ª", "2ª", "3ª", "4ª", "5ª",
            "6ª", "7ª", "8ª", "9ª", "10ª", "11ª", "12ª")


def _side(word, side):
    """Side suffix agreeing with the word's gender."""
    if not side:
        return ""
    low = word.lower()
    table = SIDE_F if any(low.startswith(f) or f in low for f in FEMININE) else SIDE_M
    return table[side]
ORD_PT = {"first": "1º", "second": "2º", "third": "3º", "fourth": "4º",
          "fifth": "5º", "sixth": "6ª", "seventh": "7ª", "eighth": "8ª",
          "ninth": "9ª", "tenth": "10ª", "eleventh": "11ª", "twelfth": "12ª"}
FINGER_PT = {"index": "indicador", "middle": "médio", "ring": "anelar",
             "little": "mínimo", "thumb": "polegar", "big": "hálux"}
TOE_ORD_PT = {"second": "2º", "third": "3º", "fourth": "4º", "little": "5º"}
LEVEL_PT = {"cervical": "cervical", "thoracic": "torácica", "lumbar": "lombar"}


def names(bone):
    """(latin, portuguese) for a bone id."""
    side = ""
    stem = bone
    for s in ("right", "left"):
        if bone.startswith(s + "_"):
            side, stem = s, bone[len(s) + 1:]
            break
        if bone.endswith("_" + s + "_foot"):
            side, stem = s, "navicular_bone"
            break

    if stem in PT:
        latin, pt = PT[stem]
        return latin, pt + _side(pt, side)

    for ordinal in ORDINALS:
        if bone.startswith(ordinal + "_"):
            for level, pt_level in LEVEL_PT.items():
                if level in bone:
                    return ("Vertebra %s" % level, "%s vértebra %s"
                            % (ORD_PT[ordinal].replace("º", "ª"), pt_level))
        if stem.startswith(ordinal + "_"):
            rest = stem[len(ordinal) + 1:]
            if rest == "rib":
                return "Costa", "%s costela%s" % (ORD_PT[ordinal], _side("costela", side))
            if rest == "metacarpal_bone":
                return "Os metacarpi", "%s metacarpo%s" % (ORD_PT[ordinal], _side("metacarpo", side))
            if rest == "metatarsal_bone":
                return "Os metatarsi", "%s metatarso%s" % (ORD_PT[ordinal], _side("metatarso", side))
            if rest.endswith("cuneiform_bone"):
                return "Os cuneiforme", "Cuneiforme%s" % _side("cuneiforme", side)

    for kind, kind_pt in (("proximal", "proximal"), ("middle", "média"),
                          ("distal", "distal")):
        prefix = "%s_phalanx_of_" % kind
        if bone.startswith(prefix):
            rest = bone[len(prefix):]
            for s in ("right", "left"):
                if rest.startswith(s + "_"):
                    side, rest = s, rest[len(s) + 1:]
                    break
            digit = rest.replace("_finger", "").replace("_toe", "")
            is_toe = rest.endswith("_toe")
            # The side agrees with the digit, not with "falange": it is the head
            # of the phrase. Hallux and thumb are named, the rest numbered, and
            # toes say so — "do 2º dedo do pé" against "do indicador".
            if is_toe:
                noun = ("hálux" if digit == "big"
                        else "%s dedo do pé" % TOE_ORD_PT.get(digit, digit))
            else:
                noun = FINGER_PT.get(digit, digit)
            return ("Phalanx %s" % kind,
                    "Falange %s do %s%s" % (kind_pt, noun, _side(noun, side)))

    for c in ("medial", "intermediate", "lateral"):
        if stem == "%s_cuneiform_bone" % c:
            pt = {"medial": "medial", "intermediate": "intermédio",
                  "lateral": "lateral"}[c]
            return "Os cuneiforme %s" % c, "Cuneiforme %s%s" % (pt, _side("cuneiforme", side))

    return "", bone.replace("_", " ")
