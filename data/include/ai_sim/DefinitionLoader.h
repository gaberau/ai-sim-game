// Module: Expand relative TOML sources and scenario manifests with cycle/conflict validation.
#pragma once
#include <toml++/toml.hpp>
#include <filesystem>

namespace ai_sim {
struct ScenarioDefinition {
    std::filesystem::path source, computer, network, files, people, emails, news, workday, places,
        economy, stock_market, infrastructure;
    toml::table manifest;
    // Read authored TOML definitions and reject malformed values.
    static ScenarioDefinition load(const std::filesystem::path& source);
};
class DefinitionLoader {
  public:
    // sources are explicit paths relative to the containing TOML file.
    static toml::table load(const std::filesystem::path& path);
    // Expand application defaults into a concrete computer definition.
    static toml::table computer(const std::filesystem::path& path);
    // Stage a supported legacy-data migration and preserve the source definitions.
    static bool migrate_legacy(const std::filesystem::path& data_root);
};
} // namespace ai_sim
