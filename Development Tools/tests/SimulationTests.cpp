// Verify: simulation progression. Fixtures isolate these behaviors from live player saves.
#include "ai_sim/Simulation.h"
#include "ai_sim/UpgradeSystem.h"
#include "ai_sim/StockMarketSystem.h"
#include "ai_sim/ChatSystem.h"
#include "ai_sim/ConversationSystem.h"
#include "ai_sim/WorkdaySystem.h"
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace ai_sim;
namespace {
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::string encode(const toml::node& value) {
    std::ostringstream output;
    if (const auto* table = value.as_table())
        output << *table;
    else if (const auto* array = value.as_array())
        output << *array;
    return output.str();
}
void legacy_update(WorldState& world) {
    if (!ChatSystem::connected(world.people))
        return;
    UpgradeSystem::advance(world.people, world.computer, &world.network);
    const Identity actor{world.computer.player_user(), world.computer.player_groups()};
    WorkdaySystem::advance(world.people, world.emails, world.pending_events, world.files,
                           world.network, world.computer.time_seconds(), actor);
    ChatSystem::discover(world.people, world.emails, world.computer.time_seconds());
    ConversationSystem::update(world.people, world.emails, world.files, world.network, actor,
                               world.computer.time_seconds());
    StockMarketSystem::advance(world.people, world.computer.time_seconds());
}
} // namespace
int main(int argc, char** argv) {
    const auto temporary =
        std::filesystem::temp_directory_path() /
        ("ai-sim-controller-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        check(argc == 2, "Repository root required");
        auto world =
            SaveSystem::new_game(std::filesystem::path(argv[1]) / "data/scenarios/hobbyist.toml");
        const auto opening = encode(world.people);
        const auto disconnected = Simulation::update(world);
        check(disconnected.events.empty() && disconnected.messages.empty(),
              "Disconnected opening emitted updates");
        check(encode(world.people) == opening, "Disconnected opening changed");
        ChatSystem::choose(world.people, "answer", 0);
        ChatSystem::choose(world.people, "assist", 0);
        auto legacy = world;
        bool discovered = false;
        for (const double time : {9.0, 10.0, 60.0, 1200.0, 1800.0, 3600.0}) {
            world.computer.advance(time - world.computer.time_seconds());
            legacy.computer.advance(time - legacy.computer.time_seconds());
            const auto result = Simulation::update(world);
            legacy_update(legacy);
            check(encode(world.people) == encode(legacy.people), "People progression changed");
            check(encode(world.emails) == encode(legacy.emails), "Message progression changed");
            check(encode(world.pending_events) == encode(legacy.pending_events),
                  "Pending event progression changed");
            for (const auto event : result.events) {
                if (event == SimulationEvent::JessicaDiscovered) {
                    check(!discovered && time == 10,
                          "Discovery notification duplicated or mistimed");
                    discovered = true;
                }
            }
            check(Simulation::update(world).events.empty(),
                  "Repeated update duplicated notification");
        }
        check(discovered, "Jessica discovery notification missing");
        SaveSystem saves(temporary);
        saves.save("controller", world);
        auto restored = saves.load("controller");
        Simulation::update(restored);
        Simulation::update(world);
        check(encode(restored.people) == encode(world.people), "Resume changed simulation state");
        std::filesystem::remove_all(temporary);
        std::cout << "Simulation progression, notifications and resume passed\n";
    } catch (const std::exception& error) {
        std::filesystem::remove_all(temporary);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
