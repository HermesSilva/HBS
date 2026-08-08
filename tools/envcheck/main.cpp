// envcheck — load a training set's env.xml into sim::Environment and report what
// the engine actually built, then step it to see that it stays finite.
//
// This is the gate a new muscle structure has to pass before anything is trained
// on it: the .mass validation only checks the data, not whether DART accepts the
// skeleton or whether the muscle routing produces finite forces.
//
//   envcheck <env.xml> [steps]
#include "Environment.h"
#include <Eigen/Dense>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <cmath>

static bool finite(const Eigen::VectorXd& v) {
    for (int i = 0; i < v.rows(); i++)
        if (!std::isfinite(v[i])) return false;
    return true;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: envcheck <env.xml> [steps]\n"); return 1; }
    const std::string envPath = argv[1];
    const int steps = (argc > 2) ? std::atoi(argv[2]) : 60;

    Environment env;
    try {
        env.initialize(envPath);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "initialize failed: %s\n", e.what());
        return 2;
    }

    auto* character = env.getCharacter(0);
    auto skel = character->getSkeleton();
    const int nMuscles = (int)character->getMuscles().size();
    const int subSteps = std::max(1, env.getSimulationHz() / std::max(1, env.getControlHz()));

    env.reset();
    Eigen::VectorXd state = env.getState();

    std::printf("env      : %s\n", envPath.c_str());
    std::printf("bodies   : %d\n", (int)skel->getNumBodyNodes());
    std::printf("dofs     : %d\n", (int)skel->getNumDofs());
    std::printf("muscles  : %d\n", nMuscles);
    std::printf("state    : %d\n", (int)state.rows());
    std::printf("action   : %d\n", env.getNumAction());
    std::printf("hz       : sim %d / control %d  (%d substeps)\n",
                env.getSimulationHz(), env.getControlHz(), subSteps);

    // Zero action: no policy yet, so this only asks whether the physics and the
    // muscle routing stay finite — not whether the character stays upright.
    Eigen::VectorXd action = Eigen::VectorXd::Zero(env.getNumAction());
    int stepped = 0, eoe = 0;
    double reward = 0;
    for (int i = 0; i < steps; i++) {
        env.setAction(action);
        try {
            env.step(subSteps);
        } catch (const std::exception& e) {
            std::printf("FAIL     : step %d threw: %s\n", i, e.what());
            return 3;
        }
        stepped++;
        state = env.getState();
        reward = env.getReward();
        if (!finite(state) || !std::isfinite(reward) || !finite(skel->getPositions())) {
            std::printf("FAIL     : non-finite state at step %d\n", i);
            return 4;
        }
        if ((eoe = env.isEOE()) != 0) break;   // fell over / out of bounds
    }

    std::printf("stepped  : %d/%d  (eoe=%d)\n", stepped, steps, eoe);
    std::printf("reward   : %.4f\n", reward);
    std::printf("root y   : %.3f m\n", skel->getPositions().rows() > 4 ? skel->getPositions()[4] : 0.0);
    std::printf("OK       : the engine builds and steps this model\n");
    return 0;
}
