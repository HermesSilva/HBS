#include "Skeleton.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <map>
#include <set>

namespace mass {
using json = nlohmann::json;

static const double M_TO_CM = 100.0;
static const double BONE_DENSITY = 1900.0;   // kg/m3, cortical+trabecular average
static const double BONE_FRACTION = 0.15;    // of body mass

// --------------------------------------------------------------- profile ----
BodyProfile BodyProfile::fromJson(const json& j) {
    BodyProfile p;
    if (j.contains("name")) p.name = j["name"].get<std::string>();
    if (j.contains("sex")) p.sex = j["sex"].get<std::string>();
    if (j.contains("height_m")) p.height_m = j["height_m"].get<double>();
    if (j.contains("mass_kg")) p.mass_kg = j["mass_kg"].get<double>();
    if (j.contains("specific_tension_N_cm2"))
        p.specific_tension = j["specific_tension_N_cm2"].get<double>();
    return p;
}

BodyProfile BodyProfile::fromFile(const std::string& path, std::string* err) {
    std::ifstream f(path);
    if (!f) { if (err) *err = "cannot open " + path; return BodyProfile(); }
    try {
        json j; f >> j;
        return fromJson(j);
    } catch (const std::exception& e) {
        if (err) *err = e.what();
        return BodyProfile();
    }
}

// --------------------------------------------------------------- anatomy ----
static const char* ORDINALS[12] = {
    "first", "second", "third", "fourth", "fifth", "sixth",
    "seventh", "eighth", "ninth", "tenth", "eleventh", "twelfth" };
static const char* FINGERS[4] = { "index", "middle", "ring", "little" };
static const char* TOES[4] = { "second", "third", "fourth", "little" };

std::vector<std::pair<std::string, std::vector<std::string>>> Skeleton::fusedMeshes() {
    return { { "skull", {
        "frontal_bone", "occipital_bone", "sphenoid_bone", "ethmoid", "vomer",
        "left_parietal_bone", "right_parietal_bone",
        "left_temporal_bone", "right_temporal_bone",
        "left_zygomatic_bone", "right_zygomatic_bone",
        "left_maxilla", "right_maxilla",
        "left_nasal_bone", "right_nasal_bone",
        "left_lacrimal_bone", "right_lacrimal_bone",
        "left_palatine_bone", "right_palatine_bone" } } };
}

std::vector<BoneSpec> Skeleton::anatomy() {
    std::vector<BoneSpec> t;
    auto add = [&](std::string b, std::string p, std::string j, double l) {
        t.push_back({ std::move(b), std::move(p), std::move(j), l });
    };

    add("sacrum", "", "Free", 0.0);
    // the hip bones meet the sacrum at the sacroiliac joints: synovial, but
    // they barely move
    for (const char* side : { "right", "left" })
        add(std::string(side) + "_hip_bone", "sacrum", "Ball", 0.05);

    // The column, sacrum upwards. Each level moves a little; the column moves
    // a lot, which is what makes a trunk a trunk.
    std::string prev = "sacrum";
    for (int i = 5; i >= 1; i--) {
        std::string v = std::string(ORDINALS[i - 1]) + "_lumbar_vertebra";
        add(v, prev, "Ball", 0.12); prev = v;
    }
    for (int i = 12; i >= 1; i--) {
        std::string v = std::string(ORDINALS[i - 1]) + "_thoracic_vertebra";
        add(v, prev, "Ball", 0.06); prev = v;
    }
    for (int i = 7; i >= 3; i--) {
        std::string v = std::string(ORDINALS[i - 1]) + "_cervical_vertebra";
        add(v, prev, "Ball", 0.15); prev = v;
    }
    add("axis", prev, "Ball", 0.15);
    add("atlas", "axis", "Ball", 0.30);       // atlantoaxial: rotation
    add("skull", "atlas", "Ball", 0.40);      // atlanto-occipital: nodding
    add("mandible", "skull", "Ball", 0.50);   // temporomandibular
    add("hyoid_bone", "skull", "Ball", 0.20);

    // ribs, each on its own thoracic vertebra, with enough travel to breathe
    for (int i = 0; i < 12; i++)
        for (const char* side : { "right", "left" })
            add(std::string(side) + "_" + ORDINALS[i] + "_rib",
                std::string(ORDINALS[i]) + "_thoracic_vertebra", "Ball", 0.10);
    add("manubrium", "right_first_rib", "Ball", 0.05);
    add("body_of_sternum", "manubrium", "Ball", 0.03);
    add("xiphoid_process", "body_of_sternum", "Ball", 0.03);

    for (const char* s : { "right", "left" }) {
        const std::string side(s);
        auto S = [&](const char* n) { return side + "_" + n; };

        // shoulder girdle — this is what lets an arm work overhead
        add(S("clavicle"), "manubrium", "Ball", 0.50);
        add(S("scapula"), S("clavicle"), "Ball", 0.40);
        add(S("humerus"), S("scapula"), "Ball", 1.60);
        add(S("ulna"), S("humerus"), "Revolute", 2.40);
        add(S("radius"), S("ulna"), "Revolute", 2.60);      // pronation

        // carpus: proximal row off the forearm, then the distal row
        add(S("scaphoid"), S("radius"), "Ball", 0.35);
        add(S("lunate"), S("radius"), "Ball", 0.35);
        add(S("triquetral"), S("lunate"), "Ball", 0.20);
        add(S("pisiform"), S("triquetral"), "Ball", 0.15);
        add(S("trapezium"), S("scaphoid"), "Ball", 0.20);
        add(S("trapezoid"), S("scaphoid"), "Ball", 0.15);
        add(S("capitate"), S("scaphoid"), "Ball", 0.20);
        add(S("hamate"), S("triquetral"), "Ball", 0.20);

        // carpometacarpal: the thumb's saddle is mobile, 2 and 3 are not
        struct MC { const char* ord; std::string parent; double lim; };
        const MC mcs[5] = {
            { "first",  S("trapezium"), 0.90 }, { "second", S("trapezoid"), 0.10 },
            { "third",  S("capitate"),  0.10 }, { "fourth", S("hamate"),    0.25 },
            { "fifth",  S("hamate"),    0.35 } };
        for (const MC& mc : mcs)
            add(side + "_" + mc.ord + "_metacarpal_bone", mc.parent, "Ball", mc.lim);

        add("proximal_phalanx_of_" + side + "_thumb", side + "_first_metacarpal_bone",
            "Ball", 1.00);
        add("distal_phalanx_of_" + side + "_thumb", "proximal_phalanx_of_" + side + "_thumb",
            "Revolute", 1.40);
        for (int i = 0; i < 4; i++) {
            const std::string f(FINGERS[i]);
            add("proximal_phalanx_of_" + side + "_" + f + "_finger",
                side + "_" + ORDINALS[i + 1] + "_metacarpal_bone", "Ball", 1.60);
            add("middle_phalanx_of_" + side + "_" + f + "_finger",
                "proximal_phalanx_of_" + side + "_" + f + "_finger", "Revolute", 1.90);
            add("distal_phalanx_of_" + side + "_" + f + "_finger",
                "middle_phalanx_of_" + side + "_" + f + "_finger", "Revolute", 1.50);
        }

        // lower limb
        add(S("femur"), S("hip_bone"), "Ball", 1.60);
        add(S("patella"), S("femur"), "Revolute", 1.20);
        add(S("tibia"), S("femur"), "Revolute", 2.40);
        add(S("fibula"), S("tibia"), "Ball", 0.05);
        add(S("talus"), S("tibia"), "Revolute", 0.90);        // ankle
        add(S("calcaneus"), S("talus"), "Revolute", 0.50);    // subtalar
        add("navicular_bone_of_" + side + "_foot", S("talus"), "Ball", 0.15);
        add(S("cuboid_bone"), S("calcaneus"), "Ball", 0.15);
        for (const char* c : { "medial", "intermediate", "lateral" })
            add(side + "_" + c + "_cuneiform_bone",
                "navicular_bone_of_" + side + "_foot", "Ball", 0.10);
        struct MT { const char* ord; std::string parent; };
        const MT mts[5] = {
            { "first",  S("medial_cuneiform_bone") },
            { "second", S("intermediate_cuneiform_bone") },
            { "third",  S("lateral_cuneiform_bone") },
            { "fourth", S("cuboid_bone") }, { "fifth", S("cuboid_bone") } };
        for (const MT& mt : mts)
            add(side + "_" + mt.ord + "_metatarsal_bone", mt.parent, "Ball", 0.10);

        add("proximal_phalanx_of_" + side + "_big_toe",
            side + "_first_metatarsal_bone", "Ball", 0.80);
        add("distal_phalanx_of_" + side + "_big_toe",
            "proximal_phalanx_of_" + side + "_big_toe", "Revolute", 0.70);
        for (int i = 0; i < 4; i++) {
            const std::string toe(TOES[i]);
            add("proximal_phalanx_of_" + side + "_" + toe + "_toe",
                side + "_" + ORDINALS[i + 1] + "_metatarsal_bone", "Ball", 0.80);
            add("middle_phalanx_of_" + side + "_" + toe + "_toe",
                "proximal_phalanx_of_" + side + "_" + toe + "_toe", "Revolute", 0.70);
            add("distal_phalanx_of_" + side + "_" + toe + "_toe",
                "middle_phalanx_of_" + side + "_" + toe + "_toe", "Revolute", 0.60);
        }
    }
    return t;
}

// ----------------------------------------------------------------- names ----
namespace {

struct Named { const char* stem; const char* latin; const char* pt; };
const Named SIMPLE[] = {
    { "sacrum", "Os sacrum", "Sacro" },
    { "hip_bone", "Os coxae", "Osso do quadril" },
    { "skull", "Cranium", "Crânio" },
    { "mandible", "Mandibula", "Mandíbula" },
    { "hyoid_bone", "Os hyoideum", "Hioide" },
    { "atlas", "Atlas", "Atlas" },
    { "axis", "Axis", "Áxis" },
    { "manubrium", "Manubrium sterni", "Manúbrio do esterno" },
    { "body_of_sternum", "Corpus sterni", "Corpo do esterno" },
    { "xiphoid_process", "Processus xiphoideus", "Processo xifoide" },
    { "clavicle", "Clavicula", "Clavícula" },
    { "scapula", "Scapula", "Escápula" },
    { "humerus", "Humerus", "Úmero" },
    { "ulna", "Ulna", "Ulna" },
    { "radius", "Radius", "Rádio" },
    { "femur", "Os femoris", "Fêmur" },
    { "patella", "Patella", "Patela" },
    { "tibia", "Tibia", "Tíbia" },
    { "fibula", "Fibula", "Fíbula" },
    { "talus", "Talus", "Tálus" },
    { "calcaneus", "Calcaneus", "Calcâneo" },
    { "cuboid_bone", "Os cuboideum", "Cuboide" },
    { "navicular_bone", "Os naviculare", "Navicular" },
    { "scaphoid", "Os scaphoideum", "Escafoide" },
    { "lunate", "Os lunatum", "Semilunar" },
    { "triquetral", "Os triquetrum", "Piramidal" },
    { "pisiform", "Os pisiforme", "Pisiforme" },
    { "trapezium", "Os trapezium", "Trapézio" },
    { "trapezoid", "Os trapezoideum", "Trapezoide" },
    { "capitate", "Os capitatum", "Capitato" },
    { "hamate", "Os hamatum", "Hamato" },
};

const char* ORD_M[12] = { "1º","2º","3º","4º","5º","6º","7º","8º","9º","10º","11º","12º" };
const char* ORD_F[12] = { "1ª","2ª","3ª","4ª","5ª","6ª","7ª","8ª","9ª","10ª","11ª","12ª" };

bool starts(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}
bool ends(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}

// Portuguese agrees in gender; the side agrees with the head of the phrase, so
// a phalanx is "do indicador esquerdo" and not "esquerda".
std::string sideWord(const std::string& head, const std::string& side) {
    if (side.empty()) return "";
    static const char* FEM[] = { "costela", "escápula", "clavícula", "vértebra",
                                 "falange", "patela", "tíbia", "fíbula", "ulna",
                                 "mandíbula" };
    bool feminine = false;
    for (const char* f : FEM)
        if (head.find(f) != std::string::npos) { feminine = true; break; }
    if (!head.empty() && head.size() > 1 &&
        head.compare(head.size() - 2, 2, "ª") == 0) feminine = true;
    if (side == "right") return feminine ? " direita" : " direito";
    return feminine ? " esquerda" : " esquerdo";
}

int ordinalIndex(const std::string& word) {
    for (int i = 0; i < 12; i++) if (word == ORDINALS[i]) return i;
    return -1;
}

}  // namespace

void Skeleton::names(const std::string& bone, std::string* latin, std::string* pt) {
    std::string side, stem = bone;
    for (const char* s : { "right", "left" }) {
        const std::string sp = std::string(s) + "_";
        if (starts(bone, sp)) { side = s; stem = bone.substr(sp.size()); break; }
        if (ends(bone, "_" + std::string(s) + "_foot")) { side = s; stem = "navicular_bone"; break; }
    }

    for (const Named& n : SIMPLE) {
        if (stem == n.stem) {
            *latin = n.latin;
            *pt = std::string(n.pt) + sideWord(n.pt, side);
            return;
        }
    }

    // vertebrae: "3ª vértebra lombar"
    {
        const size_t us = bone.find('_');
        if (us != std::string::npos) {
            int oi = ordinalIndex(bone.substr(0, us));
            if (oi >= 0) {
                if (bone.find("cervical") != std::string::npos) {
                    *latin = "Vertebra cervicalis";
                    *pt = std::string(ORD_F[oi]) + " vértebra cervical"; return;
                }
                if (bone.find("thoracic") != std::string::npos) {
                    *latin = "Vertebra thoracica";
                    *pt = std::string(ORD_F[oi]) + " vértebra torácica"; return;
                }
                if (bone.find("lumbar") != std::string::npos) {
                    *latin = "Vertebra lumbalis";
                    *pt = std::string(ORD_F[oi]) + " vértebra lombar"; return;
                }
            }
        }
    }

    // ribs, metacarpals, metatarsals, cuneiforms
    {
        const size_t us = stem.find('_');
        if (us != std::string::npos) {
            const int oi = ordinalIndex(stem.substr(0, us));
            const std::string rest = stem.substr(us + 1);
            if (oi >= 0) {
                if (rest == "rib") {
                    *latin = "Costa";
                    *pt = std::string(ORD_F[oi]) + " costela" + sideWord("costela", side);
                    return;
                }
                if (rest == "metacarpal_bone") {
                    *latin = "Os metacarpi";
                    *pt = std::string(ORD_M[oi]) + " metacarpo" + sideWord("metacarpo", side);
                    return;
                }
                if (rest == "metatarsal_bone") {
                    *latin = "Os metatarsi";
                    *pt = std::string(ORD_M[oi]) + " metatarso" + sideWord("metatarso", side);
                    return;
                }
            }
            if (ends(stem, "_cuneiform_bone")) {
                const std::string which = stem.substr(0, stem.size() - 15);
                const std::string ptw = which == "medial" ? "medial"
                                      : which == "intermediate" ? "intermédio" : "lateral";
                *latin = "Os cuneiforme " + which;
                *pt = "Cuneiforme " + ptw + sideWord("cuneiforme", side);
                return;
            }
        }
    }

    // phalanges: "Falange distal do hálux direito"
    for (const char* kind : { "proximal", "middle", "distal" }) {
        const std::string prefix = std::string(kind) + "_phalanx_of_";
        if (!starts(bone, prefix)) continue;
        std::string rest = bone.substr(prefix.size()), sd;
        for (const char* s : { "right", "left" }) {
            const std::string sp = std::string(s) + "_";
            if (starts(rest, sp)) { sd = s; rest = rest.substr(sp.size()); break; }
        }
        const bool isToe = ends(rest, "_toe");
        std::string digit = rest;
        for (const char* suffix : { "_finger", "_toe" })
            if (ends(digit, suffix)) digit = digit.substr(0, digit.size() - strlen(suffix));

        std::string noun;
        if (isToe) {
            if (digit == "big") noun = "hálux";
            else {
                static const std::map<std::string, const char*> TOE_ORD = {
                    { "second", "2º" }, { "third", "3º" },
                    { "fourth", "4º" }, { "little", "5º" } };
                auto it = TOE_ORD.find(digit);
                noun = std::string(it != TOE_ORD.end() ? it->second : digit.c_str())
                     + " dedo do pé";
            }
        } else {
            static const std::map<std::string, const char*> FIN = {
                { "index", "indicador" }, { "middle", "médio" },
                { "ring", "anelar" }, { "little", "mínimo" }, { "thumb", "polegar" } };
            auto it = FIN.find(digit);
            noun = it != FIN.end() ? it->second : digit;
        }
        const std::string kpt = std::string(kind) == "proximal" ? "proximal"
                              : std::string(kind) == "middle" ? "média" : "distal";
        *latin = std::string("Phalanx ") + kind;
        *pt = "Falange " + kpt + " do " + noun + sideWord(noun, sd);
        return;
    }

    *latin = "";
    *pt = bone;
    for (char& c : *pt) if (c == '_') c = ' ';
}

// ------------------------------------------------------------------ mesh ----
namespace {

struct Mesh {
    std::vector<Vec3> v, vn;
    std::vector<std::string> f;      // faces, verbatim, renumbered on merge
};

bool readObj(const std::string& path, Mesh& out) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.size() < 3) continue;
        if (line[0] == 'v' && line[1] == ' ') {
            std::istringstream ss(line.substr(2));
            Vec3 p{}; ss >> p[0] >> p[1] >> p[2];
            out.v.push_back(p);
        } else if (line[0] == 'v' && line[1] == 'n') {
            std::istringstream ss(line.substr(3));
            Vec3 n{}; ss >> n[0] >> n[1] >> n[2];
            out.vn.push_back(n);
        } else if (line[0] == 'f' && line[1] == ' ') {
            out.f.push_back(line);
        }
    }
    return !out.v.empty();
}

// Merge meshes into one placed OBJ, scaled and lifted; bounds come back in metres.
bool writePlaced(const std::vector<Mesh>& parts, const std::string& out,
                 double scale, double liftCm, Vec3* lo, Vec3* hi) {
    std::ofstream f(out);
    if (!f) return false;
    *lo = { 1e9, 1e9, 1e9 };
    *hi = { -1e9, -1e9, -1e9 };
    size_t vbase = 0, nbase = 0;
    std::vector<std::string> faces;
    std::ostringstream verts, norms;
    for (const Mesh& m : parts) {
        for (const Vec3& p : m.v) {
            Vec3 w{ p[0] * scale, p[1] * scale + liftCm, p[2] * scale };
            for (int i = 0; i < 3; i++) {
                (*lo)[i] = std::min((*lo)[i], w[i] / M_TO_CM);
                (*hi)[i] = std::max((*hi)[i], w[i] / M_TO_CM);
            }
            char buf[96];
            std::snprintf(buf, sizeof(buf), "v %.4f %.4f %.4f\n", w[0], w[1], w[2]);
            verts << buf;
        }
        for (const Vec3& n : m.vn) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "vn %.6f %.6f %.6f\n", n[0], n[1], n[2]);
            norms << buf;
        }
        for (const std::string& line : m.f) {
            std::istringstream ss(line.substr(2));
            std::string tok, outLine = "f";
            while (ss >> tok) {
                const size_t slash = tok.find("//");
                if (slash == std::string::npos) {
                    outLine += " " + std::to_string(std::atoi(tok.c_str()) + (int)vbase);
                } else {
                    const int vi = std::atoi(tok.substr(0, slash).c_str()) + (int)vbase;
                    const int ni = std::atoi(tok.substr(slash + 2).c_str()) + (int)nbase;
                    outLine += " " + std::to_string(vi) + "//" + std::to_string(ni);
                }
            }
            faces.push_back(outLine);
        }
        vbase += m.v.size();
        nbase += m.vn.size();
    }
    f << verts.str() << norms.str();
    for (const std::string& line : faces) f << line << "\n";
    return (*lo)[0] < (*hi)[0];
}

// Midpoint of the closest pair between two meshes: where they articulate.
Vec3 interfacePoint(const std::vector<Vec3>& a, const std::vector<Vec3>& b) {
    const size_t sa = std::max<size_t>(1, a.size() / 300);
    const size_t sb = std::max<size_t>(1, b.size() / 300);
    double best = 1e18;
    Vec3 pa = a.empty() ? Vec3{} : a[0], pb = b.empty() ? Vec3{} : b[0];
    for (size_t i = 0; i < a.size(); i += sa)
        for (size_t k = 0; k < b.size(); k += sb) {
            const double dx = a[i][0] - b[k][0], dy = a[i][1] - b[k][1], dz = a[i][2] - b[k][2];
            const double d = dx * dx + dy * dy + dz * dz;
            if (d < best) { best = d; pa = a[i]; pb = b[k]; }
        }
    return { (pa[0] + pb[0]) / 2, (pa[1] + pb[1]) / 2, (pa[2] + pb[2]) / 2 };
}

}  // namespace

// ----------------------------------------------------------------- build ----
json Skeleton::build(Model& m, const std::string& bonedir,
                     const BodyProfile& profile, std::string* err) {
    const std::vector<BoneSpec> spec = anatomy();
    std::map<std::string, std::vector<std::string>> fused;
    for (auto& kv : fusedMeshes()) fused[kv.first] = kv.second;

    // load every bone's source meshes, in the centimetres bp3d_prepare produced
    std::map<std::string, std::vector<Mesh>> raw;
    std::map<std::string, std::vector<Vec3>> pts;
    for (const BoneSpec& b : spec) {
        std::vector<std::string> files;
        auto fu = fused.find(b.bone);
        if (fu != fused.end())
            for (const std::string& mesh : fu->second) files.push_back(bonedir + "/" + mesh + ".obj");
        else
            files.push_back(bonedir + "/" + b.bone + ".obj");

        std::vector<Mesh> loaded;
        std::vector<Vec3> cloud;
        for (const std::string& p : files) {
            Mesh mesh;
            if (!readObj(p, mesh)) continue;
            cloud.insert(cloud.end(), mesh.v.begin(), mesh.v.end());
            loaded.push_back(std::move(mesh));
        }
        if (loaded.empty()) continue;
        raw[b.bone] = std::move(loaded);
        pts[b.bone] = std::move(cloud);
    }
    if (raw.empty()) {
        if (err) *err = "no bone meshes found in " + bonedir;
        return json();
    }

    // scale to the profile's height, then stand the body on the floor
    double lo = 1e9, hi = -1e9;
    for (auto& kv : pts)
        for (const Vec3& p : kv.second) { lo = std::min(lo, p[1]); hi = std::max(hi, p[1]); }
    const double builtH = (hi - lo) / M_TO_CM;
    const double scale = builtH > 1e-6 ? profile.height_m / builtH : 1.0;
    const double liftCm = -lo * scale;

    m.skeleton.clear();
    m.muscles.clear();
    m.meta.name = "HBS skeleton - " + profile.name;
    m.meta.specific_tension_N_cm2 = profile.specific_tension;

    for (const BoneSpec& b : spec) {
        auto it = raw.find(b.bone);
        if (it == raw.end()) continue;
        Vec3 blo{}, bhi{};
        const std::string file = "_" + b.bone + ".obj";
        if (!writePlaced(it->second, bonedir + "/" + file, scale, liftCm, &blo, &bhi))
            continue;

        Node n;
        n.id = b.bone;
        n.parent = b.parent;
        n.latin.clear();
        Skeleton::names(b.bone, &n.latin, &n.pt);

        n.body.type = "Box";
        n.body.obj = "../atlas/bp3d/" + file;
        for (int i = 0; i < 3; i++) n.body.size[i] = std::max(1e-4, bhi[i] - blo[i]);
        n.body.t.translation = { (blo[0] + bhi[0]) / 2, (blo[1] + bhi[1]) / 2,
                                 (blo[2] + bhi[2]) / 2 };
        n.body.mass = BONE_DENSITY * n.body.size[0] * n.body.size[1] * n.body.size[2] * 0.35;
        n.body.contact = b.bone.find("calcaneus") != std::string::npos
                      || (b.bone.find("phalanx") != std::string::npos
                          && b.bone.find("toe") != std::string::npos);

        n.joint.type = b.joint;
        auto parentPts = pts.find(b.parent);
        if (!b.parent.empty() && parentPts != pts.end()) {
            const Vec3 mid = interfacePoint(pts[b.bone], parentPts->second);
            n.joint.t.translation = { mid[0] * scale / M_TO_CM,
                                      (mid[1] * scale + liftCm) / M_TO_CM,
                                      mid[2] * scale / M_TO_CM };
        } else {
            n.joint.t.translation = n.body.t.translation;
        }
        if (b.joint == "Revolute") {
            n.joint.axis = { 0, 0, 1 };
            n.joint.lower = { -b.limit, 0, 0 };
            n.joint.upper = { 0, 0, 0 };
        } else if (b.joint == "Ball") {
            n.joint.lower = { -b.limit, -b.limit, -b.limit };
            n.joint.upper = { b.limit, b.limit, b.limit };
        }
        m.skeleton.push_back(std::move(n));
    }

    // bone is ~15% of body mass; scale the density estimates onto that
    double got = 0;
    for (const Node& n : m.skeleton) got += n.body.mass;
    if (got > 1e-9) {
        const double k = profile.mass_kg * BONE_FRACTION / got;
        for (Node& n : m.skeleton) n.body.mass *= k;
    }
    m.assignUids();
    return check(m, bonedir);
}

// ----------------------------------------------------------------- check ----
json Skeleton::check(const Model& m, const std::string& bonedir) {
    (void)bonedir;
    json problems = json::array();
    std::map<std::string, const Node*> byId;
    for (const Node& n : m.skeleton) byId[n.id] = &n;

    double floor = 1e9, top = -1e9, massSum = 0;
    std::map<std::string, int> joints;
    for (const Node& n : m.skeleton) {
        floor = std::min(floor, n.body.t.translation[1] - n.body.size[1] / 2);
        top = std::max(top, n.body.t.translation[1] + n.body.size[1] / 2);
        massSum += n.body.mass;
        joints[n.joint.type]++;
        if (n.pt.empty()) problems.push_back(n.id + " has no anatomical name");
        if (n.body.obj.empty()) problems.push_back(n.id + " has no mesh");
        if (n.body.mass <= 0) problems.push_back(n.id + " has no mass");
        if (!n.parent.empty() && !byId.count(n.parent))
            problems.push_back(n.id + ": parent " + n.parent + " missing");
    }
    if (floor < -0.05 || floor > 0.30) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "lowest bone at y=%.3f: not standing on the floor", floor);
        problems.push_back(buf);
    }
    // left/right pairs mirror across the sagittal plane
    for (const Node& n : m.skeleton) {
        if (n.id.compare(0, 6, "right_") != 0) continue;
        auto other = byId.find("left_" + n.id.substr(6));
        if (other == byId.end()) continue;
        const Vec3& a = n.body.t.translation;
        const Vec3& b = other->second->body.t.translation;
        if (std::fabs(a[0] + b[0]) > 0.02 || std::fabs(a[1] - b[1]) > 0.02)
            problems.push_back(n.id + " / " + other->second->id + " not mirrored");
    }

    return { { "bones", (int)m.skeleton.size() },
             { "mass_kg", massSum },
             { "height_m", top - floor },
             { "joints", joints },
             { "problems", problems } };
}

} // namespace mass
