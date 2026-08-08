#include "Mcp.h"
#include "Query.h"
#include "Batch.h"
#include "Kinematics.h"
#include "Complete.h"
#include "BindSkin.h"
#include <fstream>

namespace mass {
using json = nlohmann::json;

static Vec3 v3(const json& a, Vec3 def = {0,0,0}) {
    if (!a.is_array() || a.size() < 3) return def;
    return { a[0].get<double>(), a[1].get<double>(), a[2].get<double>() };
}
static json ok(const json& id, const json& result) {
    return { {"jsonrpc","2.0"}, {"id", id}, {"result", result} };
}
static json err(const json& id, int code, const std::string& msg) {
    return { {"jsonrpc","2.0"}, {"id", id}, {"error", { {"code", code}, {"message", msg} }} };
}

std::vector<std::string> McpServer::toolNames() {
    return { "describe_model", "get_node", "get_muscle", "select",
             "muscles_of_body", "muscles_crossing_joint",
             "scale_bone", "translate_subtree", "rotate_joint",
             "generate_fingers", "list_gaps", "list_inert_muscles", "add_muscles", "reanchor_waypoints",
             "load_atlas", "validate_anatomy", "sync_from_atlas",
             "bind_skin", "fit_bone", "set_body", "set_muscle",
             "save", "load" };
}

json McpServer::callTool(const std::string& name, const json& a, Model& m, Index& ix, bool& mutated) {
    mutated = false;
    // ---- read-only ----
    if (name == "describe_model") return Query::describeModel(m, ix);
    if (name == "get_node")       return Query::getNode(m, ix, a.value("key", ""));
    if (name == "get_muscle")     return Query::getMuscle(m, ix, a.value("key", ""));
    if (name == "select")         return Query::select(m, ix, a.value("expr", ""));
    if (name == "muscles_of_body") {
        json out = json::array();
        for (int mi : ix.musclesOfBone(a.value("bone", ""))) out.push_back(m.muscles[mi].name);
        return out;
    }
    if (name == "muscles_crossing_joint") {
        json out = json::array();
        for (int mi : ix.musclesCrossingJoint(a.value("childBone", ""))) out.push_back(m.muscles[mi].name);
        return out;
    }
    if (name == "list_gaps") return Complete::listGaps(m, ix);
    if (name == "list_inert_muscles") return Complete::listInertMuscles(m);
    if (name == "reanchor_waypoints") {
        bool dry = a.value("dryRun", false);
        json r = Complete::reanchor(m, a.value("muscle", ""), a.value("from", ""), a.value("to", ""),
                                    a.value("firstIndex", -1), a.value("lastOnly", false),
                                    a.value("snap", false), a.value("mirror", true), dry);
        mutated = !dry && !r["changed"].empty();
        return r;
    }
    if (name == "add_muscles") {
        // definitions inline via `defs`, or from a JSON file via `path`
        json defs = a.value("defs", json::array());
        if (a.contains("path")) {
            std::ifstream in(a.value("path", ""));
            if (!in) return { {"error", "cannot open " + a.value("path", std::string())} };
            try { in >> defs; }
            catch (const std::exception& e) { return { {"error", e.what()} }; }
            if (defs.is_object() && defs.contains("muscles")) defs = defs["muscles"];
        }
        bool dry = a.value("dryRun", false);
        json r = Complete::addMuscles(m, defs, a.value("mirror", true), dry);
        mutated = !dry && !r["created"].empty();
        if (mutated) m.assignUids();   // new muscles arrive without one
        return r;
    }

    // ---- mutating ----
    if (name == "scale_bone") {
        std::string bone = a.value("bone", "");
        Vec3 axis = v3(a.value("axis", json::array({0,1,0})));
        double f = a.value("factor", 1.0);
        int n = a.value("symmetric", false)
                    ? Batch::scaleBoneSymmetric(m, ix, bone, axis, f)
                    : (Batch::scaleBone(m, ix, bone, axis, f) ? 1 : 0);
        mutated = n > 0;
        return { {"scaled", n} };
    }
    if (name == "translate_subtree") {
        bool okk = Batch::translateSubtree(m, ix, a.value("bone", ""), v3(a.value("delta", json())));
        mutated = okk; return { {"ok", okk} };
    }
    if (name == "rotate_joint") {
        bool okk = Kinematics::rotateJoint(m, ix, a.value("bone", ""),
                                           v3(a.value("axis", json::array({0,0,1}))),
                                           a.value("angle", 0.0));
        mutated = okk; return { {"ok", okk} };
    }
    if (name == "generate_fingers") {
        // the digit table is anatomical by default; only the global knobs are
        // exposed, and `digits` can override the table wholesale
        FingerConfig cfg;
        cfg.taper     = a.value("taper", cfg.taper);
        cfg.thickness = a.value("thickness", cfg.thickness);
        cfg.density   = a.value("density", cfg.density);
        cfg.flexLimit = a.value("flexLimit", cfg.flexLimit);
        std::vector<FingerSpec> table;
        std::vector<std::string> names;   // backing storage for FingerSpec::name
        if (a.contains("digits")) {
            for (const auto& d : a["digits"]) names.push_back(d.value("name", "Digit"));
            int i = 0;
            for (const auto& d : a["digits"]) {
                table.push_back({ names[i++].c_str(), d.value("phalanges", 3),
                                  d.value("lateral", 0.0), d.value("base", 1.0),
                                  d.value("length", 0.3) });
            }
            cfg.digits = table;
        }
        std::vector<std::string> created = a.value("symmetric", false)
            ? Complete::generateFingersSymmetric(m, a.value("hand", ""), cfg)
            : Complete::generateFingers(m, a.value("hand", ""), cfg);
        mutated = !created.empty();
        return { {"created", created} };
    }

    // ---- skin ----
    if (name == "bind_skin") {
        std::string e;
        json r = BindSkin::bind(m, a.value("obj", ""),
                                v3(a.value("rot", json::array({0,0,0}))),
                                a.value("scale", 1.0),
                                v3(a.value("offset", json::array({0,0,0}))),
                                a.value("fit", true), &e);
        if (r.is_null()) return { {"ok", false}, {"error", e} };
        mutated = true;   // skin descriptor set + (if fit) skeleton morphed
        return r;
    }
    if (name == "fit_bone") {
        std::string e;
        json r = BindSkin::fitBone(m, a.value("bone", ""), a.value("margin", 1.05), &e);
        if (r.is_null()) return { {"ok", false}, {"error", e} };
        mutated = true;   // bone box resized (+ its mirror)
        return r;
    }
    if (name == "set_body") {
        Node* n = m.findNode(a.value("bone", ""));
        if (!n) return { {"ok", false}, {"error", "no bone"} };
        if (a.contains("size")) n->body.size = v3(a["size"]);
        if (a.contains("mass")) n->body.mass = a["mass"].get<double>();
        mutated = true;
        return { {"ok", true}, {"bone", n->id}, {"size", { n->body.size[0], n->body.size[1], n->body.size[2] }} };
    }

    if (name == "set_muscle") {
        // patch Hill params / anatomy metadata on one muscle (and, with mirror,
        // its L/R twin). Only the fields present in the request are touched.
        std::vector<std::string> targets{ a.value("name", std::string()) };
        if (a.value("mirror", true)) {
            const std::string& n0 = targets[0];
            if (n0.rfind("L_", 0) == 0) targets.push_back("R_" + n0.substr(2));
            else if (n0.rfind("R_", 0) == 0) targets.push_back("L_" + n0.substr(2));
        }
        json patched = json::array();
        for (const auto& t : targets) {
            Muscle* mu = m.findMuscle(t);
            if (!mu) continue;
            const json& h = a.value("hill", json::object());
            if (h.contains("f0"))        mu->f0        = h["f0"].get<double>();
            if (h.contains("lm"))        mu->lm        = h["lm"].get<double>();
            if (h.contains("lt"))        mu->lt        = h["lt"].get<double>();
            if (h.contains("pen_angle")) mu->pen_angle = h["pen_angle"].get<double>();
            if (h.contains("lmax"))      mu->lmax      = h["lmax"].get<double>();
            const json& an = a.value("anatomy", json::object());
            if (an.contains("latin"))      mu->latin      = an["latin"].get<std::string>();
            if (an.contains("pt"))         mu->pt         = an["pt"].get<std::string>();
            if (an.contains("group"))      mu->group      = an["group"].get<std::string>();
            if (an.contains("antagonist")) mu->antagonist = an["antagonist"].get<std::string>();
            if (an.contains("pcsa_cm2"))   mu->pcsa_cm2   = an["pcsa_cm2"].get<double>();
            patched.push_back(t);
        }
        mutated = !patched.empty();
        return { {"patched", patched} };
    }

    // ---- atlas ----
    if (name == "load_atlas") {
        std::string e;
        // `path` loads one .osim (call repeatedly to stack lower limb + arm +
        // wrist); `synonyms` loads the name join table instead.
        if (a.contains("synonyms")) {
            bool ok = mAtlas.loadSynonyms(a.value("synonyms", ""), &e);
            return { {"ok", ok}, {"synonyms", (int)mAtlas.synonymCount()}, {"error", e} };
        }
        mAtlasLoaded = mAtlas.loadOsim(a.value("path", ""), &e) || mAtlasLoaded;
        return { {"ok", mAtlasLoaded}, {"muscles", (int)mAtlas.size()},
                 {"synonyms", (int)mAtlas.synonymCount()}, {"error", e} };
    }
    if (name == "validate_anatomy") {
        if (!mAtlasLoaded) return { {"error", "no atlas loaded"} };
        return Atlas::validate(m, ix, mAtlas);
    }
    if (name == "sync_from_atlas") {
        if (!mAtlasLoaded) return { {"error", "no atlas loaded"} };
        int changed = Atlas::sync(m, mAtlas, a.value("fillHill", false), a.value("dryRun", false),
                                  a.value("fillLengths", false));
        mutated = changed > 0 && !a.value("dryRun", false);
        return { {"changed", changed} };
    }

    // ---- io ----
    if (name == "save") {
        std::string e; bool okk = m.SaveMass(a.value("path", ""), &e);
        return { {"ok", okk}, {"error", e} };
    }
    if (name == "load") {
        std::string e; auto nm = Model::LoadMass(a.value("path", ""), &e);
        if (!nm) return { {"ok", false}, {"error", e} };
        m = std::move(*nm); m.assignUids(); mutated = true;
        return { {"ok", true}, {"bones", (int)m.skeleton.size()}, {"muscles", (int)m.muscles.size()} };
    }

    return json();  // unknown tool sentinel
}

json McpServer::handle(const json& req, Model& m, Index& ix) {
    json id = req.contains("id") ? req["id"] : json();
    std::string method = req.value("method", "");

    if (method == "initialize") {
        return ok(id, { {"protocolVersion", "2024-11-05"},
                        {"capabilities", { {"tools", json::object()} }},
                        {"serverInfo", { {"name", "mass-mcp"}, {"version", "0.1"} }} });
    }
    if (method == "tools/list") {
        json tools = json::array();
        for (const auto& n : McpServer::toolNames()) tools.push_back({ {"name", n} });
        return ok(id, { {"tools", tools} });
    }
    if (method == "tools/call") {
        const json& params = req.value("params", json::object());
        std::string name = params.value("name", "");
        json args = params.value("arguments", json::object());
        bool mutated = false;
        json result = callTool(name, args, m, ix, mutated);
        if (result.is_null()) return err(id, -32601, "unknown tool: " + name);
        if (mutated) ix.build(m);   // keep the index consistent after structural/geometry edits
        return ok(id, { {"content", json::array({ { {"type","json"}, {"json", result} } })} });
    }
    return err(id, -32601, "unknown method: " + method);
}

// ---- McpQueue ----
std::future<json> McpQueue::submit(json request) {
    std::lock_guard<std::mutex> lk(mMx);
    mQ.push_back(Item{ std::move(request), {} });
    return mQ.back().resp.get_future();
}
int McpQueue::drain(McpServer& server, Model& m, Index& ix) {
    std::deque<Item> local;
    { std::lock_guard<std::mutex> lk(mMx); local.swap(mQ); }
    int n = 0;
    for (auto& it : local) { it.resp.set_value(server.handle(it.req, m, ix)); ++n; }
    return n;
}
size_t McpQueue::pending() { std::lock_guard<std::mutex> lk(mMx); return mQ.size(); }

} // namespace mass
