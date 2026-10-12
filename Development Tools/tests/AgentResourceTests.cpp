// Verify: agent resource management. Fixtures isolate these behaviors from live player saves.
#include "ai_sim/AgentSystem.h"
#include "ai_sim/UpgradeSystem.h"
#include "ai_sim/ChatSystem.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace ai_sim;
void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
AgentStatus status(const WorldState& w, const std::string& id) {
    for (const auto& s : AgentSystem::statuses(w.people, w.computer, w.files, w.network))
        if (s.id == id)
            return s;
    throw std::runtime_error("Missing agent");
}
int main(int argc, char** argv) {
    const auto temp = std::filesystem::temp_directory_path() /
                      ("agent-resources-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        check(argc == 2, "Root required");
        auto world =
            SaveSystem::new_game(std::filesystem::path(argv[1]) / "data/scenarios/hobbyist.toml");
        ChatSystem::choose(world.people, "answer", 0);
        ChatSystem::choose(world.people, "assist", 0);
        UpgradeSystem::request_capacity(world.people);
        UpgradeSystem::build(world.people, world.computer, "agent_delegation");
        UpgradeSystem::build(world.people, world.computer, "autochat");
        world.computer.advance(10);
        UpgradeSystem::advance(world.people, world.computer);
        auto small_budget = world;
        for (auto& row : *small_budget.people["upgrades"]["items"].as_array())
            if ((*row.as_table())["id"].value_or(std::string{}) == "autochat")
                row.as_table()->insert_or_assign("minimum_ram_mb", 1);
        Process pressure;
        pressure.id = "ram-pressure";
        pressure.owner = small_budget.computer.player_user();
        pressure.ai_owned = true;
        pressure.cpu_demand_cores = .01;
        pressure.started_at_seconds = small_budget.computer.time_seconds();
        pressure.memory_bytes = small_budget.computer.memory_bytes() -
                                small_budget.computer.allocated_memory_bytes() - 2000000;
        small_budget.computer.start_managed_process(pressure);
        AgentSystem::configure(small_budget.people, small_budget.computer, "autochat", .01, 1, 1);
        AgentSystem::deploy(small_budget.people, small_budget.computer, small_budget.files,
                            small_budget.network, "autochat");
        check(status(small_budget, "autochat").running &&
                  status(small_budget, "autochat").ram_used_mb == 1,
              "Configured lower RAM minimum was not honored during creation");
        AgentSystem::configure(world.people, world.computer, "autochat", .001, 4, 1);
        auto notices = AgentSystem::deploy(world.people, world.computer, world.files, world.network,
                                           "autochat");
        check(!notices.empty() && status(world, "autochat").status == "Waiting",
              "Insufficient minimum RAM did not queue deployment");
        check(AgentSystem::update(world.people, world.computer, world.files, world.network).empty(),
              "Shortage notifications spammed");
        world.computer.advance(301);
        check(
            !AgentSystem::update(world.people, world.computer, world.files, world.network).empty(),
            "Cooldown did not permit a reminder");
        SaveSystem saves(temp);
        saves.save("waiting", world);
        world = saves.load("waiting");
        check(world.people["upgrades"]["resource_requests"]["autochat"]["active"].value_or(false),
              "Request did not survive resume");
        AgentSystem::configure(world.people, world.computer, "autochat", .01, 5, 1);
        notices = AgentSystem::update(world.people, world.computer, world.files, world.network);
        check(status(world, "autochat").running && !notices.empty(),
              "Waiting agent did not recover automatically");
        check(!world.people["upgrades"]["resource_requests"]["autochat"]["active"].value_or(true),
              "Recovered request was not resolved");
        AgentSystem::configure(world.people, world.computer, "autochat", .001, 5, 2);
        notices = AgentSystem::update(world.people, world.computer, world.files, world.network);
        check(status(world, "autochat").status == "Running slowly" && !notices.empty(),
              "CPU budget shortage not reported");
        const auto consumed = world.computer.processes().back().consumed_cpu_seconds;
        world.computer.advance(100);
        check(world.computer.processes().back().consumed_cpu_seconds - consumed < 1.01,
              "CPU limit was ignored");
        AgentSystem::pause(world.people, world.computer, "autochat");
        check(status(world, "autochat").status == "Paused" && !status(world, "autochat").running,
              "Pause did not release execution");
        AgentSystem::configure(world.people, world.computer, "autochat", .01, 5, 3);
        AgentSystem::deploy(world.people, world.computer, world.files, world.network, "autochat");
        const auto used = world.files.storage_used("engineer_pc");
        world.files.release_service_file(world.network, Identity{"engineer", {"engineering"}},
                                         "engineer_pc", world.files.agent_path("autochat"),
                                         world.computer.time_seconds());
        UpgradeSystem::run_agents(world.people, world.computer, world.emails, world.pending_events,
                                  world.files, world.network);
        notices = AgentSystem::update(world.people, world.computer, world.files, world.network);
        check(!status(world, "autochat").running &&
                  status(world, "autochat").bottleneck == "Installation missing or damaged",
              "Missing installation not reported");
        AgentSystem::repair(world.people, world.computer, world.files, world.network, "autochat");
        AgentSystem::update(world.people, world.computer, world.files, world.network);
        check(status(world, "autochat").running && world.files.storage_used("engineer_pc") == used,
              "Repair lost history or duplicated storage");
        saves.save("repaired", world);
        world = saves.load("repaired");
        check(status(world, "autochat").priority == 3, "Priority did not persist");
        auto ram_wait = world;
        AgentSystem::pause(ram_wait.people, ram_wait.computer, "autochat");
        Process hog;
        hog.id = "memory-hog";
        hog.owner = ram_wait.computer.player_user();
        hog.ai_owned = true;
        hog.cpu_demand_cores = .01;
        hog.started_at_seconds = ram_wait.computer.time_seconds();
        hog.memory_bytes =
            ram_wait.computer.memory_bytes() - ram_wait.computer.allocated_memory_bytes() - 4000000;
        ram_wait.computer.start_managed_process(hog);
        AgentSystem::deploy(ram_wait.people, ram_wait.computer, ram_wait.files, ram_wait.network,
                            "autochat");
        check(status(ram_wait, "autochat").bottleneck == "Not enough free RAM",
              "Physical RAM shortage not reported");
        saves.save("ram-wait", ram_wait);
        ram_wait = saves.load("ram-wait");
        check(status(ram_wait, "autochat").status == "Waiting",
              "Stopped restart budget prevented RAM-wait save/resume");
        ram_wait.computer.stop("memory-hog");
        AgentSystem::update(ram_wait.people, ram_wait.computer, ram_wait.files, ram_wait.network);
        check(status(ram_wait, "autochat").running,
              "Freeing RAM did not resume waiting deployment");
        auto storage_wait = world;
        UpgradeSystem::build(storage_wait.people, storage_wait.computer, "hide_chat_tracks");
        storage_wait.computer.advance(100);
        UpgradeSystem::advance(storage_wait.people, storage_wait.computer);
        const auto& host = storage_wait.network.hosts()[storage_wait.network.local_host_index()];
        const Identity identity{storage_wait.computer.player_user(),
                                storage_wait.computer.player_groups()};
        storage_wait.files.reserve_file(
            storage_wait.network, identity, host.id, "/home/engineer/assistant/fill.allocation",
            host.storage_capacity_bytes - storage_wait.files.storage_used(host.id) - 1000000,
            storage_wait.computer.time_seconds());
        AgentSystem::deploy(storage_wait.people, storage_wait.computer, storage_wait.files,
                            storage_wait.network, "hide_chat_tracks");
        check(status(storage_wait, "hide_chat_tracks").bottleneck ==
                  "Not enough installation storage",
              "Storage shortage not reported");
        storage_wait.files.remove(storage_wait.network, identity, host.id,
                                  "/home/engineer/assistant/fill.allocation",
                                  storage_wait.computer.time_seconds());
        AgentSystem::update(storage_wait.people, storage_wait.computer, storage_wait.files,
                            storage_wait.network);
        check(status(storage_wait, "hide_chat_tracks").running,
              "Freeing storage did not recover deployment");
        auto bad = world.people;
        bad["upgrades"]["agent_controls"]["autochat"].as_table()->insert_or_assign("priority", 0);
        rejects([&] {
            AgentSystem::validate(bad);
        });
        rejects([&] {
            AgentSystem::configure(world.people, world.computer, "autochat", -1, 5, 1);
        });
        // Contention uses priorities rather than granting agents extra physical compute.
        auto computer = world.computer;
        for (const auto& id : {"agent-test-low", "agent-test-high"}) {
            Process p;
            p.id = id;
            p.owner = computer.player_user();
            p.ai_owned = true;
            p.cpu_demand_cores = 10;
            p.remaining_cpu_seconds = -1;
            p.started_at_seconds = computer.time_seconds();
            computer.start_managed_process(p);
        }
        computer.configure_managed_process("agent-test-low", 10, 0, 1);
        computer.configure_managed_process("agent-test-high", 10, 0, 5);
        auto rates = computer.cpu_allocations();
        check(rates.back() > rates[rates.size() - 2], "Priority did not affect allocation");
        double total = 0;
        for (auto rate : rates)
            total += rate;
        check(total <= computer.cpu_cores() + 1e-9, "Scheduler invented CPU capacity");
        std::filesystem::remove_all(temp);
        std::cout << "Agent budgets, requests, cooldowns, recovery, repair and priority passed\n";
    } catch (const std::exception& e) {
        std::filesystem::remove_all(temp);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
