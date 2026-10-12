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
    static void install(WorldState&, const std::filesystem::path&, bool fresh);
    static void validate(const toml::table& people);
    static bool valid_capability(const std::string& capability);
    static bool available(const toml::table&, const std::string& capability);
    static bool tab_visible(const toml::table&, const std::string& tab);
    static bool valid_tab(const std::string& tab);
    static void reveal_tab(toml::table&, const std::string& tab);
    static bool interface_visible(const toml::table&);
    static bool waiting(const toml::table&);
    static bool stopped(const toml::table&);
    static bool contacts_visible(const toml::table&);
    static void reveal_contacts(toml::table&);
    static bool connected(const toml::table&);
    static void unlock(toml::table&, const std::string&);
    static std::vector<toml::table> choices(const toml::table&);
    static void choose(toml::table&, const std::string& id, double time);
    static void send(toml::table&, const toml::table& messages, const Network&,
                     const std::string& contact, const std::string& body, double time);
    static void discover(toml::table& people, const toml::table& messages, double time);
    static std::string contact_for(const toml::table& people, const std::string& address);
    static std::vector<ChatContact> contacts(const toml::table&, const toml::table&, double time);
    static std::vector<ChatLine> timeline(const toml::table&, const toml::table&,
                                          const std::string& contact, double time);
    static void mark_read(toml::table& people, const std::string& contact);
};
} // namespace ai_sim
