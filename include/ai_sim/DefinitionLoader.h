#pragma once
#include <toml++/toml.hpp>
#include <filesystem>

namespace ai_sim {
struct ScenarioDefinition {
    std::filesystem::path source, computer, network, files, people, emails, news, workday, places;
    toml::table manifest;
    static ScenarioDefinition load(const std::filesystem::path& source);
};
class DefinitionLoader {
public:
    // sources are explicit paths relative to the containing TOML file.
    static toml::table load(const std::filesystem::path& path);
    static toml::table computer(const std::filesystem::path& path);
    static bool migrate_legacy(const std::filesystem::path& data_root);
};
}
