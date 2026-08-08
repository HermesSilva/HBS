#include "Complete.h"
#include "Kinematics.h"   // normalize
#include <cstdio>

namespace mass {

using json = nlohmann::json;

static Vec3 add(const Vec3& a, const Vec3& b) { return { a[0]+b[0], a[1]+b[1], a[2]+b[2] }; }
static Vec3 mul(const Vec3& a, double s) { return { a[0]*s, a[1]*s, a[2]*s }; }

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
    const Vec3 wrist = add(centre, mul(fwd, -0.5 * handLen));   // proximal edge
    const double thickness = cfg.thickness * handWidth;

    for (const FingerSpec& d : cfg.digits) {
        // start at `base` along the hand, offset sideways by `lateral`
        Vec3 root = add(add(wrist, mul(fwd, d.base * handLen)),
                        mul(spread, d.lateral * handWidth));
        // the thumb opposes rather than curls, so it hinges about another axis
        const Vec3 axis = (std::string(d.name) == "Thumb") ? Vec3{ 0, 1, 0 } : spread;
        std::string parent = hand;
        double len = d.length * handLen;
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
            std::snprintf(name, sizeof(name), "%s%s%d%s", stem.c_str(), d.name, jp + 1, side.c_str());
            Node n;
            n.id = name;
            n.parent = parent;
            n.joint.type = "Revolute";
            n.joint.axis = axis;
            n.joint.lower[0] = -cfg.flexLimit; n.joint.upper[0] = 0.0;
            n.joint.t.translation = at;
            n.body.type = "Box";
            n.body.size = { len, thickness, thickness };
            n.body.t.translation = add(at, mul(fwd, 0.5 * len));
            n.body.mass = cfg.density * len * thickness * thickness;
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
    if (!other.empty() && m.findNode(other)) {
        auto more = generateFingers(m, other, cfg);
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
