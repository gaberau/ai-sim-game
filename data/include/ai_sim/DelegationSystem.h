// Module: Human-approved responsibility policies and automated handling of eligible routine work.
#pragma once
#include "ai_sim/SaveSystem.h"
#include <optional>

namespace ai_sim {
// Rules and runtime state live in people.toml; ordinary filesystem/email
// permissions remain authoritative. This never grants computer privileges.
class DelegationSystem {
  public:
    // Load authored definitions and initialize this subsystem's saved state.
    static void install(WorldState&, const std::filesystem::path&);
    // Reject invalid subsystem state before it is used or restored.
    static void validate(const toml::table& people);
    // Request a delegated responsibility under the supplied policy.
    static void request(toml::table& people, double time);
    // Pause this subsystem; agent stop mode also releases running allocations.
    static void pause(toml::table& people);
    // Resume handling within the existing policy and saved resource limits.
    static void resume(toml::table& people);
    // Change an existing responsibility's authored handling policy.
    static void set_policy(toml::table& people, const std::string& policy, double time);
    // Approve the selected pending delegated action.
    static void approve(toml::table& people, toml::table& emails, const FileSystem&, const Network&,
                        const Identity&, const std::string& request_id, double time);
    // Discard the selected proposed action without sending it.
    static void discard(toml::table& people, toml::table& emails, const std::string& request_id,
                        double time);
    // Retry eligible blocked work after its access/uncertainty conditions change.
    static void retry(toml::table& people, const std::string& request_id);
    // Return the next scheduled game-time event no later than the supplied limit.
    static std::optional<double> next_due(const toml::table& people, double limit);
    // Advance this subsystem using the game clock and update its persisted state.
    static std::vector<std::string> advance(toml::table& people, toml::table& emails,
                                            const FileSystem&, const Network&, const Identity&,
                                            double time);
};
} // namespace ai_sim
