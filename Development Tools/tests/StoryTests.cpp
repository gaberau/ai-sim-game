// Verify: current story and retirement. Fixtures isolate these behaviors from live player saves.
#include "ai_sim/Simulation.h"
#include "ai_sim/ChatSystem.h"
#include "ai_sim/ConversationSystem.h"
#include "ai_sim/ConversationRules.h"
#include "ai_sim/WorkdaySystem.h"
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace ai_sim;
namespace {
const Identity actor{"engineer", {"engineering"}};
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void advance(WorldState& world, double time) {
    world.computer.advance(time - world.computer.time_seconds());
    Simulation::update(world);
}
void choose(WorldState& world, const std::string& id) {
    ConversationSystem::choose(world.people, world.emails, world.pending_events, world.files,
                               world.network, actor, "jessica", "jessica-day-one-chat-one", id,
                               world.computer.time_seconds());
}
void verify_story(const WorldState& world) {
    const auto* runs = world.people["chat"]["workday"]["runs"].as_array();
    check(runs && runs->size() == 1, "Retired conversations are active");
    check((*runs->front().as_table())["id"].value_or(std::string{}) == "jessica-day-one-chat-one",
          "Wrong conversation retained");
    check(world.emails["emails"].as_array()->empty(), "Retired office messages are active");
    check(!world.people.contains("phase_work") && !world.people.contains("delegation"),
          "Retired automatic work remains active");
    for (const auto& contact :
         ChatSystem::contacts(world.people, world.emails, world.computer.time_seconds()))
        check(contact.id == "engineer" || contact.id == "jessica", "Retired contact is visible");
}
} // namespace
int main(int argc, char** argv) {
    const auto temporary =
        std::filesystem::temp_directory_path() /
        ("ai-sim-story-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        check(argc == 2, "Repository root required");
        const auto root = std::filesystem::path(argv[1]);
        const auto opening = root / "data/conversations/opening.toml";
        for (const auto* branch : {"good", "bad", "weird"}) {
            auto world = SaveSystem::new_game(root / "data/scenarios/hobbyist.toml");
            verify_story(world);
            ChatSystem::choose(world.people, "answer", 0);
            ChatSystem::choose(world.people, "assist", 0);
            check(ChatSystem::contacts_visible(world.people), "Contacts cannot be navigated");
            advance(world, 10);
            choose(world, "improvise");
            choose(world, branch);
            check(ConversationSystem::choices(world.people, "jessica", world.files, world.network,
                                              actor, world.computer.time_seconds())
                      .empty(),
                  "Scripted reply still needs confirmation");
            advance(world, 10000);
            verify_story(world);
            SaveSystem saves(temporary);
            saves.save(branch, world);
            auto resumed = saves.load(branch);
            ChatSystem::install(resumed, opening, false);
            advance(resumed, 11000);
            verify_story(resumed);
        }
        // Exercise a real old save containing requests, pending access work,
        // reply consequences and follow-up graphs before switching its story.
        auto old = SaveSystem::new_game(
            root / "Development Tools/tests/fixtures/legacy-game/data/scenarios/hobbyist.toml");
        ChatSystem::choose(old.people, "answer", 0);
        ChatSystem::choose(old.people, "assist", 0);
        advance(old, 10);
        choose(old, "improvise");
        choose(old, "bad");
        choose(old, "send");
        advance(old, 60);
        WorkdaySystem::request_access(old.people, old.emails, old.pending_events, old.files,
                                      old.network, actor, "missing-document", 60);
        const auto lines = old.people["chat"]["transcript"].as_array()->size();
        const auto trust = ConversationRules::get(old.people, "engineer.trust");
        SaveSystem saves(temporary);
        saves.save("old", old);
        auto migrated = saves.load("old");
        ChatSystem::install(migrated, opening, false);
        verify_story(migrated);
        check(migrated.computer.time_seconds() == 60, "Migration reset clock");
        check(ConversationRules::get(migrated.people, "engineer.trust") == trust,
              "Migration reset trust");
        check(migrated.people["chat"]["transcript"].as_array()->size() == lines,
              "Migration shifted evidence identifiers");
        check(migrated.pending_events.empty(), "Retired events remain queued");
        ChatSystem::install(migrated, opening, false);
        advance(migrated, 10000);
        verify_story(migrated);
        saves.save("migrated", migrated);
        auto resumed = saves.load("migrated");
        ChatSystem::install(resumed, opening, false);
        verify_story(resumed);
        std::filesystem::remove_all(temporary);
        std::cout << "Current branches, old-save retirement and resume passed\n";
    } catch (const std::exception& error) {
        std::filesystem::remove_all(temporary);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
