// Module: Opening progression, capabilities, contact discovery and persistent chat transcripts.
#pragma once
#include "ai_sim/SaveSystem.h"
#include <vector>

namespace ai_sim {
struct ChatContact {
    std::string id, name;
    double latest_seconds = 0;
    int unread = 0;
};
struct ChatLine {
    std::string speaker, text, request_id;
    double seconds = 0;
    bool outgoing = false;
    std::string id, conversation_id, node_id;
};
class ChatSystem {
  public:
    // Load authored definitions and initialize this subsystem's saved state.
    static void install(WorldState&, const std::filesystem::path&, bool fresh);
    // Reject invalid subsystem state before it is used or restored.
    static void validate(const toml::table& people);
    // Check whether an authored capability name belongs to the supported catalog.
    static bool valid_capability(const std::string& capability);
    // Check whether the player has unlocked the named capability.
    static bool available(const toml::table&, const std::string& capability);
    // Check authored tab visibility separately from feature availability.
    static bool tab_visible(const toml::table&, const std::string& tab);
    // Recognize authored tab IDs, including legacy IDs retained for content compatibility.
    static bool valid_tab(const std::string& tab);
    // Reveal a supported workspace tab through saved progression state.
    static void reveal_tab(toml::table&, const std::string& tab);
    // Check whether opening progression has revealed the main interface.
    static bool interface_visible(const toml::table&);
    // Report whether the opening script is holding for a scheduled condition.
    static bool waiting(const toml::table&);
    // Report whether opening progression has stopped the AI.
    static bool stopped(const toml::table&);
    // Check whether progression has revealed the contact list.
    static bool contacts_visible(const toml::table&);
    // Reveal contact navigation without altering known conversation history.
    static void reveal_contacts(toml::table&);
    // Check whether the opening script has connected company chat.
    static bool connected(const toml::table&);
    // Persist the named authored capability unlock.
    static void unlock(toml::table&, const std::string&);
    // Return the currently available authored choices without applying them.
    static std::vector<toml::table> choices(const toml::table&);
    // Apply the selected authored choice and its validated state changes.
    static void choose(toml::table&, const std::string& id, double time);
    // Validate and deliver the outgoing body to a known contact.
    static void send(toml::table&, const toml::table& messages, const Network&,
                     const std::string& contact, const std::string& body, double time);
    // Reveal contacts/messages that are eligible at the supplied game time.
    static void discover(toml::table& people, const toml::table& messages, double time);
    // Resolve a sender address to its stable contact ID.
    static std::string contact_for(const toml::table& people, const std::string& address);
    // Return known contacts and unread counts without UI archive filtering.
    static std::vector<ChatContact> contacts(const toml::table&, const toml::table&, double time);
    // Return the selected contact's visible transcript at the supplied game time.
    static std::vector<ChatLine> timeline(const toml::table&, const toml::table&,
                                          const std::string& contact, double time);
    // Save the transcript boundary the player has seen for this contact.
    static void mark_read(toml::table& people, const std::string& contact);
};
} // namespace ai_sim
