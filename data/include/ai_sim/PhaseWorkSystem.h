// Module: Phase-specific work and follow-ups scheduled against the simulated workday.
#pragma once
#include "ai_sim/SaveSystem.h"
#include <optional>
namespace ai_sim {
class PhaseWorkSystem {
  public:
    // Load authored definitions and initialize this subsystem's saved state.
    static void install(WorldState&, const std::filesystem::path&);
    // Reject invalid subsystem state before it is used or restored.
    static void validate(const toml::table&);
    // Return the next scheduled game-time event no later than the supplied limit.
    static std::optional<double> next_due(const toml::table&, double limit,
                                          bool include_commitments = true);
    // Advance this subsystem using the game clock and update its persisted state.
    static void advance(toml::table&, toml::table& emails, double time);
};
} // namespace ai_sim
