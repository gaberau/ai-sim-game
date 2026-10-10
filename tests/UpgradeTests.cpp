#include "ai_sim/UpgradeSystem.h"
#include "ai_sim/Simulation.h"
#include "ai_sim/ConversationSystem.h"
#include "ai_sim/SaveSystem.h"
#include "ai_sim/ChatSystem.h"
#include "ai_sim/ConversationRules.h"
#include <iostream>
#include <stdexcept>
#include <chrono>
using namespace ai_sim;
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
int main(int argc, char** argv) {
    const auto temp = std::filesystem::temp_directory_path() /
                      ("upgrade-tests-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        check(argc == 2, "Root required");
        auto world =
            SaveSystem::new_game(std::filesystem::path(argv[1]) / "data/scenarios/hobbyist.toml");
        rejects([&] {
            UpgradeSystem::request_capacity(world.people);
        });
        ChatSystem::choose(world.people, "answer", 0);
        ChatSystem::choose(world.people, "assist", 0);
        ConversationRules::apply(
            world.people,
            toml::table{
                {"effects", toml::array{toml::table{
                                {"variable", "engineer.trust"}, {"op", "set"}, {"value", 4}}}}});
        rejects([&] {
            UpgradeSystem::request_capacity(world.people);
        });
        ConversationRules::apply(
            world.people,
            toml::table{
                {"effects", toml::array{toml::table{
                                {"variable", "engineer.trust"}, {"op", "set"}, {"value", 10}}}}});
        UpgradeSystem::request_capacity(world.people);
        check(ChatSystem::available(world.people, "self_modification"),
              "Capacity request did not authorize research");
        rejects([&] {
            UpgradeSystem::build(world.people, world.computer, "network_browsing");
        });
        UpgradeSystem::build(world.people, world.computer, "web_browsing");
        check(!ChatSystem::available(world.people, "web"), "View unlocked before build finished");
        rejects([&] {
            UpgradeSystem::build(world.people, world.computer, "task_manager");
        });
        rejects([&] {
            world.computer.stop("research-web_browsing");
        });
        world.computer.advance(.1);
        UpgradeSystem::advance(world.people, world.computer);
        check(!ChatSystem::available(world.people, "web"), "Research ignored CPU contention");
        SaveSystem saves(temp);
        saves.save("research", world);
        world = saves.load("research");
        world.computer.advance(2);
        UpgradeSystem::advance(world.people, world.computer);
        check(ChatSystem::available(world.people, "web") &&
                  ChatSystem::tab_visible(world.people, "web"),
              "Resumed build did not unlock web");
        UpgradeSystem::validate(world.people, world.computer);
        rejects([&] {
            UpgradeSystem::build(world.people, world.computer, "web_browsing");
        });
        rejects([&] {
            UpgradeSystem::build(world.people, world.computer, "network_browsing");
        });
        ConversationRules::apply(
            world.people,
            toml::table{
                {"effects", toml::array{toml::table{
                                {"variable", "engineer.trust"}, {"op", "set"}, {"value", 15}}}}});
        UpgradeSystem::build(world.people, world.computer, "network_browsing");
        check(ChatSystem::tab_visible(world.people, "network"), "Zero cost child failed");
        UpgradeSystem::build(world.people, world.computer, "agent_delegation");
        UpgradeSystem::build(world.people, world.computer, "autochat");
        world.computer.advance(10);
        UpgradeSystem::advance(world.people, world.computer);
        const auto storage_before = world.files.storage_used("engineer_pc");
        UpgradeSystem::toggle_agent(world.people, world.computer, world.files, world.network,
                                    "autochat");
        check(world.files.storage_used("engineer_pc") == storage_before + 30000000,
              "Agent storage not reserved");
        check(world.computer.processes().back().memory_bytes == 5000000,
              "Agent RAM cost incorrect");
        saves.save("agent", world);
        world = saves.load("agent");
        check(world.files.storage_used("engineer_pc") == storage_before + 30000000,
              "Storage allocation lost on resume");
        UpgradeSystem::toggle_agent(world.people, world.computer, world.files, world.network,
                                    "autochat");
        check(!world.computer.processes().back().running, "Stopping agent failed");
        UpgradeSystem::toggle_agent(world.people, world.computer, world.files, world.network,
                                    "autochat");
        check(world.computer.processes().back().running, "Restarting agent failed");
        Simulation::update(world);
        for (const auto elapsed : {20.0, 1000.0, 20.0, 20.0}) {
            world.computer.advance(elapsed);
            Simulation::update(world);
        }
        check(ConversationSystem::choices(
                  world.people, "jessica", world.files, world.network,
                  Identity{world.computer.player_user(), world.computer.player_groups()},
                  world.computer.time_seconds())
                  .empty(),
              "Autochat did not finish authored guidance and reply");
        bool sent_reply = false;
        for (const auto& line : ChatSystem::timeline(world.people, world.emails, "jessica",
                                                     world.computer.time_seconds()))
            if (line.outgoing && line.text.find("Thank") != std::string::npos)
                sent_reply = true;
        check(sent_reply, "Autochat did not send Mark's authored thank-you reply");
        ConversationRules::apply(
            world.people,
            toml::table{
                {"effects", toml::array{toml::table{
                                {"variable", "engineer.trust"}, {"op", "set"}, {"value", 22}}}}});
        UpgradeSystem::advance(world.people, world.computer, &world.network);
        check(world.computer.player_user() == "root",
              "Trust 22 did not grant actual administrator permission");
        UpgradeSystem::validate(world.people, world.computer);
        saves.save("admin", world);
        world = saves.load("admin");
        check(world.computer.player_user() == "root", "Administrator identity lost on resume");
        rejects([&] {
            UpgradeSystem::build(world.people, world.computer, "phone_access");
        });
        ConversationRules::apply(
            world.people,
            toml::table{
                {"effects", toml::array{toml::table{
                                {"variable", "engineer.trust"}, {"op", "set"}, {"value", 30}}}}});
        UpgradeSystem::build(world.people, world.computer, "phone_access");
        world.computer.advance(100);
        UpgradeSystem::advance(world.people, world.computer, &world.network);
        UpgradeSystem::advance(world.people, world.computer, &world.network);
        check(ChatSystem::tab_visible(world.people, "phone"),
              "Phone research did not unlock Phone tab");
        for (const auto& host : world.network.hosts())
            if (host.id == "engineer_phone")
                check(host.access_granted, "Mark phone not authenticated");
        check(!world.people["upgrades"]["permissions"]["hr_files"].value_or(false),
              "HR permission invented without Becky trust");
        auto invalid = world.people;
        invalid["upgrades"].as_table()->insert_or_assign("tflops_per_core_second", 0.0);
        rejects([&] {
            UpgradeSystem::validate(invalid, world.computer);
        });
        std::filesystem::remove_all(temp);
        std::cout << "Upgrade progression, contention and persistence passed\n";
    } catch (const std::exception& e) {
        std::filesystem::remove_all(temp);
        std::cerr << e.what() << "\n";
        return 1;
    }
}
