#include "Atlas.h"
#include <tinyxml2.h>
#include <cctype>
#include <cmath>
#include <fstream>
#include <set>

namespace mass {
using json = nlohmann::json;
namespace xml = tinyxml2;

std::string Atlas::normalize(const std::string& s) {
    // split on non-alphanumerics into lowercase tokens, dropping bare side tokens
    std::vector<std::string> toks;
    std::string cur;
    bool droppedSide = false;
    auto flush = [&]{ if (!cur.empty()) {
        if (cur == "l" || cur == "r") droppedSide = true; else toks.push_back(cur);
        cur.clear();
    } };
    for (char c : s) {
        if (std::isalnum((unsigned char)c)) cur += (char)std::tolower((unsigned char)c);
        else flush();
    }
    flush();
    std::string t;
    for (auto& tk : toks) t += tk;
    // Only when the side wasn't already a separate token (camelCase "FemurR" ->
    // "femurr"), strip a trailing side letter. If a side token was dropped
    // ("femur_r" -> "femur"), leave it — else we'd eat the real trailing letter.
    if (!droppedSide && t.size() > 1 && (t.back() == 'l' || t.back() == 'r')) t.pop_back();
    return t;
}

std::vector<std::string> Atlas::joinKeys(const std::string& s) {
    std::string t, cur;
    auto flush = [&]{ if (!cur.empty()) { if (cur != "l" && cur != "r") t += cur; cur.clear(); } };
    for (char c : s) {
        if (std::isalnum((unsigned char)c)) cur += (char)std::tolower((unsigned char)c);
        else flush();
    }
    flush();
    std::vector<std::string> keys{ t };
    if (t.size() > 1 && (t.back() == 'l' || t.back() == 'r'))
        keys.push_back(t.substr(0, t.size() - 1));   // reading where it was the side
    return keys;
}

bool Atlas::sameName(const std::string& a, const std::string& b) {
    for (const auto& ka : joinKeys(a))
        for (const auto& kb : joinKeys(b))
            if (ka == kb) return true;
    return false;
}

// Index `value` under every reading of `name`, first writer winning.
template <class V>
static void indexAll(std::unordered_map<std::string, V>& m, const std::string& name, const V& value) {
    for (const auto& k : Atlas::joinKeys(name)) m.emplace(k, value);
}

// Look `name` up under every reading of it.
template <class Map>
static typename Map::const_iterator lookupAll(const Map& m, const std::string& name) {
    for (const auto& k : Atlas::joinKeys(name)) {
        auto it = m.find(k);
        if (it != m.end()) return it;
    }
    return m.end();
}

// last path component of an OpenSim socket path, e.g. "/bodyset/femur_r" -> "femur_r"
static std::string lastPath(const std::string& s) {
    size_t p = s.find_last_of("/");
    return p == std::string::npos ? s : s.substr(p + 1);
}

// resolve a PathPoint's body: <body> (3.x) or <socket_parent_frame> (4.x)
static std::string pointBody(xml::XMLElement* pt) {
    if (auto* b = pt->FirstChildElement("body"))
        if (b->GetText()) return b->GetText();
    if (auto* s = pt->FirstChildElement("socket_parent_frame"))
        if (s->GetText()) return lastPath(s->GetText());
    return "";
}

static double childD(xml::XMLElement* e, const char* tag) {
    auto* c = e->FirstChildElement(tag);
    if (c && c->GetText()) return std::atof(c->GetText());
    return 0.0;
}

// Recursively collect muscle elements (any element with a max_isometric_force child).
static void collect(xml::XMLElement* e, std::vector<AtlasMuscle>& out) {
    for (xml::XMLElement* c = e->FirstChildElement(); c; c = c->NextSiblingElement()) {
        if (c->FirstChildElement("max_isometric_force")) {
            AtlasMuscle a;
            const char* nm = c->Attribute("name");
            a.name = nm ? nm : "";
            a.f0  = childD(c, "max_isometric_force");
            a.lm  = childD(c, "optimal_fiber_length");
            a.lt  = childD(c, "tendon_slack_length");
            a.pen = childD(c, "pennation_angle_at_optimal");
            if (a.pen == 0.0) a.pen = childD(c, "pennation_angle");
            // path points -> origin (first) / insertion (last)
            std::vector<std::string> bodies;
            if (auto* gp = c->FirstChildElement("GeometryPath")) {
                if (auto* pps = gp->FirstChildElement("PathPointSet")) {
                    if (auto* objs = pps->FirstChildElement("objects")) {
                        for (xml::XMLElement* pt = objs->FirstChildElement(); pt; pt = pt->NextSiblingElement()) {
                            std::string b = pointBody(pt);
                            if (!b.empty()) bodies.push_back(b);
                        }
                    }
                }
            }
            if (!bodies.empty()) { a.originBody = bodies.front(); a.insertionBody = bodies.back(); }
            out.push_back(std::move(a));
        }
        collect(c, out);   // recurse (muscles live deep under Model/ForceSet/objects)
    }
}

bool Atlas::loadOsim(const std::string& path, std::string* err) {
    xml::XMLDocument doc;
    if (doc.LoadFile(path.c_str()) != xml::XML_SUCCESS) {
        if (err) *err = doc.ErrorStr() ? doc.ErrorStr() : "xml parse error";
        return false;
    }
    xml::XMLElement* root = doc.RootElement();
    if (!root) return true;
    // Stack onto whatever is already loaded: one .osim rarely covers the whole
    // body, so lower limb / arm / wrist models are loaded in sequence. First
    // file to define a name wins.
    std::vector<AtlasMuscle> loaded;
    collect(root, loaded);
    for (auto& a : loaded) {
        if (lookupAll(mByNorm, a.name) != mByNorm.end()) continue;
        indexAll(mByNorm, a.name, (int)mMuscles.size());
        mMuscles.push_back(std::move(a));
    }
    return true;
}

bool Atlas::loadSynonyms(const std::string& path, std::string* err) {
    std::ifstream in(path);
    if (!in) { if (err) *err = "cannot open " + path; return false; }
    json doc;
    try { in >> doc; }
    catch (const std::exception& e) { if (err) *err = e.what(); return false; }

    mSynonym.clear(); mGroups.clear(); mAtlasKey.clear(); mBodyMap.clear();
    for (auto it = doc["groups"].begin(); it != doc["groups"].end(); ++it) {
        std::vector<std::string> members;
        for (const auto& mem : it.value().value("members", json::array()))
            members.push_back(mem.get<std::string>());
        if (members.empty()) continue;
        int gi = (int)mGroups.size();
        mGroups.push_back(members);
        // every bundle of the group resolves to the group's atlas entry
        std::string atlasName = it.key();
        for (const auto& mem : members) {
            indexAll(mSynonym, mem, gi);
            indexAll(mAtlasKey, mem, atlasName);
        }
    }
    for (auto it = doc["bodies"].begin(); it != doc["bodies"].end(); ++it) {
        std::vector<std::string> accepted;
        for (const auto& b : it.value())
            accepted.push_back(b.get<std::string>());
        indexAll(mBodyMap, it.key(), accepted);
    }
    return true;
}

bool Atlas::bodyMatches(const std::string& modelBody, const std::string& atlasBody) const {
    auto it = lookupAll(mBodyMap, atlasBody);
    if (it == mBodyMap.end()) return sameName(modelBody, atlasBody);
    for (const auto& ok : it->second) if (sameName(modelBody, ok)) return true;
    return false;
}

const AtlasMuscle* Atlas::find(const std::string& modelMuscleName) const {
    std::string name = modelMuscleName;
    auto syn = lookupAll(mAtlasKey, name);
    if (syn != mAtlasKey.end()) name = syn->second;
    auto it = lookupAll(mByNorm, name);
    return it != mByNorm.end() ? &mMuscles[it->second] : nullptr;
}

const std::vector<std::string>* Atlas::groupMembers(const std::string& modelMuscleName) const {
    auto it = lookupAll(mSynonym, modelMuscleName);
    return it != mSynonym.end() ? &mGroups[it->second] : nullptr;
}

// "L_Gluteus_Maximus" -> "L_"; a muscle without a side marker yields "".
static std::string sidePrefix(const std::string& n) {
    if (n.rfind("L_", 0) == 0) return "L_";
    if (n.rfind("R_", 0) == 0) return "R_";
    return "";
}

// The model muscles that share `mu`'s atlas entry on `mu`'s own side, in group
// order. Falls back to {&mu} when the muscle is not in the synonym table.
template <class M, class ModelT>
static std::vector<M*> bundlesOf(ModelT& m, const Atlas& atlas, M& mu) {
    std::vector<M*> out;
    const std::vector<std::string>* members = atlas.groupMembers(mu.name);
    if (!members) { out.push_back(&mu); return out; }
    std::string pre = sidePrefix(mu.name);
    for (const auto& mem : *members)
        for (auto& cand : m.muscles)
            if (cand.name == pre + mem) { out.push_back(&cand); break; }
    if (out.empty()) out.push_back(&mu);
    return out;
}

json Atlas::validate(const Model& m, const Index& ix, const Atlas& atlas, double f0RelTol) {
    (void)ix;
    json findings = json::array();
    std::set<std::string> f0Done;      // side prefix + atlas name, checked once per group
    for (const auto& mu : m.muscles) {
        const AtlasMuscle* a = atlas.find(mu.name);
        if (!a) { findings.push_back({ {"muscle", mu.name}, {"issue", "not_in_atlas"} }); continue; }
        if (!mu.waypoints.empty()) {
            std::string org = mu.waypoints.front().body;
            std::string ins = mu.waypoints.back().body;
            // Origin and insertion are compared as an unordered pair: which end a
            // model lists first is a bookkeeping convention (gait2392 runs the
            // external oblique pelvis-first, anatomy runs it ribs-first) and the
            // path — hence the mechanics — is the same either way.
            bool flipped = atlas.bodyMatches(org, a->insertionBody)
                        && atlas.bodyMatches(ins, a->originBody);
            if (!flipped) {
                if (!a->originBody.empty() && !atlas.bodyMatches(org, a->originBody))
                    findings.push_back({ {"muscle", mu.name}, {"issue", "origin_mismatch"},
                                         {"model", org}, {"atlas", a->originBody} });
                if (!a->insertionBody.empty() && !atlas.bodyMatches(ins, a->insertionBody))
                    findings.push_back({ {"muscle", mu.name}, {"issue", "insertion_mismatch"},
                                         {"model", ins}, {"atlas", a->insertionBody} });
            }
        }
        if (mu.pen_angle == 0.0 && a->pen > 0.0)
            findings.push_back({ {"muscle", mu.name}, {"issue", "pennation_missing"},
                                 {"model", mu.pen_angle}, {"atlas", a->pen} });
        // f0 is compared for the group as a whole: the model splits one atlas
        // muscle into several bundles, so only their sum is meaningful.
        if (a->f0 > 0 && f0Done.insert(sidePrefix(mu.name) + a->name).second) {
            auto bundles = bundlesOf<const Muscle, const Model>(m, atlas, mu);
            double sum = 0;
            for (const auto* b : bundles) sum += b->f0;
            double rel = std::fabs(sum - a->f0) / a->f0;
            if (sum > 0 && rel > f0RelTol)
                findings.push_back({ {"muscle", mu.name}, {"issue", "f0_deviation"},
                                     {"model", sum}, {"atlas", a->f0}, {"rel", rel},
                                     {"bundles", (int)bundles.size()} });
        }
    }
    return findings;
}

int Atlas::sync(Model& m, const Atlas& atlas, bool fillHill, bool dryRun, bool fillLengths) {
    int changed = 0;
    std::set<std::string> f0Done;      // side prefix + atlas name, rescaled once per group
    const double tension = m.meta.specific_tension_N_cm2 > 0
                         ? m.meta.specific_tension_N_cm2 : 60.0;
    for (auto& mu : m.muscles) {
        const AtlasMuscle* a = atlas.find(mu.name);
        if (!a) continue;
        bool touched = false;
        if (mu.side.empty()) {
            if (mu.name.rfind("L_", 0) == 0) { if (!dryRun) mu.side = "L"; touched = true; }
            else if (mu.name.rfind("R_", 0) == 0) { if (!dryRun) mu.side = "R"; touched = true; }
        }
        if (fillHill && a->f0 > 0) {
            if (!dryRun) {
                mu.pen_angle = a->pen;
                if (fillLengths && a->lm > 0 && a->lt > 0) {
                    // metres -> MASS' fraction of the rest muscle-tendon length
                    double fibre = a->lm * std::cos(a->pen);
                    double ref   = fibre + a->lt;
                    mu.lt = a->lt / ref;
                    mu.lm = 1.0 - mu.lt;
                }
            }
            touched = true;
            // Distribute the atlas f0 over the group's bundles, preserving the
            // proportions already authored in the model (equal split if the
            // bundles carry no force yet), then derive PCSA from it.
            if (f0Done.insert(sidePrefix(mu.name) + a->name).second) {
                auto bundles = bundlesOf<Muscle, Model>(m, atlas, mu);
                double sum = 0;
                for (const auto* b : bundles) sum += b->f0;
                for (auto* b : bundles) {
                    double share = sum > 0 ? b->f0 / sum : 1.0 / (double)bundles.size();
                    if (!dryRun) {
                        b->f0 = a->f0 * share;
                        b->pcsa_cm2 = b->f0 / tension;
                    }
                }
            }
        }
        if (touched) changed++;
    }
    return changed;
}

} // namespace mass
