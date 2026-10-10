#pragma once
#include <toml++/toml.hpp>
#include <filesystem>
namespace ai_sim {
class ConversationRules {
public:
    static void install(toml::table& people, const std::filesystem::path& people_directory);
    static void validate_definition(const toml::table&, const toml::table* people = nullptr);
    static void validate_state(const toml::table& people);
    static bool passes(const toml::table& people, const toml::table& value);
    static void apply(toml::table& people, const toml::table& value);
    static double get(const toml::table& people, const std::string& reference);
};
}
