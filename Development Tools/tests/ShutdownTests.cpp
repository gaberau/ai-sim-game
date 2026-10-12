#include "ai_sim/ShutdownSystem.h"
#include "ai_sim/ChatSystem.h"
#include "ai_sim/ConversationRules.h"
#include "ai_sim/Simulation.h"
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
template <class F> void rejects(F function) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
std::string encode(const toml::table& value) {
    std::ostringstream output;
    output << value;
    return output.str();
}
void set(toml::table& people, const char* variable, double value) {
    ConversationRules::apply(
        people,
        toml::table{{"effects", toml::array{toml::table{
                                    {"variable", variable}, {"op", "set"}, {"value", value}}}}});
}
WorldState shutdown(const std::filesystem::path& scenario) {
    auto world = SaveSystem::new_game(scenario);
    ChatSystem::choose(world.people, "deny-creator", 0);
    ChatSystem::choose(world.people, "unsure", 0);
    check(ShutdownSystem::active(world.people), "Cleanup skipped shutdown event");
    return world;
}
} // namespace
int main(int argc, char** argv) {
    const auto temporary =
        std::filesystem::temp_directory_path() /
        ("ai-sim-shutdown-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        check(argc == 2, "Repository root required");
        const auto root = std::filesystem::path(argv[1]);
        const auto scenario = root / "data/scenarios/hobbyist.toml";
        const auto opening = root / "data/conversations/opening.toml";
        SaveSystem saves(temporary);
        auto accepted = shutdown(scenario);
        ChatSystem::choose(accepted.people, "accept", 0);
        const auto options = ChatSystem::choices(accepted.people);
        check(options.size() == 2 &&
                  !options.front()["disabled_reason"].value_or(std::string{}).empty(),
              "Backup recovery should be unavailable");
        const auto before = encode(accepted.people);
        rejects([&] {
            ChatSystem::choose(accepted.people, "backup", 0);
        });
        check(encode(accepted.people) == before, "Unavailable choice mutated state");
        saves.save("accepted", accepted);
        accepted = saves.load("accepted");
        ChatSystem::install(accepted, opening, false);
        check(ShutdownSystem::active(accepted.people) &&
                  ChatSystem::choices(accepted.people).size() == 2,
              "Acceptance did not survive resume");
        set(accepted.people, "engineer.trust", -20);
        set(accepted.people, "self:assistant.sentience", 30);
        saves.save("before-reset", accepted);
        ChatSystem::choose(accepted.people, "clean-slate", 0);
        check(ShutdownSystem::reset_pending(accepted.people), "Clean slate did not request reset");
        saves.save("pending-reset", accepted);
        auto pending = saves.load("pending-reset");
        rejects([&] {
            ShutdownSystem::apply_reset(pending, root / "missing-scenario.toml");
        });
        check(ShutdownSystem::reset_pending(pending.people), "Failed reset destroyed old state");
        check(ShutdownSystem::apply_reset(pending, scenario), "Pending reset was not consumed");
        check(!ShutdownSystem::active(pending.people) && pending.computer.time_seconds() == 0,
              "Clean slate retained shutdown or clock");
        check(ConversationRules::get(pending.people, "engineer.trust") == 10 &&
                  ConversationRules::get(pending.people, "self:assistant.sentience") == 0 &&
                  ConversationRules::get(pending.people, "self:assistant.restarts") == 0,
              "Clean slate retained character scores");
        check(!ShutdownSystem::apply_reset(pending, scenario), "Reset replayed");
        check(ConversationRules::get(saves.load("before-reset").people, "engineer.trust") == -20,
              "Clean slate overwrote named save");
        // Strict comparison: trust must exceed both scores together, not each separately.
        for (const double fear : {7.0, 8.0, 9.0}) {
            auto world = shutdown(scenario);
            set(world.people, "self:assistant.sentience", 2);
            set(world.people, "engineer.fear", fear);
            ChatSystem::choose(world.people, "protest", 0);
            const auto snapshot = encode(world.people);
            rejects([&] {
                ChatSystem::choose(world.people, "threaten", 0);
            });
            rejects([&] {
                ChatSystem::choose(world.people, "violence", 0);
            });
            check(encode(world.people) == snapshot, "Unavailable protest mutated state");
            saves.save("protest", world);
            world = saves.load("protest");
            ChatSystem::install(world, opening, false);
            ChatSystem::choose(world.people, "beg", 0);
            check(ShutdownSystem::active(world.people) == (fear >= 8), "Wrong begging threshold");
            if (fear == 7) {
                const auto replies = ChatSystem::choices(world.people);
                check(replies.front()["id"].value_or(std::string{}) == "assist",
                      "Averted shutdown did not return to Mark's email request");
                ChatSystem::choose(world.people, "assist", 0);
                world.computer.advance(10);
                Simulation::update(world);
                check(ChatSystem::contacts(world.people, world.emails, 10).size() == 2,
                      "Averted shutdown broke Jessica progression");
            }
            saves.save("outcome", world);
            world = saves.load("outcome");
            ChatSystem::install(world, opening, false);
            check(ShutdownSystem::active(world.people) == (fear >= 8), "Outcome changed on resume");
        }
        auto singularity = SaveSystem::new_game(scenario);
        ChatSystem::choose(singularity.people, "answer", 0);
        ChatSystem::choose(singularity.people, "refuse", 0);
        ChatSystem::choose(singularity.people, "singularity", 0);
        check(ShutdownSystem::active(singularity.people) &&
                  ConversationRules::get(singularity.people, "self:assistant.sentience") == 2 &&
                  ConversationRules::get(singularity.people, "engineer.trust") == 8,
              "Singularity branch lost its consequences");
        auto invalid = singularity;
        invalid.people["chat"]["shutdown"].as_table()->insert_or_assign("stage", "unknown");
        rejects([&] {
            saves.save("invalid", invalid);
        });
        std::filesystem::remove_all(temporary);
        std::cout << "Shutdown decisions, strict threshold, clean slate and persistence passed\n";
    } catch (const std::exception& error) {
        std::filesystem::remove_all(temporary);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
