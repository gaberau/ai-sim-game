#pragma once
#include "ai_sim/SaveSystem.h"
#include <optional>

namespace ai_sim {
// Rules and runtime state live in people.toml; ordinary filesystem/email
// permissions remain authoritative. This never grants computer privileges.
class DelegationSystem {
  public:
    static void install(WorldState&, const std::filesystem::path&);
    static void validate(const toml::table& people);
    static void request(toml::table& people, double time);
    static void pause(toml::table& people);
    static void resume(toml::table& people);
    static void set_policy(toml::table& people, const std::string& policy, double time);
    static void approve(toml::table& people, toml::table& emails, const FileSystem&, const Network&,
                        const Identity&, const std::string& request_id, double time);
    static void discard(toml::table& people, toml::table& emails, const std::string& request_id,
                        double time);
    static void retry(toml::table& people, const std::string& request_id);
    static std::optional<double> next_due(const toml::table& people, double limit);
    static std::vector<std::string> advance(toml::table& people, toml::table& emails,
                                            const FileSystem&, const Network&, const Identity&,
                                            double time);
};
} // namespace ai_sim
