#pragma once

#include "ai_sim/SaveSystem.h"
#include <vector>

namespace ai_sim {

// Borrow the live world without copying its TOML documents each frame.
struct SimulationState {
    Computer& computer;
    toml::table& people;
    toml::table& emails;
    toml::array& pending_events;
    FileSystem& files;
    Network& network;
};

enum class SimulationEvent { EngineerReviewStarted, JessicaDiscovered };

struct SimulationUpdate {
    std::vector<std::string> messages;
    std::vector<std::string> payments;
    std::vector<SimulationEvent> events;
};

class Simulation {
  public:
    // Synchronize work, discovery and conversations at the current world clock.
    static SimulationUpdate update(SimulationState state);
    static SimulationUpdate update(WorldState& world);
    static SimulationUpdate advance(SimulationState state, double seconds);
    static SimulationUpdate advance(WorldState& world, double seconds);
};

} // namespace ai_sim
