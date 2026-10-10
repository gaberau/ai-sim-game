#pragma once
#include "ai_sim/ChatSystem.h"
namespace ai_sim {
// Small authored response sets for incoming messages outside an active flowchart.
class ChatResponses {
public:
    static void install(toml::table&, const std::filesystem::path&);
    static void validate(const toml::table&);
    static std::vector<toml::table> choices(const toml::table&, const toml::table&, const ChatLine&, double);
    static void choose(toml::table&, toml::table&, toml::array&, const FileSystem&, const Network&, const Identity&,
        const std::string& contact, const ChatLine&, const toml::table&, double);
};
}
