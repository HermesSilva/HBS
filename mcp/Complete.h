#pragma once
// libmassedit — structural completion: generate missing articulations (finger
// phalanges, toes, ...) so parts like the hand can actually articulate, and
// report gaps against a small expectation table. Generated bones get Revolute
// flex joints and mass from volume; the caller rebuilds the Index afterwards.
#include "MassModel.h"
#include "Index.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace mass {

// One digit's proportions, as fractions of the hand bone's own dimensions, so a
// hand of any size grows a correctly scaled set.
struct FingerSpec {
    const char* name;        // "Thumb", "Index", "Middle", "Ring", "Little"
    int    phalanges;        // 2 for the thumb, 3 for the rest
    double lateral;          // position across the hand width: + radial, - ulnar
    double base;             // where along the hand it starts (1 = distal edge)
    double length;           // proximal phalanx, as a fraction of hand length
};

struct FingerConfig {
    // Anatomical digit table. Successive phalanges shorten by `taper`; the thumb
    // sits proximally on the radial side and carries one segment less.
    std::vector<FingerSpec> digits {
        { "Thumb",  2,  0.42, 0.25, 0.24 },
        { "Index",  3,  0.33, 1.00, 0.30 },
        { "Middle", 3,  0.11, 1.00, 0.34 },
        { "Ring",   3, -0.11, 1.00, 0.31 },
        { "Little", 3, -0.32, 0.95, 0.24 },
    };
    double taper      = 0.62;         // each phalanx relative to the previous one
    double thickness  = 0.16;         // phalanx cross-section, fraction of hand width
    double density    = 1000.0;       // kg/m^3 for mass = density*volume
    double flexLimit  = 1.5;          // flexion range (rad)
};

struct Complete {
    // Grow the digits of `hand` as Revolute-flex chains, sized from the hand
    // bone and started at its distal edge. Names: "<hand>_<Digit><n>", e.g.
    // "HandL_Index2". The extension direction is taken from the hand's offset
    // from its parent, so left and right hands both point away from the body.
    // Appends bones and returns the created names; the caller must rebuild the
    // Index before querying/animating them.
    static std::vector<std::string> generateFingers(Model& m, const std::string& hand,
                                                     const FingerConfig& cfg = {});

    // Same, applied to `hand` and its L/R counterpart.
    static std::vector<std::string> generateFingersSymmetric(Model& m, const std::string& hand,
                                                             const FingerConfig& cfg = {});

    // Report bones from a small expectation table (hands/feet) that currently
    // have no children -> [{bone, expects}]. Uses the Index for child lookup.
    static nlohmann::json listGaps(const Model& m, const Index& ix);

    // Add muscles from a definition array (see data/atlas/missing_muscles.json):
    // [{name, hill:{f0,lm,lt,pen_angle}, anatomy:{...}, waypoints:[{body,p}]}].
    // Definitions are authored on the left side; with `mirror` each one also
    // yields its right-side twin, X-negated and with the bodies' L/R suffix
    // swapped. A name already in the model is skipped rather than duplicated.
    // Returns {"created":[names], "skipped":[names], "errors":[...]}.
    static nlohmann::json addMuscles(Model& m, const nlohmann::json& defs,
                                     bool mirror, bool dryRun);

    // Muscles anchored to a single bone: they cross no joint, so they can never
    // produce torque. Returns [{muscle, body}].
    static nlohmann::json listInertMuscles(const Model& m);

    // Re-anchor a muscle's waypoints from bone `from` to bone `to`, so an
    // insertion sitting on the wrong side of a joint starts driving it.
    // `firstIndex` re-anchors only from that waypoint on (-1 = all), and
    // `lastOnly` just the final one — the usual case, an insertion that has to
    // follow a newly created distal bone. With `snap` the moved waypoints are
    // placed at the target bone's centre; without it they keep their position
    // and only the anchoring changes. With `mirror` the L/R twin gets the same
    // treatment on the mirrored bones. Returns {"changed":[{muscle, waypoints}]}.
    static nlohmann::json reanchor(Model& m, const std::string& muscle,
                                   const std::string& from, const std::string& to,
                                   int firstIndex, bool lastOnly, bool snap,
                                   bool mirror, bool dryRun);
};

} // namespace mass
