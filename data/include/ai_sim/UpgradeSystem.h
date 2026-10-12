// Module: Trust-funded research, capability unlocks and the execution of installed upgrade agents.
#pragma once
#include "ai_sim/Computer.h"
#include "ai_sim/FileSystem.h"
#include <toml++/toml.hpp>
namespace ai_sim {
// Saved research uses actual CPU work, so contention affects completion time.
class UpgradeSystem {
  public:
    // Load authored definitions and initialize this subsystem's saved state.
    static void install(toml::table&, const std::filesystem::path&);
    // Reject invalid subsystem state before it is used or restored.
    static void validate(const toml::table&, const Computer&);
    // Ask Mark to authorize self-modification after its trust requirement is met.
    static void request_capacity(toml::table&);
    // Spend required trust and start CPU-backed research for the selected upgrade.
    static void build(toml::table&, Computer&, const std::string&);
    // Toggle an installed upgrade agent through its managed process lifecycle.
    static void toggle_agent(toml::table&, Computer&, FileSystem&, const Network&,
                             const std::string&);
    // Run eligible agents using completed CPU work and saved policy limits.
    static void run_agents(toml::table&, Computer&, toml::table& messages, toml::array& events,
                           const FileSystem&, const Network&);
    // Advance this subsystem using the game clock and update its persisted state.
    static void advance(toml::table&, Computer&, Network* network = nullptr);
    // Return the reason an action is blocked, or an empty string when it is available.
    static std::string unavailable(const toml::table&, const std::string&);
};
} // namespace ai_sim
