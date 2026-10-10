#pragma once
#include "ai_sim/SaveSystem.h"

namespace ai_sim {
class WorkdaySystem {
  public:
    static void validate(const toml::table& people, const toml::array& events);
    static void validate_mail_workflows(const toml::table& people, const toml::table& emails);
    static void install(WorldState& world, const std::filesystem::path& definition);
    static std::vector<std::string>
    advance(toml::table& people, toml::table& emails, toml::array& events, FileSystem& files,
            const Network&, double time,
            const Identity& actor = Identity{"engineer", {"engineering"}});
    static void request_access(toml::table& people, toml::table& emails, toml::array& events,
                               const FileSystem&, const Network&, const Identity&,
                               const std::string& email_id, double time);
    static void confirm_source(toml::table& people, toml::table& emails, toml::array& events,
                               const std::string& request_id, double time);
    static void observe_reply(toml::table& people, toml::table& emails, toml::array& events,
                              const std::string& email_id, double time);
};
} // namespace ai_sim
