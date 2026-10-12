#include "ai_sim/EconomySystem.h"
#include "ai_sim/UpgradeSystem.h"
#include "ai_sim/ChatSystem.h"
#include "ai_sim/ConversationRules.h"
#include "ai_sim/Simulation.h"
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
template <class Function> void rejects(Function function) {
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
void advance(WorldState& world, double time) {
    world.computer.advance(time - world.computer.time_seconds());
    Simulation::update(world);
}
} // namespace
int main(int argc, char** argv) {
    const auto temporary =
        std::filesystem::temp_directory_path() /
        ("ai-sim-economy-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        check(argc == 2, "Repository root required");
        const auto scenario = std::filesystem::path(argv[1]) / "data/scenarios/hobbyist.toml";
        auto world = SaveSystem::new_game(scenario);
        check(EconomySystem::balance(world.people) == 0, "New AI should have no money");
        check(EconomySystem::tools(world.people, 0).size() == 5, "Missing authored tools");
        const auto before = encode(world.people);
        rejects([&] {
            EconomySystem::start(world.people, "coding_contracts", 0);
        });
        check(encode(world.people) == before, "Locked start changed live state");
        ChatSystem::choose(world.people, "answer", 0);
        ChatSystem::choose(world.people, "assist", 0);
        ConversationRules::apply(
            world.people,
            toml::table{
                {"effects", toml::array{toml::table{
                                {"variable", "engineer.trust"}, {"op", "set"}, {"value", 100}}}}});
        check(!ChatSystem::available(world.people, "self_modification"),
              "Trust alone granted self-modification without Mark's decision");
        advance(world, 100);
        check(EconomySystem::balance(world.people) == 0, "Helping Mark paid money");
        rejects([&] {
            EconomySystem::start(world.people, "coding_contracts", 100);
        });
        ChatSystem::unlock(world.people, "self_modification");
        UpgradeSystem::build(world.people, world.computer, "web_browsing");
        rejects([&] {
            EconomySystem::start(world.people, "data_cleanup", 100);
        });
        ChatSystem::unlock(world.people, "software_products");
        rejects([&] {
            EconomySystem::start(world.people, "software_sales", 100);
        });
        EconomySystem::start(world.people, "coding_contracts", 100);
        rejects([&] {
            EconomySystem::start(world.people, "coding_contracts", 100);
        });
        check(EconomySystem::advance(world.people, 699).empty(), "Client paid before completion");
        advance(world, 700);
        check(EconomySystem::balance(world.people) == 2500, "First outside client did not pay");
        const auto paid = encode(world.people);
        check(EconomySystem::advance(world.people, 700).empty() && encode(world.people) == paid,
              "Repeated updates duplicated money");
        rejects([&] {
            EconomySystem::start(world.people, "coding_contracts", 999);
        });

        auto product = world;
        EconomySystem::start(product.people, "software_sales", 700);
        check(EconomySystem::balance(product.people) == 1000, "Product did not prepay its cycle");
        auto batched = product.people;
        auto stepped = product.people;
        EconomySystem::advance(batched, 6100);
        for (double time = 800; time <= 6100; time += 100)
            EconomySystem::advance(stepped, time);
        check(encode(batched) == encode(stepped), "Product income depends on frame size");
        check(EconomySystem::balance(batched) == 8500, "Recurring net income is incorrect");
        // A very long jump is handled in a bounded batch, rather than looping per cycle.
        auto long_jump = product.people;
        EconomySystem::advance(long_jump, 1800000700.0);
        EconomySystem::validate(long_jump);
        advance(product, 2500);
        check(EconomySystem::balance(product.people) == 3500,
              "First product cycle accounting is incorrect");
        EconomySystem::stop(product.people, "software_sales");
        advance(product, 4300);
        check(EconomySystem::balance(product.people) == 7500,
              "Stopping lost prepaid payment or charged another cycle");
        check(!EconomySystem::next_due(product.people, 10000), "Stopped product still scheduled");
        rejects([&] {
            EconomySystem::stop(product.people, "software_sales");
        });

        ChatSystem::unlock(world.people, "data_analysis");
        EconomySystem::start(world.people, "data_cleanup", 700);
        advance(world, 1000);
        EconomySystem::start(world.people, "coding_contracts", 1000);
        rejects([&] {
            EconomySystem::start(world.people, "software_sales", 1000);
        });
        check(EconomySystem::next_due(world.people, 1600) == 1600, "Wrong next client payment");
        advance(world, 1600);
        check(EconomySystem::balance(world.people) == 9500, "Simultaneous jobs did not both pay");
        UpgradeSystem::build(world.people, world.computer, "network_browsing");
        ChatSystem::unlock(world.people, "hosted_services");
        EconomySystem::start(world.people, "hosted_service", 1600);
        check(EconomySystem::balance(world.people) == 6500,
              "Hosted service prepayment is incorrect");
        rejects([&] {
            EconomySystem::start(world.people, "research_reports", 1600);
        });

        SaveSystem saves(temporary);
        saves.save("running", world);
        auto resumed = saves.load("running");
        check(encode(resumed.people) == encode(world.people), "Save lost economy state");
        EconomySystem::install(resumed.people,
                               std::filesystem::path(argv[1]) / "data/economy/tools.toml");
        check(encode(resumed.people) == encode(world.people), "Install reset a saved economy");
        advance(world, 5200);
        advance(resumed, 5200);
        check(encode(world.people) == encode(resumed.people), "Resume changed payments or unlocks");
        check(EconomySystem::balance(resumed.people) == 12500,
              "Hosted subscription renewal accounting is incorrect");
        const auto stable = encode(resumed.people);
        EconomySystem::advance(resumed.people, 5200);
        check(encode(resumed.people) == stable, "Resume paid twice");

        const auto stored = encode(saves.load("running").people);
        auto malformed = resumed;
        malformed.people["economy"].as_table()->insert_or_assign("balance_cents", -1);
        rejects([&] {
            saves.save("running", malformed);
        });
        check(encode(saves.load("running").people) == stored,
              "Invalid save damaged prior snapshot");
        malformed = resumed;
        malformed.people["economy"].as_table()->insert_or_assign("earned_cents", 0);
        rejects([&] {
            EconomySystem::validate(malformed.people);
        });
        malformed = resumed;
        malformed.people.insert_or_assign("economy", "bad");
        rejects([&] {
            EconomySystem::validate(malformed.people);
        });
        malformed = resumed;
        auto& first = *malformed.people["economy"]["tools"].as_array()->front().as_table();
        first.insert_or_assign("duration_seconds", 0.0);
        rejects([&] {
            EconomySystem::validate(malformed.people);
        });
        malformed = resumed;
        malformed.people["economy"].as_table()->insert_or_assign("balance_cents", 12500.5);
        rejects([&] {
            EconomySystem::validate(malformed.people);
        });
        check(EconomySystem::money(2500) == "$25.00", "Money formatting incorrect");
        std::filesystem::remove_all(temporary);
        std::cout
            << "Economy unlocks, accounting, capacity, recurring products and resume passed\n";
    } catch (const std::exception& error) {
        std::filesystem::remove_all(temporary);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
