// Module: Typed character-state bounds and conditions/effects shared by authored conversations.
#pragma once
#include <toml++/toml.hpp>
#include <filesystem>
namespace ai_sim {
class ConversationRules {
  public:
    // Load authored definitions and initialize this subsystem's saved state.
    static void install(toml::table& people, const std::filesystem::path& people_directory);
    // Validate authored state names, bounds, conditions and effects.
    static void validate_definition(const toml::table&, const toml::table* people = nullptr);
    // Validate current saved character state against its authored bounds.
    static void validate_state(const toml::table& people);
    // Evaluate authored conditions without modifying state.
    static bool passes(const toml::table& people, const toml::table& value);
    // Apply authored effects after checking supported fields and bounds.
    static void apply(toml::table& people, const toml::table& value);
    // Read a numeric character-state path used by authored conditions and UI costs.
    static double get(const toml::table& people, const std::string& reference);
};
} // namespace ai_sim
