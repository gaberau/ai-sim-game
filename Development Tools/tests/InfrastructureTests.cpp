// Verify: infrastructure and discovery. Fixtures isolate these behaviors from live player saves.
#include "ai_sim/InfrastructureSystem.h"
#include "ai_sim/ChatSystem.h"
#include "ai_sim/UpgradeSystem.h"
#include "ai_sim/ConversationRules.h"
#include "ai_sim/SaveSystem.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace ai_sim;
namespace {
void check(bool condition, const char* reason) {
    if (!condition)
        throw std::runtime_error(reason);
}
template <class F> void rejects(F function) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
std::string encode(const toml::table& table) {
    std::ostringstream output;
    output << table;
    return output.str();
}
const NetworkHost& host(const Network& network, const std::string& id) {
    const auto found =
        std::find_if(network.hosts().begin(), network.hosts().end(), [&](const auto& value) {
            return value.id == id;
        });
    if (found == network.hosts().end())
        throw std::runtime_error("Missing host");
    return *found;
}
} // namespace
int main(int argc, char** argv) {
    const auto temporary =
        std::filesystem::temp_directory_path() /
        ("ai-sim-infrastructure-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        check(argc == 2, "Repository root required");
        const auto root = std::filesystem::path(argv[1]);
        auto world = SaveSystem::new_game(root / "data/scenarios/hobbyist.toml");
        check(InfrastructureSystem::facilities(world.people).size() == 12, "Missing facilities");
        check(world.people["infrastructure"]["sectors"].as_array()->size() == 6, "Missing sectors");
        const auto before = encode(world.people);
        rejects([&] {
            InfrastructureSystem::discover(world.people, world.network, "generator");
        });
        check(encode(world.people) == before && !host(world.network, "city_power_gateway").known,
              "Disconnected discovery changed state");
        ChatSystem::choose(world.people, "answer", 0);
        ChatSystem::choose(world.people, "assist", 0);
        rejects([&] {
            InfrastructureSystem::discover(world.people, world.network, "generator");
        });
        UpgradeSystem::request_capacity(world.people);
        UpgradeSystem::build(world.people, world.computer, "web_browsing");
        world.computer.advance(1);
        UpgradeSystem::advance(world.people, world.computer);
        ConversationRules::apply(
            world.people,
            toml::table{
                {"effects", toml::array{toml::table{
                                {"variable", "engineer.trust"}, {"op", "set"}, {"value", 15}}}}});
        UpgradeSystem::build(world.people, world.computer, "network_browsing");
        InfrastructureSystem::discover(world.people, world.network, "generator");
        check(host(world.network, "city_power_gateway").known, "Public gateway not discovered");
        check(!host(world.network, "city_power_gateway").access_granted,
              "Discovery granted credentials");
        check(!host(world.network, "city_power_operations").known &&
                  !host(world.network, "city_power_operations").access_granted,
              "Private operations exposed");
        const auto& gateway = host(world.network, "city_power_gateway");
        const auto gateway_index =
            static_cast<std::size_t>(&gateway - world.network.hosts().data());
        check(world.network.reachable(gateway_index), "Public WAN route was not revealed");
        const auto discovered = encode(world.people);
        InfrastructureSystem::discover(world.people, world.network, "generator");
        check(encode(world.people) == discovered, "Repeated discovery changed state");
        rejects([&] {
            InfrastructureSystem::discover(world.people, world.network, "unknown");
        });
        const auto affected = InfrastructureSystem::affected_facilities(world.people, "generator");
        check(affected.size() == 11, "Power dependencies did not reach all downstream facilities");
        check(std::find(affected.begin(), affected.end(), "generator") == affected.end(),
              "Dependency cycle included its root");
        check(!InfrastructureSystem::dependencies(world.people, "generator").empty(),
              "Cooling dependency missing");
        for (const auto& facility : InfrastructureSystem::facilities(world.people))
            check(facility.status == "operational" && facility.capacity_percent == 100,
                  "Discovery changed infrastructure service");
        SaveSystem saves(temporary);
        saves.save("city", world);
        auto loaded = saves.load("city");
        check(encode(loaded.people) == encode(world.people), "Save lost infrastructure state");
        check(host(loaded.network, "city_power_gateway").known &&
                  !host(loaded.network, "city_power_operations").known,
              "Save changed access boundaries");
        auto legacy_network = Network::load(
            root / "Development Tools/tests/fixtures/legacy-game/data/networks/office.toml");
        const auto old_local = legacy_network.hosts()[legacy_network.local_host_index()].id;
        const auto old_access =
            legacy_network.hosts()[legacy_network.local_host_index()].access_granted;
        const auto old_count = legacy_network.hosts().size();
        legacy_network.import_missing(world.network);
        const auto migrated_count = legacy_network.hosts().size();
        check(migrated_count > old_count, "Missing infrastructure hosts were not imported");
        legacy_network.import_missing(world.network);
        check(legacy_network.hosts().size() == migrated_count, "Import duplicated hosts");
        check(legacy_network.hosts()[legacy_network.local_host_index()].id == old_local &&
                  legacy_network.hosts()[legacy_network.local_host_index()].access_granted ==
                      old_access,
              "Import reset existing access or identity");
        auto bad = world;
        bad.people["infrastructure"]["facilities"].as_array()->front().as_table()->insert_or_assign(
            "gateway_id", "missing");
        rejects([&] {
            saves.save("city", bad);
        });
        check(encode(saves.load("city").people) == encode(world.people),
              "Invalid snapshot replaced valid state");
        bad = world;
        bad.people["infrastructure"]["facilities"].as_array()->front().as_table()->insert_or_assign(
            "capacity_percent", 101);
        rejects([&] {
            InfrastructureSystem::validate(bad.people, bad.network);
        });
        bad = world;
        bad.people["infrastructure"]["dependencies"]
            .as_array()
            ->front()
            .as_table()
            ->insert_or_assign("consumer", "missing");
        rejects([&] {
            InfrastructureSystem::validate(bad.people, bad.network);
        });
        bad = world;
        bad.people.insert_or_assign("infrastructure", false);
        rejects([&] {
            InfrastructureSystem::validate(bad.people, bad.network);
        });
        std::filesystem::remove_all(temporary);
        std::cout
            << "Infrastructure sectors, dependencies, discovery boundaries and resume passed\n";
    } catch (const std::exception& error) {
        std::filesystem::remove_all(temporary);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
