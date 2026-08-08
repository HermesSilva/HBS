#pragma once
// libmassedit — generate the musculature onto a generated skeleton.
//
// Muscle paths come from the OpenSim reference models: they are measured lines
// of action with Hill parameters behind them, which is not something to invent.
// What has to be solved is that they are expressed in *their* skeleton's local
// frames, and ours is a different body built from BodyParts3D.
//
// The bridge is proportion. A waypoint is turned into a fraction of the atlas
// bone's bounding box, and that fraction is applied to the same bone here. An
// attachment a third of the way down the femur stays a third of the way down —
// which is what an anatomical description actually says, and what makes the
// result follow the body profile for free.
//
// Only muscles that span two bones are generated. One anchored to a single bone
// crosses no joint and can produce no torque; whatever it models, it is not a
// skeletal muscle acting on the skeleton.
#include "MassModel.h"
#include "Skeleton.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace mass {

struct MuscleSource {
    std::string osim;                // reference model to read paths from
    std::string note;                // what it covers, for the report
};

struct Muscles {
    // The reference models used, in the order they are read. Later sources do
    // not overwrite a muscle an earlier one already provided.
    static std::vector<MuscleSource> sources(const std::string& atlasdir);

    // Generate muscles onto `m`, which must already hold a skeleton. Existing
    // muscles are replaced. Hill parameters come from the atlas; f0 is scaled
    // to the profile, since force follows physiological cross-section and that
    // follows body size.
    //
    // Returns {muscles, skipped:{...}, unmapped:[bodies], problems:[...]}.
    static nlohmann::json build(Model& m, const std::string& atlasdir,
                                const BodyProfile& profile, std::string* err = nullptr);
};

} // namespace mass
