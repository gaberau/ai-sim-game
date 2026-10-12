// Module: Validate city facilities, dependencies and authored discovery state.
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
    // Load authored definitions and initialize this subsystem's saved state.
    static void install(toml::table& people, const std::filesystem::path& definition);
    // Reject invalid subsystem state before it is used or restored.
    static void validate(const toml::table& people, const Network& network);
    // Return player-visible city facilities and their current status.
    static std::vector<InfrastructureFacility> facilities(const toml::table& people);
    // Return the authored dependency edges between infrastructure facilities.
    static std::vector<std::string> dependencies(const toml::table& people, const std::string& id);
    // Find facilities affected by the selected dependency or service disruption.
    static std::vector<std::string> affected_facilities(const toml::table& people,
                                                        const std::string& id);
    // Discovery reveals public network metadata, never operational credentials.
    static void discover(toml::table& people, Network& network, const std::string& id);
};
} // namespace ai_sim
