#include "ai_sim/Simulation.h"
#include "ai_sim/EconomySystem.h"
#include "ai_sim/UpgradeSystem.h"
#include "ai_sim/AgentSystem.h"
#include "ai_sim/StockMarketSystem.h"
#include "ai_sim/ShutdownSystem.h"
#include "ai_sim/ChatSystem.h"
#include "ai_sim/ConversationSystem.h"
#include "ai_sim/PeopleSystem.h"
#include "ai_sim/WorkdaySystem.h"

namespace ai_sim {
namespace {
bool engineer_review_started(const toml::table& people) {
    const auto* runs = people["chat"]["workday"]["runs"].as_array();
    if (!runs)
        return false;
    for (const auto& node : *runs) {
        const auto* run = node.as_table();
        if (run && (*run)["id"].value_or(std::string{}) == "mark-jessica-review")
            return (*run)["started"].value_or(false);
    }
    return false;
}
} // namespace

SimulationUpdate Simulation::update(SimulationState state) {
    SimulationUpdate result;
    if (!ChatSystem::connected(state.people) || ShutdownSystem::active(state.people))
        return result;

    UpgradeSystem::advance(state.people, state.computer, &state.network);
    const Identity actor{state.computer.player_user(), state.computer.player_groups()};
    const double time = state.computer.time_seconds();
    result.messages = WorkdaySystem::advance(state.people, state.emails, state.pending_events,
                                             state.files, state.network, time, actor);
    auto resource_messages =
        AgentSystem::update(state.people, state.computer, state.files, state.network);
    result.messages.insert(result.messages.end(), resource_messages.begin(),
                           resource_messages.end());
    ChatSystem::discover(state.people, state.emails, time);

    const bool had_review = engineer_review_started(state.people);
    const bool knew_jessica = PeopleSystem::dossier(state.people, "jessica") != nullptr;
    ConversationSystem::update(state.people, state.emails, state.files, state.network, actor, time);

    UpgradeSystem::run_agents(state.people, state.computer, state.emails, state.pending_events,
                              state.files, state.network);
    result.payments = EconomySystem::advance(state.people, time);
    StockMarketSystem::advance(state.people, time);
    if (!had_review && engineer_review_started(state.people))
        result.events.push_back(SimulationEvent::EngineerReviewStarted);
    if (!knew_jessica && PeopleSystem::dossier(state.people, "jessica"))
        result.events.push_back(SimulationEvent::JessicaDiscovered);
    return result;
}

SimulationUpdate Simulation::update(WorldState& world) {
    return update(SimulationState{world.computer, world.people, world.emails, world.pending_events,
                                  world.files, world.network});
}

} // namespace ai_sim
