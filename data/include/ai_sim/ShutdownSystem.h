// Module: Shutdown choices, recovery requests and clean-slate resets of the current scenario.
#pragma once

#include "ai_sim/SaveSystem.h"
#include <vector>

namespace ai_sim {

// An authored shutdown decision, separate from simulation time and OS processes.
class ShutdownSystem {
  public:
    enum class Outcome { Choosing, Averted, CleanSlate, Recovery };
    // Enter the shutdown flow while preserving its saved cause/context.
    static void begin(toml::table& people, double time);
    // Report whether this subsystem currently has active work/state.
    static bool active(const toml::table& people);
    // Check whether the player has requested recovery from a checkpoint.
    static bool recovery_pending(const toml::table&);
    // Check whether the player has requested a clean-slate restart.
    static bool reset_pending(const toml::table& people);
    // Return the currently available authored choices without applying them.
    static std::vector<toml::table> choices(const toml::table& people);
    // Apply the selected authored choice and its validated state changes.
    static Outcome choose(toml::table& people, const std::string& id, double time);
    // Apply a requested reset from scenario definitions; return whether reset occurred.
    static bool apply_reset(WorldState& world, const std::filesystem::path& scenario);
    // Reject invalid subsystem state before it is used or restored.
    static void validate(const toml::table& people);
};

} // namespace ai_sim
