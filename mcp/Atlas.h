#pragma once
// libmassedit — anatomy atlas (OpenSim .osim) as external ground truth.
// Parses muscles (Hill params + origin/insertion bodies), joins to the model by
// normalized name, validates the model against it, and can fill missing model
// metadata/Hill params from it.
#include "MassModel.h"
#include "Index.h"
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace mass {

struct AtlasMuscle {
    std::string name;
    std::string originBody, insertionBody;
    double f0 = 0, lm = 0, lt = 0, pen = 0;   // max iso force, optimal fiber len, tendon slack, pennation
};

class Atlas {
public:
    // Load an OpenSim .osim (XML). Returns false + err on parse failure.
    // Several .osim may be stacked (lower limb + arm + wrist); a later file does
    // not evict entries already loaded from an earlier one.
    bool loadOsim(const std::string& path, std::string* err = nullptr);

    // Load the synonym table (data/atlas/synonyms.json) that joins the atlas'
    // abbreviated names ("glut_max1") to this model's spelled-out bundles
    // ("Gluteus_Maximus", "Gluteus_Maximus1"). Without it the name join matches
    // almost nothing. Returns false + err on parse failure.
    bool loadSynonyms(const std::string& path, std::string* err = nullptr);

    size_t size() const { return mByNorm.size(); }
    size_t synonymCount() const { return mSynonym.size(); }
    // Lookup by model muscle name: the synonym table first, then normalized
    // matching (case/underscore/side insensitive). Null if absent.
    const AtlasMuscle* find(const std::string& modelMuscleName) const;

    // Names of the model bundles sharing this muscle's atlas entry (including
    // itself), sideless. Null when the muscle is not in the synonym table.
    const std::vector<std::string>* groupMembers(const std::string& modelMuscleName) const;

    // Whether a model body is an acceptable counterpart of an atlas body, per the
    // synonym table's `bodies` section (the atlas' finer skeleton collapses onto
    // ours: "calcn"/"talus" -> Talus, "ulna"/"radius" -> ForeArm). Falls back to
    // normalized name equality for bodies the table does not mention.
    bool bodyMatches(const std::string& modelBody, const std::string& atlasBody) const;

    // Normalize a name for joins: lowercase, drop non-alphanumerics, strip a
    // leading/trailing side marker (l/r). Exposed for body-name comparison.
    static std::string normalize(const std::string& s);

    // Index keys for every join in this class. A trailing l/r is irreducibly
    // ambiguous — the side of "FemurL" but part of the name in "Sartorius" — and
    // normalize() has to guess, which sends the same body down three paths in
    // the three notations in play ("femur_r", "FemurL", "Femur"). So no guess is
    // made: both readings are returned, entries are indexed under each, and two
    // names match when their key sets intersect.
    static std::vector<std::string> joinKeys(const std::string& s);
    // Whether two names denote the same thing, side ignored.
    static bool sameName(const std::string& a, const std::string& b);

    // Validate model muscles against the atlas. Returns [{muscle, issue, ...}]:
    // origin/insertion body mismatch, f0 deviation beyond tolerance. f0 is
    // compared per group (the bundles' sum against the atlas value), so splitting
    // one atlas muscle into several bundles is not reported as a deviation.
    static nlohmann::json validate(const Model& m, const Index& ix, const Atlas& atlas,
                                   double f0RelTol = 0.25);

    // Fill model muscles from the atlas: infer empty `side` from the name; if
    // fillHill, copy the pennation angle, rescale the group's f0 so the bundles
    // sum to the atlas value keeping their existing relative proportions, and
    // derive pcsa_cm2 from f0 / specific tension.
    //
    // fillLengths additionally rewrites lm/lt. It is separate because the two
    // models disagree on units: OpenSim stores optimal fiber and tendon slack
    // length in metres, while MASS normalizes the muscle-tendon length by its
    // rest value (Muscle::Getl_mt returns length / l_mt0), so lm/lt are
    // fractions. The metres are converted to that fraction — only the atlas'
    // tendon-to-fibre ratio carries over, and the model's rest pose is taken as
    // each fibre's optimal length. It shifts every force-length operating point,
    // so an already trained policy will not transfer.
    //
    // Returns the number of muscles changed.
    static int sync(Model& m, const Atlas& atlas, bool fillHill, bool dryRun,
                    bool fillLengths = false);

private:
    std::vector<AtlasMuscle> mMuscles;
    std::unordered_map<std::string, int> mByNorm;   // normalized name -> index
    // normalized model name -> index into mGroups; mGroups holds the sideless
    // member lists shared by every bundle of one atlas muscle.
    std::unordered_map<std::string, int> mSynonym;
    std::unordered_map<std::string, std::string> mAtlasKey;  // model name -> atlas name
    std::vector<std::vector<std::string>> mGroups;
    // normalized atlas body -> normalized model bodies accepted for it
    std::unordered_map<std::string, std::vector<std::string>> mBodyMap;
};

} // namespace mass
