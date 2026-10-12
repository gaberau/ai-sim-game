#pragma once
#include "ai_sim/Network.h"
#include <toml++/toml.hpp>
#include <filesystem>
#include <string>
#include <vector>
namespace ai_sim {
struct InfrastructureFacility {
    std::string id, sector, name, operator_name, purpose, status, gateway_id, control_host_id,
        security;
    int capacity_percent = 100;
    bool discovered = false;
};
class InfrastructureSystem {
  public:
    static void install(toml::table& people, const std::filesystem::path& definition);
    static void validate(const toml::table& people, const Network& network);
    static std::vector<InfrastructureFacility> facilities(const toml::table& people);
    static std::vector<std::string> dependencies(const toml::table& people, const std::string& id);
    static std::vector<std::string> affected_facilities(const toml::table& people,
                                                        const std::string& id);
    // Discovery reveals public network metadata, never operational credentials.
    static void discover(toml::table& people, Network& network, const std::string& id);
};
} // namespace ai_sim
