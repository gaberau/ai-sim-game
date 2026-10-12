// Module: Execute authored conversation nodes, delayed replies, evidence and delivery promises.
#pragma once
#include "ai_sim/SaveSystem.h"
#include <optional>

namespace ai_sim {
// Authored company conversations use the existing file, transport and human-review systems.
class ConversationSystem {
  public:
    // Load authored definitions and initialize this subsystem's saved state.
    static void install(toml::table& people, const std::filesystem::path& catalog);
    // Reject invalid subsystem state before it is used or restored.
    static void validate(const toml::table& people);
    // Synchronize subsystem state at the current game clock.
    static void update(toml::table& people, const toml::table& messages, const FileSystem&,
                       const Network&, const Identity&, double time);
    // Return the currently available authored choices without applying them.
    static std::vector<toml::table> choices(const toml::table& people, const std::string& contact,
                                            const FileSystem&, const Network&, const Identity&,
                                            double time);
    // Apply the selected authored choice and its validated state changes.
    static void choose(toml::table& people, toml::table& messages, toml::array& events,
                       const FileSystem&, const Network&, const Identity&,
                       const std::string& contact, const std::string& conversation,
                       const std::string& choice, double time);
    // Return current conversation commitments and related player-visible context.
    static std::vector<std::string> context(const toml::table& people, const std::string& contact,
                                            double time);
    // Resolve the selected contact's current conversation anchor.
    static std::string anchor(const toml::table&, const std::string& contact);
    // Find the earliest pending human response in game seconds.
    static std::optional<double> next_response(const toml::table& people, double time);
    // Find the next outstanding delivery commitment before the game-time limit.
    static std::optional<double> next_promise(const toml::table& people, double limit);
    // Assess elapsed delivery commitments and record their saved outcomes.
    static void settle_promises(toml::table& people, double time);
};
} // namespace ai_sim
