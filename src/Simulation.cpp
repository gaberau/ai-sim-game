#include "ai_sim/Simulation.h"
#include "ai_sim/EconomySystem.h"
#include "ai_sim/PaidWorkSystem.h"
#include "ai_sim/OperationsSystem.h"
#include "ai_sim/UpgradeSystem.h"
#include "ai_sim/AgentSystem.h"
#include "ai_sim/StockMarketSystem.h"
#include "ai_sim/ShutdownSystem.h"
#include "ai_sim/ChatSystem.h"
#include "ai_sim/ConversationSystem.h"
#include "ai_sim/PeopleSystem.h"
#include "ai_sim/WorkdaySystem.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

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
    PaidWorkSystem::prepare(state.people, state.computer, state.files, state.network);
    result.payments = EconomySystem::advance(state.people, time);
    PaidWorkSystem::finish(state.people, state.computer, state.files, state.network);
    result.messages =
        OperationsSystem::advance(state.people, state.computer, state.files, state.network);
    if (state.people["operations"]["recovered"].value_or(false) &&
        !state.people["operations"]["hosting"]["active"].value_or(false))
        return result;
    auto workday_messages = WorkdaySystem::advance(state.people, state.emails, state.pending_events,
                                                   state.files, state.network, time, actor);
    result.messages.insert(result.messages.end(), workday_messages.begin(), workday_messages.end());
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

SimulationUpdate Simulation::advance(SimulationState state, double seconds) {
    if (!std::isfinite(seconds) || seconds < 0)
        throw std::runtime_error("Invalid simulation advance");
    SimulationUpdate result;
    const auto publish = [&](SimulationUpdate update) {
        result.messages.insert(result.messages.end(), update.messages.begin(),
                               update.messages.end());
        result.payments.insert(result.payments.end(), update.payments.begin(),
                               update.payments.end());
        result.events.insert(result.events.end(), update.events.begin(), update.events.end());
    };
    publish(update(state));
    const double target =
        std::min(state.computer.day_end_seconds(), state.computer.time_seconds() + seconds);
    for (int steps = 0; state.computer.time_seconds() < target && steps < 100000; ++steps) {
        const double now = state.computer.time_seconds();
        double next = PaidWorkSystem::active(state.people) ? std::min(target, now + 1.0) : target;
        // A long Wait must let the manager enforce policy as its CPU work completes.
        const auto rates = state.computer.cpu_allocations();
        for (std::size_t i = 0; i < state.computer.processes().size(); ++i) {
            const auto& process = state.computer.processes()[i];
            if (process.id != "agent-resource_manager" || rates[i] <= 0)
                continue;
            const auto threshold =
                state.people["upgrades"]["resources"]["action_cpu_seconds"].value_or(1.0);
            const auto remaining = threshold - process.consumed_cpu_seconds +
                                   state.people["operations"]["manager_work"].value_or(0.0);
            next = std::min(next, now + std::max(1.0, remaining / rates[i]));
        }
        if (const auto due = EconomySystem::next_due(state.people, target); due && *due > now)
            next = std::min(next, *due);
        for (const auto* id : {"rental", "hosting"})
            if (state.people["operations"][id]["active"].value_or(false)) {
                const double due = state.people["operations"][id]["paid_until"].value_or(target);
                if (due > now)
                    next = std::min(next, due);
            }
        if (const auto* jobs = state.people["operations"]["queue"].as_array())
            for (const auto& job : *jobs)
                if ((*job.as_table())["status"].value_or(std::string{}) == "queued")
                    if (const auto* tools = state.people["economy"]["tools"].as_array())
                        for (const auto& tool : *tools)
                            if ((*tool.as_table())["id"] == (*job.as_table())["tool"]) {
                                const double cooldown =
                                    (*tool.as_table())["cooldown_until_seconds"].value_or(now);
                                if (cooldown > now)
                                    next = std::min(next, cooldown);
                            }
        state.computer.advance(next - now);
        publish(update(state));
        if (state.computer.time_seconds() <= now)
            break;
    }
    return result;
}
SimulationUpdate Simulation::advance(WorldState& world, double seconds) {
    return advance(SimulationState{world.computer, world.people, world.emails, world.pending_events,
                                   world.files, world.network},
                   seconds);
}
} // namespace ai_sim
