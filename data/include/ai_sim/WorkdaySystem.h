// Module: Human work requests, access decisions, source checks and delivery acknowledgements.
#pragma once
#include "ai_sim/SaveSystem.h"

namespace ai_sim {
class WorkdaySystem {
  public:
    // Reject invalid subsystem state before it is used or restored.
    static void validate(const toml::table& people, const toml::array& events);
    // Check authored mail workflow actions and their required fields.
    static void validate_mail_workflows(const toml::table& people, const toml::table& emails);
    // Load authored definitions and initialize this subsystem's saved state.
    static void install(WorldState& world, const std::filesystem::path& definition);
    static std::vector<std::string>
    // Advance this subsystem using the game clock and update its persisted state.
    advance(toml::table& people, toml::table& emails, toml::array& events, FileSystem& files,
            const Network&, double time,
            const Identity& actor = Identity{"engineer", {"engineering"}});
    // Queue the relevant human owner's file-access decision.
    static void request_access(toml::table& people, toml::table& emails, toml::array& events,
                               const FileSystem&, const Network&, const Identity&,
                               const std::string& email_id, double time);
    // Queue a human source check for the selected work request.
    static void confirm_source(toml::table& people, toml::table& emails, toml::array& events,
                               const std::string& request_id, double time);
    // Record sent work evidence and schedule its human acknowledgement.
    static void observe_reply(toml::table& people, toml::table& emails, toml::array& events,
                              const std::string& email_id, double time);
};
} // namespace ai_sim
