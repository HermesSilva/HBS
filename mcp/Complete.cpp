#include "Complete.h"
#include "Kinematics.h"   // normalize
#include <cstdio>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>

namespace mass {

using json = nlohmann::json;

static Vec3 add(const Vec3& a, const Vec3& b) { return { a[0]+b[0], a[1]+b[1], a[2]+b[2] }; }
static Vec3 mul(const Vec3& a, double s) { return { a[0]*s, a[1]*s, a[2]*s }; }

// Vertex positions of an OBJ (positions only — the digit measurement is a point
// cloud problem, faces do not matter).
static bool loadObjPoints(const std::string& path, double scale, std::vector<Vec3>& out) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.size() < 3 || line[0] != 'v' || line[1] != ' ') continue;
        std::istringstream ss(line.substr(2));
        Vec3 v{};
        ss >> v[0] >> v[1] >> v[2];
        out.push_back({ v[0] * scale, v[1] * scale, v[2] * scale });
    }
    return !out.empty();
}

// One digit as found in the mesh, in the hand's (along, across) frame.
struct MeshDigit {
    double across;   // centre of the digit sideways
    double base;     // where it separates from the palm
    double tip;      // how far it reaches
    double width;    // its own width sideways
};

// Find the digits by slicing the mesh along the hand and looking at where the
// cross-section breaks into separate lumps: near the wrist it is one solid palm,
// and past the knuckles it splits into the fingers. The slice with the most
// lumps gives their lateral positions; each lump's own vertices then give how
// far it reaches.
static std::vector<MeshDigit> measureDigits(const std::vector<double>& along,
                                            const std::vector<double>& across,
                                            double gap) {
    double a0 = *std::min_element(along.begin(), along.end());
    double a1 = *std::max_element(along.begin(), along.end());
    double span = a1 - a0;
    if (span <= 0) return {};

    std::vector<std::pair<double,double>> best;   // [lo,hi] across-ranges
    double bestAt = 0;
    for (int i = 8; i < 19; i++) {                // 40%..95% along the hand
        double lo = a0 + span * i / 20.0, hi = a0 + span * (i + 1) / 20.0;
        std::vector<double> w;
        for (size_t k = 0; k < along.size(); k++)
            if (along[k] >= lo && along[k] < hi) w.push_back(across[k]);
        if (w.size() < 24) continue;
        std::sort(w.begin(), w.end());
        std::vector<std::pair<double,double>> lumps;
        double s = w.front(), prev = w.front();
        int n = 1;
        for (size_t k = 1; k < w.size(); k++) {
            if (w[k] - prev > gap) {
                if (n >= 6) lumps.push_back({ s, prev });
                s = w[k]; n = 0;
            }
            prev = w[k]; n++;
        }
        if (n >= 6) lumps.push_back({ s, prev });
        if (lumps.size() > best.size()) { best = lumps; bestAt = lo; }
    }
    if (best.size() < 2) return {};

    // The slice above identifies the digits reliably, but it sits past the
    // knuckles, which would shorten every finger. So find the knuckle line
    // separately: the most proximal slice where the long fingers already stand
    // apart. The thumb is excluded from that count — it branches off much
    // earlier and would make a proximal slice look split when the fingers are
    // still fused. It is the outlying lump on the radial side.
    double fingersLo = best.front().first, fingersHi = best.back().second;
    size_t longFingers = best.size();
    if (best.size() >= 5) {                        // drop the thumb's lump
        fingersHi = best[best.size() - 1].second;
        fingersLo = best[1].first;
        longFingers = best.size() - 1;
    }
    double knuckle = bestAt;
    for (int i = 4; i < 19; i++) {
        double lo = a0 + span * i / 20.0, hi = a0 + span * (i + 1) / 20.0;
        std::vector<double> w;
        for (size_t k = 0; k < along.size(); k++)
            if (along[k] >= lo && along[k] < hi && across[k] >= fingersLo && across[k] <= fingersHi)
                w.push_back(across[k]);
        if (w.size() < 24) continue;
        std::sort(w.begin(), w.end());
        size_t lumps = 1;
        for (size_t k = 1; k < w.size(); k++) if (w[k] - w[k-1] > gap) lumps++;
        if (lumps >= longFingers) { knuckle = lo; break; }
    }

    std::vector<MeshDigit> digits;
    for (auto& lump : best) {
        double lo = lump.first - gap * 0.5, hi = lump.second + gap * 0.5;
        double refW = lump.second - lump.first;

        // Tip: nothing lies beyond the fingers, so the far end of this lateral
        // band is the fingertip.
        double tip = bestAt, sum = 0;
        int n = 0;
        for (size_t k = 0; k < along.size(); k++) {
            if (across[k] < lo || across[k] > hi) continue;
            if (along[k] > tip) tip = along[k];
            sum += across[k]; n++;
        }
        if (!n) continue;

        // Base: the knuckle line, i.e. the slice where the digits first stand
        // apart. Measuring each digit's own minimum instead runs into the palm,
        // which spans every lateral position, and puts the knuckle at the wrist.
        // The thumb's own base is left to the caller: it branches off much
        // further back and this slice says nothing about it.
        digits.push_back({ sum / n, knuckle, tip, refW });
    }
    std::sort(digits.begin(), digits.end(),
              [](const MeshDigit& a, const MeshDigit& b) { return a.across > b.across; });
    return digits;
}

std::vector<std::string> Complete::generateFingers(Model& m, const std::string& hand,
                                                   const FingerConfig& cfg) {
    std::vector<std::string> created;
    const Node* h = m.findNode(hand);
    if (!h) return created;

    // The hand box: X is its length, Z its width (as everywhere in this model).
    const double handLen   = h->body.size[0];
    const double handWidth = h->body.size[2];
    const Vec3   centre    = h->body.t.translation;

    // Point the digits away from the body: the hand sits distal to its parent,
    // so the parent-to-hand offset is the extension direction. Both hands get it
    // right without the caller having to mirror anything.
    Vec3 fwd { 1, 0, 0 };
    if (const Node* p = m.findNode(h->parent)) {
        Vec3 d { centre[0] - p->body.t.translation[0],
                 centre[1] - p->body.t.translation[1],
                 centre[2] - p->body.t.translation[2] };
        if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] > 1e-12) fwd = normalize(d);
    }
    const Vec3 spread { 0, 0, 1 };                     // across the hand's width
    Vec3 wrist = add(centre, mul(fwd, -0.5 * handLen));         // proximal edge
    double thickness = cfg.thickness * handWidth;

    // Measure the mesh when one is given: the bone's box does not describe the
    // hand it stands for, and the digits have to land in the fingers already
    // drawn there. `specs` mirrors cfg.digits but carries metres, not fractions.
    struct Digit { std::string name; int phalanges; double across, base, length, width; };
    std::vector<Digit> specs;
    std::vector<Vec3> pts;
    if (!cfg.mesh.empty() && loadObjPoints(cfg.mesh, cfg.meshScale, pts)) {
        std::vector<double> along, across;
        along.reserve(pts.size()); across.reserve(pts.size());
        for (const Vec3& p : pts) {
            along.push_back(p[0]*fwd[0] + p[1]*fwd[1] + p[2]*fwd[2]);
            across.push_back(p[0]*spread[0] + p[1]*spread[1] + p[2]*spread[2]);
        }
        double meshLen = *std::max_element(along.begin(), along.end())
                       - *std::min_element(along.begin(), along.end());
        auto found = measureDigits(along, across, 0.006);
        // Longest digits first in cfg.digits order minus the thumb: the mesh
        // lumps come sorted from the radial side, which is the thumb's side.
        std::vector<const FingerSpec*> table;
        for (const FingerSpec& d : cfg.digits)
            if (std::string(d.name) != "Thumb") table.push_back(&d);
        if (found.size() >= table.size()) {
            // the extra, most radial lump is the thumb when there is one
            size_t off = found.size() - table.size();
            if (off > 0) {
                const FingerSpec* th = nullptr;
                for (const FingerSpec& d : cfg.digits)
                    if (std::string(d.name) == "Thumb") th = &d;
                // The thumb branches off well before the knuckle line, so that
                // line is no base for it. Its tip is measured like any other
                // digit; its length comes from the table, scaled by the hand.
                if (th) {
                    double len = meshLen * th->length;
                    specs.push_back({ th->name, th->phalanges, found[0].across,
                                      found[0].tip - len, len, found[0].width });
                }
            }
            for (size_t i = 0; i < table.size(); i++) {
                const MeshDigit& g = found[i + off];
                double len = meshLen * table[i]->length;
                specs.push_back({ table[i]->name, table[i]->phalanges, g.across,
                                  g.tip - len, len, g.width });
            }
        }
    }
    if (specs.empty()) {   // no mesh (or unreadable): fall back to the table
        for (const FingerSpec& d : cfg.digits)
            specs.push_back({ d.name, d.phalanges,
                              (centre[2]*spread[2]) + d.lateral * handWidth,
                              (wrist[0]*fwd[0] + wrist[1]*fwd[1] + wrist[2]*fwd[2])
                                  + d.base * handLen,
                              d.length * handLen / 0.62, thickness });
    }

    for (const Digit& d : specs) {
        // `across` and `base` are already in the hand's frame, so rebuild the
        // world point from them rather than from fractions of the bone box
        Vec3 root { centre[0], centre[1], centre[2] };
        double c_along  = centre[0]*fwd[0] + centre[1]*fwd[1] + centre[2]*fwd[2];
        double c_across = centre[0]*spread[0] + centre[1]*spread[1] + centre[2]*spread[2];
        root = add(root, mul(fwd,    d.base   - c_along));
        root = add(root, mul(spread, d.across - c_across));
        // the thumb opposes rather than curls, so it hinges about another axis
        const Vec3 axis = (d.name == "Thumb") ? Vec3{ 0, 1, 0 } : spread;
        std::string parent = hand;
        // split the measured digit into phalanges that taper distally
        double share = 0;
        for (int k = 0; k < d.phalanges; k++) share += std::pow(cfg.taper, k);
        double len = d.length / share;
        Vec3 at = root;
        // Name as "<hand-without-side><Digit><n><side>" — "HandIndex1L". The side
        // marker has to be the last character: sim/Character.cpp pairs muscles
        // L/R and compares their bones ignoring exactly one trailing character,
        // so a marker anywhere else ("HandL_Index1") makes it reject the model.
        std::string stem = hand, side;
        if (!hand.empty() && (hand.back() == 'L' || hand.back() == 'R')) {
            side = hand.back();
            stem = hand.substr(0, hand.size() - 1);
        }
        for (int jp = 0; jp < d.phalanges; jp++) {
            char name[96];
            std::snprintf(name, sizeof(name), "%s%s%d%s", stem.c_str(), d.name.c_str(), jp + 1, side.c_str());
            Node n;
            n.id = name;
            n.parent = parent;
            n.joint.type = "Revolute";
            n.joint.axis = axis;
            n.joint.lower[0] = -cfg.flexLimit; n.joint.upper[0] = 0.0;
            n.joint.t.translation = at;
            n.body.type = "Box";
            n.body.size = { len, d.width, d.width };
            n.body.t.translation = add(at, mul(fwd, 0.5 * len));
            n.body.mass = cfg.density * len * d.width * d.width;
            m.skeleton.push_back(std::move(n));
            created.push_back(name);
            parent = name;
            at  = add(at, mul(fwd, len));      // next joint at this phalanx's tip
            len *= cfg.taper;                  // and each one shorter than the last
        }
    }
    return created;
}

std::vector<std::string> Complete::generateFingersSymmetric(Model& m, const std::string& hand,
                                                            const FingerConfig& cfg) {
    std::vector<std::string> created = generateFingers(m, hand, cfg);
    // counterpart hand name (…R<->…L); its own forward direction is derived, and
    // `lateral` is measured from the hand centre, so nothing needs mirroring here
    std::string other;
    if (!hand.empty() && hand.back() == 'R') other = hand.substr(0, hand.size()-1) + "L";
    else if (!hand.empty() && hand.back() == 'L') other = hand.substr(0, hand.size()-1) + "R";
    const Node* on = m.findNode(other);
    if (!other.empty() && on) {
        FingerConfig mc = cfg;
        // The measurements are world coordinates, so the other hand has to be
        // measured on its own mesh — handing it this one's would place its
        // digits on this side of the body. Swap in the counterpart's own OBJ,
        // keeping the directory the caller gave us.
        if (!cfg.mesh.empty() && !on->body.obj.empty()) {
            size_t slash = cfg.mesh.find_last_of("/\\");
            mc.mesh = (slash == std::string::npos) ? on->body.obj
                                                   : cfg.mesh.substr(0, slash + 1) + on->body.obj;
        }
        auto more = generateFingers(m, other, mc);
        created.insert(created.end(), more.begin(), more.end());
    }
    return created;
}

json Complete::listGaps(const Model& m, const Index& ix) {
    // small expectation table: these bones should articulate further
    static const std::pair<const char*, const char*> expect[] = {
        {"HandR", "fingers"}, {"HandL", "fingers"},
        {"FootR", "toes"},    {"FootL", "toes"},
    };
    json gaps = json::array();
    for (const auto& e : expect) {
        int s = ix.boneSlot(e.first);
        if (s < 0) continue;                         // bone not in model
        // a gap if no bone lists this one as parent
        bool hasChild = false;
        for (int b = 0; b < ix.boneCount(); b++)
            if (ix.parentOf(b) == s) { hasChild = true; break; }
        if (!hasChild) gaps.push_back({ {"bone", e.first}, {"expects", e.second} });
    }
    return gaps;
}

// "ShoulderL" -> "ShoulderR", "HandL_Middle3" -> "HandR_Middle3". The side marker
// is a trailing L/R on any underscore-separated segment, not just the last one —
// generated bones carry it in the middle. Midline bones (Pelvis, Torso, ...) and
// names whose counterpart is absent come back unchanged.
static std::string mirrorBody(const Model& m, const std::string& b) {
    for (size_t i = 0; i < b.size(); i++) {
        if (b[i] != 'L' && b[i] != 'R') continue;
        if (i + 1 != b.size() && b[i + 1] != '_') continue;   // must end a segment
        std::string other = b;
        other[i] = (b[i] == 'L') ? 'R' : 'L';
        if (m.findNode(other)) return other;
    }
    return b;
}

static Muscle muscleFromJson(const json& d, bool mirrored, const Model& m) {
    Muscle mu;
    mu.name = std::string(mirrored ? "R_" : "L_") + d.value("name", "");
    const json& h = d.value("hill", json::object());
    mu.f0        = h.value("f0", 100.0);
    mu.lm        = h.value("lm", 1.0);
    mu.lt        = h.value("lt", 0.1);
    mu.pen_angle = h.value("pen_angle", 0.0);
    mu.lmax      = h.value("lmax", -0.1);
    const json& a = d.value("anatomy", json::object());
    mu.latin      = a.value("latin", "");
    mu.pt         = a.value("pt", "");
    mu.group      = a.value("group", "");
    mu.antagonist = a.value("antagonist", "");
    mu.pcsa_cm2   = a.value("pcsa_cm2", 0.0);
    mu.side       = mirrored ? "R" : "L";
    for (const auto& w : d.value("waypoints", json::array())) {
        Waypoint wp;
        wp.body = w.value("body", "");
        auto p  = w.value("p", std::vector<double>{0, 0, 0});
        wp.p = { p.size() > 0 ? p[0] : 0.0, p.size() > 1 ? p[1] : 0.0, p.size() > 2 ? p[2] : 0.0 };
        if (mirrored) { wp.body = mirrorBody(m, wp.body); wp.p[0] = -wp.p[0]; }
        mu.waypoints.push_back(std::move(wp));
    }
    return mu;
}

json Complete::addMuscles(Model& m, const json& defs, bool mirror, bool dryRun) {
    json created = json::array(), skipped = json::array(), errors = json::array();
    const json& list = defs.is_array() ? defs : json::array({ defs });
    for (const auto& d : list) {
        std::string base = d.value("name", "");
        if (base.empty()) { errors.push_back("definition without a name"); continue; }
        if (d.value("waypoints", json::array()).size() < 2) {
            errors.push_back(base + ": needs at least two waypoints");
            continue;
        }
        for (int side = 0; side < (mirror ? 2 : 1); side++) {
            Muscle mu = muscleFromJson(d, side == 1, m);
            if (m.findMuscle(mu.name)) { skipped.push_back(mu.name); continue; }
            // a muscle anchored to one bone crosses no joint and is dead weight
            bool crosses = false;
            for (const auto& w : mu.waypoints)
                if (w.body != mu.waypoints.front().body) { crosses = true; break; }
            if (!crosses) { errors.push_back(mu.name + ": anchored to a single bone"); continue; }
            bool unknownBody = false;
            for (const auto& w : mu.waypoints)
                if (!m.findNode(w.body)) {
                    errors.push_back(mu.name + ": unknown bone " + w.body);
                    unknownBody = true;
                    break;
                }
            if (unknownBody) continue;
            created.push_back(mu.name);
            if (!dryRun) m.muscles.push_back(std::move(mu));
        }
    }
    return { {"created", created}, {"skipped", skipped}, {"errors", errors} };
}

// "L_Infraspinatus1" <-> "R_Infraspinatus1"; other names unchanged.
static std::string mirrorMuscleName(const std::string& n) {
    if (n.rfind("L_", 0) == 0) return "R_" + n.substr(2);
    if (n.rfind("R_", 0) == 0) return "L_" + n.substr(2);
    return n;
}

json Complete::reanchor(Model& m, const std::string& muscle, const std::string& from,
                        const std::string& to, int firstIndex, bool lastOnly, bool snap,
                        bool mirror, bool dryRun) {
    json changed = json::array();
    std::vector<std::pair<std::string, std::pair<std::string, std::string>>> jobs;
    jobs.push_back({ muscle, { from, to } });
    if (mirror) {
        std::string other = mirrorMuscleName(muscle);
        if (other != muscle)
            jobs.push_back({ other, { mirrorBody(m, from), mirrorBody(m, to) } });
    }
    for (const auto& job : jobs) {
        Muscle* mu = m.findMuscle(job.first);
        if (!mu) continue;
        const Node* dest = m.findNode(job.second.second);
        if (!dest || mu->waypoints.empty()) continue;
        int n = 0;
        int last = (int)mu->waypoints.size() - 1;
        for (int i = 0; i < (int)mu->waypoints.size(); i++) {
            if (lastOnly && i != last) continue;
            if (firstIndex >= 0 && i < firstIndex) continue;
            if (mu->waypoints[i].body != job.second.first) continue;
            if (!dryRun) {
                mu->waypoints[i].body = job.second.second;
                if (snap) mu->waypoints[i].p = dest->body.t.translation;
            }
            n++;
        }
        if (n) changed.push_back({ {"muscle", job.first}, {"waypoints", n} });
    }
    return { {"changed", changed} };
}

json Complete::listInertMuscles(const Model& m) {
    json out = json::array();
    for (const auto& mu : m.muscles) {
        if (mu.waypoints.empty()) continue;
        bool crosses = false;
        for (const auto& w : mu.waypoints)
            if (w.body != mu.waypoints.front().body) { crosses = true; break; }
        if (!crosses) out.push_back({ {"muscle", mu.name}, {"body", mu.waypoints.front().body} });
    }
    return out;
}

} // namespace mass
