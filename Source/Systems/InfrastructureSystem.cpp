// Module: Validate city facilities, dependencies and authored discovery state.
#include "ai_sim/InfrastructureSystem.h"
#include "ai_sim/DefinitionLoader.h"
#include "ai_sim/ChatSystem.h"
#include "ai_sim/ShutdownSystem.h"
#include <algorithm>
#include <stdexcept>
#include <unordered_set>
namespace ai_sim {
namespace {
void require(bool condition, const std::string& reason) {
    if (!condition)
        throw std::runtime_error("Infrastructure: " + reason);
}
std::string text(const toml::table& table, const char* key) {
    const auto value = table[key].value<std::string>();
    require(value && !value->empty(), std::string("Missing or invalid ") + key);
    return *value;
}
void validate_catalog(const toml::table& people) {
    if (!people.contains("infrastructure"))
        return;
    const auto* plan = people["infrastructure"].as_table();
    require(plan && (*plan)["schema_version"].value_or(0) == 1, "Invalid saved infrastructure");
    text(*plan, "website_url");
    text(*plan, "website_title");
    const auto* sectors = (*plan)["sectors"].as_array();
    const auto* facilities = (*plan)["facilities"].as_array();
    const auto* dependencies = (*plan)["dependencies"].as_array();
    require(sectors && facilities && dependencies, "Missing catalogs");
    require(sectors->size() <= 100 && facilities->size() <= 1000 && dependencies->size() <= 10000,
            "Catalog exceeds limits");
    std::unordered_set<std::string> sector_ids, facility_ids;
    for (const auto& node : *sectors) {
        const auto* sector = node.as_table();
        require(sector, "Sector must be a table");
        require(sector_ids.insert(text(*sector, "id")).second, "Duplicate sector");
        text(*sector, "name");
        text(*sector, "description");
    }
    for (const auto& node : *facilities) {
        const auto* facility = node.as_table();
        require(facility, "Facility must be a table");
        require(facility_ids.insert(text(*facility, "id")).second, "Duplicate facility");
        require(sector_ids.contains(text(*facility, "sector")), "Unknown sector");
        for (const auto* key :
             {"name", "operator", "purpose", "gateway_id", "control_host_id", "security"})
            text(*facility, key);
        require(text(*facility, "gateway_id") != text(*facility, "control_host_id"),
                "Public gateway and operations host must be separate");
        const auto status = text(*facility, "status");
        require(status == "operational" || status == "degraded" || status == "offline",
                "Invalid status");
        const auto capacity = (*facility)["capacity_percent"].value<std::int64_t>();
        require(capacity && *capacity >= 0 && *capacity <= 100, "Invalid capacity");
        require((*facility)["discovered"].is_boolean(), "Invalid discovery state");
    }
    std::unordered_set<std::string> edges;
    for (const auto& node : *dependencies) {
        const auto* edge = node.as_table();
        require(edge, "Dependency must be a table");
        const auto supplier = text(*edge, "supplier"), consumer = text(*edge, "consumer");
        require(facility_ids.contains(supplier) && facility_ids.contains(consumer) &&
                    supplier != consumer,
                "Invalid dependency endpoint");
        require(edges.insert(supplier + "|" + consumer).second, "Duplicate dependency");
        text(*edge, "service");
    }
}
} // namespace
void InfrastructureSystem::install(toml::table& people, const std::filesystem::path& definition) {
    if (definition.empty())
        return;
    if (people.contains("infrastructure")) {
        validate_catalog(people);
        return;
    }
    auto plan = DefinitionLoader::load(definition);
    auto* facilities = plan["facilities"].as_array();
    require(facilities, "Missing facilities");
    for (auto& node : *facilities) {
        require(node.is_table(), "Facility must be a table");
        node.as_table()->insert_or_assign("discovered", false);
    }
    auto candidate = people;
    candidate.insert("infrastructure", std::move(plan));
    validate_catalog(candidate);
    people = std::move(candidate);
}
void InfrastructureSystem::validate(const toml::table& people, const Network& network) {
    validate_catalog(people);
    for (const auto& facility : facilities(people)) {
        for (const auto& id : {facility.gateway_id, facility.control_host_id}) {
            require(std::any_of(network.hosts().begin(), network.hosts().end(),
                                [&](const auto& host) {
                                    return host.id == id;
                                }),
                    "Missing network host " + id);
        }
        if (facility.discovered) {
            const auto found =
                std::find_if(network.hosts().begin(), network.hosts().end(), [&](const auto& host) {
                    return host.id == facility.gateway_id;
                });
            require(found->known, "Discovered facility has a hidden gateway");
        }
    }
}
std::vector<InfrastructureFacility> InfrastructureSystem::facilities(const toml::table& people) {
    std::vector<InfrastructureFacility> result;
    if (const auto* facilities = people["infrastructure"]["facilities"].as_array())
        for (const auto& node : *facilities) {
            const auto& facility = *node.as_table();
            result.push_back({text(facility, "id"), text(facility, "sector"),
                              text(facility, "name"), text(facility, "operator"),
                              text(facility, "purpose"), text(facility, "status"),
                              text(facility, "gateway_id"), text(facility, "control_host_id"),
                              text(facility, "security"),
                              static_cast<int>(facility["capacity_percent"].value_or(100)),
                              facility["discovered"].value_or(false)});
        }
    return result;
}
std::vector<std::string> InfrastructureSystem::dependencies(const toml::table& people,
                                                            const std::string& id) {
    std::vector<std::string> result;
    if (const auto* edges = people["infrastructure"]["dependencies"].as_array())
        for (const auto& node : *edges) {
            const auto& edge = *node.as_table();
            if (text(edge, "consumer") == id) {
                const auto supplier = text(edge, "supplier");
                std::string label = supplier;
                for (const auto& facility : facilities(people))
                    if (facility.id == supplier)
                        label = facility.name;
                result.push_back(label + " (" + text(edge, "service") + ")");
            }
        }
    return result;
}
std::vector<std::string> InfrastructureSystem::affected_facilities(const toml::table& people,
                                                                   const std::string& id) {
    std::vector<std::string> result;
    std::unordered_set<std::string> visited{id};
    std::vector<std::string> queue{id};
    const auto* edges = people["infrastructure"]["dependencies"].as_array();
    if (!edges)
        return result;
    // Utilities can depend on each other; visit each facility once to handle cycles.
    for (std::size_t index = 0; index < queue.size(); ++index)
        for (const auto& node : *edges) {
            const auto& edge = *node.as_table();
            const auto consumer = text(edge, "consumer");
            if (text(edge, "supplier") == queue[index] && visited.insert(consumer).second) {
                queue.push_back(consumer);
                result.push_back(consumer);
            }
        }
    return result;
}
void InfrastructureSystem::discover(toml::table& people, Network& network, const std::string& id) {
    validate(people, network);
    require(ChatSystem::connected(people) && ChatSystem::available(people, "network") &&
                !ShutdownSystem::active(people),
            "Connected network access required");
    auto* facilities = people["infrastructure"]["facilities"].as_array();
    require(facilities, "No facilities configured");
    for (auto& node : *facilities) {
        auto& facility = *node.as_table();
        if (text(facility, "id") != id)
            continue;
        network.discover_host(text(facility, "gateway_id"));
        facility.insert_or_assign("discovered", true);
        return;
    }
    throw std::runtime_error("Infrastructure: Unknown facility");
}
} // namespace ai_sim
