#include "Muscles.h"
#include "Atlas.h"
#include <tinyxml2.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace mass {
using json = nlohmann::json;
namespace xml = tinyxml2;

std::vector<MuscleSource> Muscles::sources(const std::string& dir) {
    return {
        { dir + "/gait2392_thelen2003muscle.osim", "lower limb and trunk" },
        { dir + "/arm26.osim",                     "upper arm" },
        { dir + "/wrist.osim",                     "forearm, wrist and hand" },
    };
}

namespace {

// ---- reading the atlas -----------------------------------------------------
struct AtlasPoint { std::string body; Vec3 p; };
struct AtlasMuscleFull {
    std::string name;
    double f0 = 0, lm = 0, lt = 0, pen = 0;
    std::vector<AtlasPoint> path;
};

Vec3 parseVec(const char* text) {
    Vec3 v{};
    if (text) { std::istringstream ss(text); ss >> v[0] >> v[1] >> v[2]; }
    return v;
}
double childD(xml::XMLElement* e, const char* tag) {
    auto* c = e->FirstChildElement(tag);
    return (c && c->GetText()) ? std::atof(c->GetText()) : 0.0;
}
std::string lastPath(const std::string& s) {
    const size_t p = s.find_last_of('/');
    return p == std::string::npos ? s : s.substr(p + 1);
}

void collect(xml::XMLElement* e, std::vector<AtlasMuscleFull>& out) {
    for (xml::XMLElement* c = e->FirstChildElement(); c; c = c->NextSiblingElement()) {
        if (c->FirstChildElement("max_isometric_force")) {
            AtlasMuscleFull a;
            a.name = c->Attribute("name") ? c->Attribute("name") : "";
            a.f0 = childD(c, "max_isometric_force");
            a.lm = childD(c, "optimal_fiber_length");
            a.lt = childD(c, "tendon_slack_length");
            a.pen = childD(c, "pennation_angle_at_optimal");
            if (a.pen == 0.0) a.pen = childD(c, "pennation_angle");
            for (xml::XMLElement* pt = c; pt; pt = nullptr) {
                for (xml::XMLElement* p : {pt}) (void)p;
            }
            // walk every PathPoint under this muscle
            std::vector<xml::XMLElement*> stack{ c };
            while (!stack.empty()) {
                xml::XMLElement* cur = stack.back(); stack.pop_back();
                for (xml::XMLElement* k = cur->FirstChildElement(); k; k = k->NextSiblingElement()) {
                    const std::string tag = k->Name() ? k->Name() : "";
                    if (tag == "PathPoint") {
                        AtlasPoint ap;
                        if (auto* b = k->FirstChildElement("body"))
                            ap.body = b->GetText() ? b->GetText() : "";
                        else if (auto* s = k->FirstChildElement("socket_parent_frame"))
                            ap.body = lastPath(s->GetText() ? s->GetText() : "");
                        if (auto* l = k->FirstChildElement("location"))
                            ap.p = parseVec(l->GetText());
                        if (!ap.body.empty()) a.path.push_back(ap);
                    }
                    stack.push_back(k);
                }
            }
            if (a.path.size() >= 2) out.push_back(std::move(a));
        }
        collect(c, out);
    }
}

// ---- bounding boxes --------------------------------------------------------
struct Box { Vec3 lo{ 1e9,1e9,1e9 }, hi{ -1e9,-1e9,-1e9 }; bool valid = false; };

void grow(Box& b, const Vec3& p) {
    for (int i = 0; i < 3; i++) { b.lo[i] = std::min(b.lo[i], p[i]); b.hi[i] = std::max(b.hi[i], p[i]); }
    b.valid = true;
}

bool objBox(const std::string& path, Box& out, double scale) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.size() < 3 || line[0] != 'v' || line[1] != ' ') continue;
        std::istringstream ss(line.substr(2));
        Vec3 p{}; ss >> p[0] >> p[1] >> p[2];
        grow(out, { p[0] * scale, p[1] * scale, p[2] * scale });
    }
    return out.valid;
}

// Where a point sits inside a box, 0..1 per axis. Clamped: atlas attachments
// sometimes sit just outside the mesh, and a wild extrapolation helps nobody.
Vec3 fractionIn(const Box& b, const Vec3& p) {
    Vec3 f{};
    for (int i = 0; i < 3; i++) {
        const double span = b.hi[i] - b.lo[i];
        f[i] = span > 1e-9 ? (p[i] - b.lo[i]) / span : 0.5;
        f[i] = std::max(-0.25, std::min(1.25, f[i]));
    }
    return f;
}

Vec3 pointAt(const Box& b, const Vec3& f) {
    return { b.lo[0] + f[0] * (b.hi[0] - b.lo[0]),
             b.lo[1] + f[1] * (b.hi[1] - b.lo[1]),
             b.lo[2] + f[2] * (b.hi[2] - b.lo[2]) };
}


// Which converted mesh stands for an atlas body. Spelled out because the names
// do not correspond: the OpenSim foot body is "calcn_r" and its mesh "r_foot".
std::vector<std::string> atlasMeshes(const std::string& body) {
    static const std::map<std::string, std::vector<std::string>> M = {
        { "pelvis", { "pelvis", "r_pelvis" } },
        { "torso",  { "hat_spine" } },
        { "femur_r", { "r_femur" } },   { "femur_l", { "l_femur" } },
        { "tibia_r", { "r_tibia" } },   { "tibia_l", { "l_tibia" } },
        { "talus_r", { "r_talus" } },   { "talus_l", { "l_talus" } },
        { "calcn_r", { "r_foot" } },    { "calcn_l", { "l_foot" } },
        { "toes_r",  { "r_bofoot" } },  { "toes_l",  { "l_bofoot" } },
        { "patella_r", { "r_patella" } }, { "patella_l", { "l_patella" } },
        { "humerus_r", { "humerus_rv", "r_humerus" } },
        { "humerus_l", { "humerus_lv", "l_humerus" } },
        { "ulna_r", { "ulna_rv", "r_ulna" } },  { "ulna_l", { "ulna_lv", "l_ulna" } },
        { "radius_r", { "radius_rv", "r_radius" } },
        { "radius_l", { "radius_lv", "l_radius" } },
        { "r_humerus", { "arm_r_humerus", "humerus_rv" } },
        { "radius", { "arm_r_radius", "radius_rv" } },
        { "ulna", { "arm_r_ulna", "ulna_rv" } },
        { "scaphoid", { "arm_r_scaphoid", "scaphoid_rvs" } },
        { "capitate", { "arm_r_capitate", "capitate_rvs" } },
        { "trapezoid", { "trapezoid_rvs" } },
        { "triquetrum", { "triquetrum_rvs" } },
        { "metacarpal2", { "metacarpal2_rvs", "arm_r_2mc" } },
        { "metacarpal3", { "metacarpal3_rvs", "arm_r_3mc" } },
        { "metacarpal4", { "metacarpal4_rvs", "arm_r_4mc" } },
        { "metacarpal5", { "metacarpal5_rvs", "arm_r_5mc" } },
        { "thumb", { "thumb_proximal_rvs" } },
    };
    auto it = M.find(body);
    return it != M.end() ? it->second : std::vector<std::string>{};
}

// ---- atlas body -> our bone ------------------------------------------------
// The atlas' skeleton is coarser than ours: its "torso" is the whole trunk and
// its "toes" every toe. A single name cannot map onto one of our bones, so
// those resolve by where the attachment sits along the body — which is exactly
// what having an articulated column buys.
struct Mapping {
    // The bones this atlas body corresponds to. Several, when the atlas fuses
    // what we articulate: its forearm is ulna+radius+hand in one body, so a
    // fraction along it has to be taken over all three or a tendon that inserts
    // near the elbow ends up in the middle of the hand.
    std::vector<std::string> bones;
    bool byHeight = false;     // resolve up the trunk instead (the atlas' torso)
};

std::map<std::string, Mapping> bodyMap() {
    std::map<std::string, Mapping> m;
    for (const char* side : { "r", "l" }) {
        const std::string s(side);
        const std::string our = (s == "r" ? "right" : "left");
        m["femur_" + s]  = { { our + "_femur" }, false };
        m["tibia_" + s]  = { { our + "_tibia", our + "_fibula" }, false };
        m["talus_" + s]  = { { our + "_talus" }, false };
        m["calcn_" + s]  = { { our + "_calcaneus", our + "_cuboid_bone",
                          our + "_first_metatarsal_bone", our + "_fifth_metatarsal_bone" }, false };
        m["toes_" + s]   = { { "proximal_phalanx_of_" + our + "_big_toe",
                          "proximal_phalanx_of_" + our + "_little_toe" }, false };
        m["patella_" + s] = { { our + "_patella" }, false };
        m["humerus_" + s] = { { our + "_humerus" }, false };
        m["ulna_" + s]   = { { our + "_ulna" }, false };
        m["radius_" + s] = { { our + "_radius" }, false };
        m["hand_" + s]   = { { our + "_capitate" }, false };
    }
    m["pelvis"] = { { "right_hip_bone", "sacrum" }, false };   // side refined below
    m["torso"]  = { {}, true };
    m["ground"] = { { "right_humerus" }, false };   // arm26/wrist anchor the limb
    m["base"]   = { { "right_scapula" }, false };
    // wrist model
    m["radius"] = { { "right_radius" }, false };
    m["ulna"]   = { { "right_ulna" }, false };
    m["scaphoid"] = { { "right_scaphoid" }, false };
    m["capitate"] = { { "right_capitate" }, false };
    m["trapezoid"] = { { "right_trapezoid" }, false };
    m["triquetrum"] = { { "right_triquetral" }, false };
    m["metacarpal2"] = { { "right_second_metacarpal_bone" }, false };
    m["metacarpal3"] = { { "right_third_metacarpal_bone" }, false };
    m["metacarpal4"] = { { "right_fourth_metacarpal_bone" }, false };
    m["metacarpal5"] = { { "right_fifth_metacarpal_bone" }, false };
    m["Iphalanx1"] = { { "proximal_phalanx_of_right_index_finger" }, false };
    m["Iphalanx2"] = { { "middle_phalanx_of_right_index_finger" }, false };
    m["Iphalanx3"] = { { "distal_phalanx_of_right_index_finger" }, false };
    m["midfinger"] = { { "proximal_phalanx_of_right_middle_finger" }, false };
    m["ringfinger"] = { { "proximal_phalanx_of_right_ring_finger" }, false };
    m["littlefinger"] = { { "proximal_phalanx_of_right_little_finger" }, false };
    m["thumb"] = { { "proximal_phalanx_of_right_thumb" }, false };
    // the arm26 forearm is ulna+radius+hand in one body: resolve along it, so a
    // distal attachment lands on the hand and a proximal one on the ulna
    m["r_ulna_radius_hand"] = { { "right_ulna", "right_radius", "right_capitate",
                                 "right_third_metacarpal_bone" }, false };
    m["r_humerus"] = { { "right_humerus" }, false };
    return m;
}

bool endsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}

}  // namespace

// ------------------------------------------------------------------ build ---
json Muscles::build(Model& m, const std::string& atlasdir,
                    const BodyProfile& profile, std::string* err) {
    if (m.skeleton.empty()) {
        if (err) *err = "no skeleton: build_skeleton first";
        return json();
    }

    // Our bones, boxed in world metres, straight off the model.
    std::map<std::string, Box> ours;
    for (const Node& n : m.skeleton) {
        Box b;
        for (int i = 0; i < 3; i++) {
            b.lo[i] = n.body.t.translation[i] - n.body.size[i] / 2;
            b.hi[i] = n.body.t.translation[i] + n.body.size[i] / 2;
        }
        b.valid = true;
        ours[n.id] = b;
    }

    // The trunk, in the order a muscle climbs it: sacrum, lumbar, thoracic,
    // cervical. An attachment on the atlas' one-piece torso lands on whichever
    // of these its height picks out.
    std::vector<std::pair<double, std::string>> trunk;
    for (const Node& n : m.skeleton) {
        const bool isTrunk = n.id == "sacrum" || n.id.find("_vertebra") != std::string::npos
                          || n.id == "atlas" || n.id == "axis";
        if (isTrunk) trunk.push_back({ n.body.t.translation[1], n.id });
    }
    std::sort(trunk.begin(), trunk.end());

    const std::map<std::string, Mapping> bmap = bodyMap();
    Box atlasTorso;   // filled from the first source that has a torso mesh
    objBox(atlasdir + "/bones/hat_spine.obj", atlasTorso, 0.01);

    m.muscles.clear();
    json skipped = json::object();
    std::set<std::string> unmapped;
    int nSingleBone = 0, nNoMesh = 0, nDup = 0;
    std::set<std::string> noMeshBodies;
    std::set<std::string> seen;

    for (const MuscleSource& src : sources(atlasdir)) {
        xml::XMLDocument doc;
        if (doc.LoadFile(src.osim.c_str()) != xml::XML_SUCCESS) continue;
        std::vector<AtlasMuscleFull> found;
        if (doc.RootElement()) collect(doc.RootElement(), found);

        // Box every body of this model from *all* its attachments. Some bodies
        // are fictitious ("base") or composite ("r_ulna_radius_hand") and have
        // no mesh to measure; the spread of every muscle that touches them is a
        // far better stand-in than one muscle's two points.
        std::map<std::string, Box> fromPaths;
        for (const AtlasMuscleFull& a : found)
            for (const AtlasPoint& p : a.path) grow(fromPaths[p.body], p.p);

        for (const AtlasMuscleFull& a : found) {
            // atlas bone boxes, in their own local frames
            std::map<std::string, Box> atlasBox;
            bool haveAll = true;
            for (const AtlasPoint& p : a.path) {
                if (atlasBox.count(p.body)) continue;
                Box b;
                // The atlas does not name its meshes after its bodies — its
                // foot body is "calcn_r" and its mesh "r_foot" — so the pairing
                // is spelled out rather than guessed.
                for (const std::string& mesh : atlasMeshes(p.body))
                    if (objBox(atlasdir + "/bones/" + mesh + ".obj", b, 0.01)) break;
                if (!b.valid) {
                    // No mesh for this atlas body — it is fictitious ("base") or
                    // composite ("r_ulna_radius_hand"). Estimating its box from
                    // the attachments does not work: that box covers where the
                    // muscles insert, not the bone, so a fraction of it applied
                    // to a whole bone lands anywhere. Better to skip the muscle
                    // and say so than to emit a path that is 90% too long.
                    noMeshBodies.insert(p.body);
                    haveAll = false;
                    break;
                }
                atlasBox[p.body] = b;
            }
            if (!haveAll) { nNoMesh++; continue; }

            // decide the side once, from the atlas name, so "pelvis" and the
            // wrist model's unsided bones land on the correct half of the body
            const bool leftSide = endsWith(a.name, "_l") || a.name.find("_l_") != std::string::npos;

            std::vector<Waypoint> wps;
            bool ok = true;
            for (const AtlasPoint& p : a.path) {
                auto it = bmap.find(p.body);
                if (it == bmap.end()) { unmapped.insert(p.body); ok = false; break; }

                const Vec3 frac = fractionIn(atlasBox[p.body], p.p);

                if (it->second.byHeight) {
                    // the atlas' one-piece torso: the height of the attachment
                    // picks the vertebra, which is what an articulated column
                    // makes possible
                    if (trunk.empty()) { ok = false; break; }
                    const double f = std::max(0.0, std::min(1.0, frac[1]));
                    const size_t idx = std::min(trunk.size() - 1,
                                                (size_t)std::llround(f * (trunk.size() - 1)));
                    auto ob = ours.find(trunk[idx].second);
                    if (ob == ours.end()) { ok = false; break; }
                    Waypoint w;
                    w.body = trunk[idx].second;
                    w.p = pointAt(ob->second, frac);
                    wps.push_back(w);
                    continue;
                }

                // Take the fraction over the union of the bones this atlas body
                // stands for, then anchor to whichever of them the point lands
                // in. Taking it over one bone of a composite would put an elbow
                // tendon in the middle of the hand.
                Box unionBox;
                std::vector<std::string> targets;
                for (std::string t : it->second.bones) {
                    if (leftSide) {
                        if (t.compare(0, 6, "right_") == 0) t = "left_" + t.substr(6);
                        const size_t r = t.find("_right_");
                        if (r != std::string::npos) t.replace(r, 7, "_left_");
                    }
                    auto ob = ours.find(t);
                    if (ob == ours.end()) continue;
                    targets.push_back(t);
                    grow(unionBox, ob->second.lo);
                    grow(unionBox, ob->second.hi);
                }
                if (targets.empty()) { unmapped.insert(p.body); ok = false; break; }

                const Vec3 world = pointAt(unionBox, frac);
                std::string best = targets.front();
                double bestD = 1e18;
                for (const std::string& t : targets) {
                    const Box& b = ours[t];
                    double d = 0;
                    for (int i = 0; i < 3; i++) {
                        const double c = (b.lo[i] + b.hi[i]) / 2;
                        const double half = (b.hi[i] - b.lo[i]) / 2;
                        const double over = std::max(0.0, std::fabs(world[i] - c) - half);
                        d += over * over;
                    }
                    if (d < bestD) { bestD = d; best = t; }
                }
                Waypoint w;
                w.body = best;
                w.p = world;
                wps.push_back(w);
            }
            if (!ok || wps.size() < 2) continue;

            // A muscle anchored to a single bone crosses no joint and adds no
            // torque, but it is not spurious: it attaches to another muscle, to
            // an aponeurosis or to a shared tendon, and it is part of the
            // body's shape. Keep it, and count it so the report is honest about
            // how many of these there are.
            bool spans = false;
            for (const Waypoint& w : wps) if (w.body != wps[0].body) { spans = true; break; }
            if (!spans) nSingleBone++;

            std::string name = a.name;
            for (char& c : name) if (c == ' ') c = '_';
            std::string base = name;
            if (endsWith(base, "_r") || endsWith(base, "_l")) base = base.substr(0, base.size() - 2);
            const std::string full = (leftSide ? "L_" : "R_") + base;
            if (!seen.insert(full).second) { nDup++; continue; }

            Muscle mu;
            mu.name = full;
            mu.side = leftSide ? "L" : "R";
            // Force follows physiological cross-section, which follows body
            // size: areas go with the square of a length ratio.
            const double k = profile.height_m / 1.75;
            mu.f0 = a.f0 * k * k * (profile.mass_kg / 75.0);
            mu.pcsa_cm2 = mu.f0 / (profile.specific_tension > 0 ? profile.specific_tension : 60.0);
            mu.pen_angle = a.pen;
            // metres -> MASS' fraction of the rest muscle-tendon length
            const double fibre = a.lm * std::cos(a.pen);
            const double ref = fibre + a.lt;
            if (ref > 1e-9) { mu.lt = a.lt / ref; mu.lm = 1.0 - mu.lt; }
            mu.lmax = -0.1;
            mu.waypoints = std::move(wps);
            m.muscles.push_back(std::move(mu));
        }
    }

    // mirror every right-side muscle onto the left, since the atlases are
    // one-sided for the arm and hand
    const size_t built = m.muscles.size();
    for (size_t i = 0; i < built; i++) {
        const Muscle& r = m.muscles[i];
        if (r.side != "R") continue;
        const std::string other = "L_" + r.name.substr(2);
        if (seen.count(other)) continue;
        Muscle l = r;
        l.name = other;
        l.side = "L";
        bool ok = true;
        for (Waypoint& w : l.waypoints) {
            std::string b = w.body;
            if (b.compare(0, 6, "right_") == 0) b = "left_" + b.substr(6);
            else {
                const size_t p = b.find("_right_");
                if (p != std::string::npos) b.replace(p, 7, "_left_");
            }
            if (b == w.body) continue;          // midline bone: keep it
            if (!ours.count(b)) { ok = false; break; }
            w.body = b;
            w.p[0] = -w.p[0];
        }
        if (ok) { seen.insert(other); m.muscles.push_back(std::move(l)); }
    }

    m.assignUids();

    json unmappedList = json::array();
    for (const std::string& u : unmapped) unmappedList.push_back(u);
    skipped["kept_single_bone"] = nSingleBone;
    skipped["duplicate"] = nDup;
    skipped["no_mesh"] = nNoMesh;
    json nm = json::array();
    for (const std::string& b : noMeshBodies) nm.push_back(b);
    skipped["no_mesh_bodies"] = nm;

    // report what the result looks like
    std::map<std::string, int> perBone;
    for (const Muscle& mu : m.muscles) {
        std::set<std::string> bs;
        for (const Waypoint& w : mu.waypoints) bs.insert(w.body);
        for (const std::string& b : bs) perBone[b]++;
    }
    json problems = json::array();
    for (const Muscle& mu : m.muscles) {
        if (mu.f0 <= 0) problems.push_back(mu.name + " has no force");
        if (mu.waypoints.size() < 2) problems.push_back(mu.name + " has one waypoint");
    }
    return { { "muscles", (int)m.muscles.size() },
             { "bones_with_muscle", (int)perBone.size() },
             { "skipped", skipped },
             { "unmapped", unmappedList },
             { "problems", problems } };
}

} // namespace mass
