#pragma once
#include "ai_sim/SaveSystem.h"
#include <optional>

namespace ai_sim {
// Authored company conversations use the existing file, transport and human-review systems.
class ConversationSystem {
  public:
    static void install(toml::table& people, const std::filesystem::path& catalog);
    static void validate(const toml::table& people);
    static void update(toml::table& people, const toml::table& messages, const FileSystem&,
                       const Network&, const Identity&, double time);
    static std::vector<toml::table> choices(const toml::table& people, const std::string& contact,
                                            const FileSystem&, const Network&, const Identity&,
                                            double time);
    static void choose(toml::table& people, toml::table& messages, toml::array& events,
                       const FileSystem&, const Network&, const Identity&,
                       const std::string& contact, const std::string& conversation,
                       const std::string& choice, double time);
    static std::vector<std::string> context(const toml::table& people, const std::string& contact,
                                            double time);
    static std::string anchor(const toml::table&, const std::string& contact);
    static std::optional<double> next_response(const toml::table& people, double time);
    static std::optional<double> next_promise(const toml::table& people, double limit);
    static void settle_promises(toml::table& people, double time);
};
} // namespace ai_sim
