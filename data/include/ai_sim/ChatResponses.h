// Module: Resolve message-specific response actions against saved requests and conversations.
#pragma once
#include "ai_sim/ChatSystem.h"
namespace ai_sim {
// Small authored response sets for incoming messages outside an active flowchart.
class ChatResponses {
  public:
    // Load authored definitions and initialize this subsystem's saved state.
    static void install(toml::table&, const std::filesystem::path&);
    // Reject invalid subsystem state before it is used or restored.
    static void validate(const toml::table&);
    // Return the currently available authored choices without applying them.
    static std::vector<toml::table> choices(const toml::table&, const toml::table&, const ChatLine&,
                                            double);
    // Apply the selected authored choice and its validated state changes.
    static void choose(toml::table&, toml::table&, toml::array&, const FileSystem&, const Network&,
                       const Identity&, const std::string& contact, const ChatLine&,
                       const toml::table&, double);
};
} // namespace ai_sim
