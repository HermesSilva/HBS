#pragma once
// libmassedit — build the complete human skeleton.
//
// A skeleton with a fused trunk is not a human skeleton: it cannot flex, twist
// or carry a shoulder. This generates every bone that articulates as its own
// body — 24 vertebrae, 24 ribs, the sternum, both shoulder girdles, both hands
// and both feet — from BodyParts3D, which is one segmented body: every bone a
// separate mesh, all in a single frame, so each position comes from the data
// and nothing is registered or estimated.
//
// What that source has no notion of is joints, and those live in the anatomy
// table below: who hangs off whom, how the joint moves and how far.
//
// The skeleton is a generated artefact. Fix this code, never the .mass.
#include "MassModel.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace mass {

// The body a skeleton is generated for. Every derived quantity follows from it:
// segment lengths from the height, bone mass as a fraction of body mass, and —
// once muscles arrive — f0 through the volume relation in Handsfield 2014.
struct BodyProfile {
    std::string name = "reference";
    std::string sex = "male";
    double height_m = 1.75;
    double mass_kg = 75.0;
    double specific_tension = 60.0;

    static BodyProfile fromJson(const nlohmann::json& j);
    static BodyProfile fromFile(const std::string& path, std::string* err = nullptr);
};

// One bone in the anatomy table.
struct BoneSpec {
    std::string bone;      // mesh name, or a fused body defined in fusedMeshes()
    std::string parent;    // empty for the root
    std::string joint;     // Free | Ball | Revolute | Weld
    double limit = 0.0;    // range in radians; a hinge uses it as its span
};

struct Skeleton {
    // The human skeleton as a tree. Anatomy written down, kept apart from the
    // generator so it can be read and corrected on its own.
    static std::vector<BoneSpec> anatomy();

    // Bones fused in the adult: one rigid body carrying every mesh. The skull's
    // 19 bones are joined by sutures that do not move, so by this project's
    // rule they are not joints — but all their geometry is there.
    static std::vector<std::pair<std::string, std::vector<std::string>>> fusedMeshes();

    // (latin, portuguese) for a bone id. Portuguese agrees in gender, and the
    // side agrees with the head of the phrase: "escápula esquerda", and a
    // phalanx is "do indicador esquerdo", not "esquerda".
    static void names(const std::string& bone, std::string* latin, std::string* pt);

    // Generate the skeleton into `m`, replacing whatever it held. `bonedir`
    // holds the prepared per-bone meshes; the placed ones are written back
    // there and referenced from the model. Returns a report:
    // {bones, mass_kg, height_m, joints:{...}, problems:[...]}.
    static nlohmann::json build(Model& m, const std::string& bonedir,
                                const BodyProfile& profile, std::string* err = nullptr);

    // What came out, so the generator can be corrected until it is right:
    // counts, mass, height, joint census, missing names or meshes, unmirrored
    // pairs, and whether the model stands on the floor.
    static nlohmann::json check(const Model& m, const std::string& bonedir);
};

} // namespace mass
