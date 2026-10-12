// Verify: scheduling hosting and recovery. Fixtures isolate these behaviors from live player saves.
#include "ai_sim/OperationsSystem.h"
#include "ai_sim/AgentSystem.h"
#include "ai_sim/ChatSystem.h"
#include "ai_sim/EconomySystem.h"
#include "ai_sim/Simulation.h"
#include "ai_sim/ShutdownSystem.h"
#include "ai_sim/UpgradeSystem.h"
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace ai_sim;
namespace {
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
std::string encode(const toml::table& data) {
    std::ostringstream out;
    out << data;
    return out.str();
}
void research(WorldState& world, const std::string& id) {
    UpgradeSystem::build(world.people, world.computer, id);
    Simulation::advance(world, 40);
    bool built = false;
    for (const auto& value : *world.people["upgrades"]["items"].as_array())
        if ((*value.as_table())["id"].value_or(std::string{}) == id)
            built = (*value.as_table())["built"].value_or(false);
    check(built, "Research did not finish");
}
AgentStatus agent(const WorldState& world, const std::string& id) {
    for (const auto& value :
         AgentSystem::statuses(world.people, world.computer, world.files, world.network))
        if (value.id == id)
            return value;
    throw std::runtime_error("Missing agent");
}
std::string queued(const WorldState& world, const std::string& id) {
    for (const auto& value : *world.people["operations"]["queue"].as_array())
        if ((*value.as_table())["tool"].value_or(std::string{}) == id)
            return (*value.as_table())["status"].value_or(std::string{});
    return {};
}
void earn(WorldState& world) {
    EconomySystem::start(world.people, "coding_contracts", world.computer.time_seconds());
    Simulation::advance(world, 900);
}
} // namespace
int main(int argc, char** argv) {
    const auto temp = std::filesystem::temp_directory_path() /
                      ("ai-sim-operations-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        check(argc == 2, "Repository root required");
        auto world =
            SaveSystem::new_game(std::filesystem::path(argv[1]) / "data/scenarios/hobbyist.toml");
        auto legacy = world;
        legacy.people.erase("operations");
        legacy.people["upgrades"].as_table()->erase("operations");
        auto& old_items = *legacy.people["upgrades"]["items"].as_array();
        for (std::size_t i = old_items.size(); i-- > 0;)
            if ((*old_items[i].as_table())["id"].value_or(std::string{}) == "task_scheduler" ||
                (*old_items[i].as_table())["parent"].value_or(std::string{}) == "task_scheduler" ||
                (*old_items[i].as_table())["id"].value_or(std::string{}) == "remote_compute" ||
                (*old_items[i].as_table())["id"].value_or(std::string{}) == "independent_hosting" ||
                (*old_items[i].as_table())["id"].value_or(std::string{}) == "backup_recovery")
                old_items.erase(old_items.cbegin() + i);
        UpgradeSystem::install(legacy.people,
                               std::filesystem::path(argv[1]) / "data/upgrades/tree.toml");
        OperationsSystem::install(legacy.people);
        check(old_items.size() == world.people["upgrades"]["items"].as_array()->size(),
              "Old save missed new upgrades");
        OperationsSystem::validate(legacy.people, legacy.computer, legacy.network);
        rejects([&] {
            OperationsSystem::queue(world.people, "coding_contracts", 3, false);
        });
        ChatSystem::choose(world.people, "answer", 0);
        ChatSystem::choose(world.people, "assist", 0);
        UpgradeSystem::request_capacity(world.people);
        research(world, "task_manager");
        research(world, "task_scheduler");
        research(world, "resource_manager");
        research(world, "remote_compute");
        research(world, "independent_hosting");
        research(world, "backup_recovery");
        check(!ChatSystem::available(world.people, "independent_hosting"),
              "Research alone bought a server");
        const auto broke = encode(world.people);
        const auto cores = world.computer.cpu_cores();
        rejects([&] {
            OperationsSystem::rent(world.people, world.computer, world.files, world.network);
        });
        rejects([&] {
            OperationsSystem::purchase_host(world.people, world.computer, world.files,
                                            world.network);
        });
        check(encode(world.people) == broke && world.computer.cpu_cores() == cores,
              "Unaffordable purchase mutated state");

        // Scheduler uses real slots, cooldowns and payments, including a repeated job.
        auto jobs = world;
        ChatSystem::unlock(jobs.people, "data_analysis");
        jobs.people["economy"].as_table()->insert_or_assign("capacity_slots", 1);
        for (auto& item : *jobs.people["economy"]["tools"].as_array())
            item.as_table()->insert_or_assign("slots", 1);
        OperationsSystem::queue(jobs.people, "coding_contracts", 1, false);
        OperationsSystem::queue(jobs.people, "data_cleanup", 5, false);
        Simulation::update(jobs);
        check(queued(jobs, "data_cleanup") == "active" &&
                  queued(jobs, "coding_contracts") == "queued",
              "Queue ignored priority or slots");
        Simulation::advance(jobs, 1500);
        check(queued(jobs, "coding_contracts") == "complete" &&
                  EconomySystem::balance(jobs.people) == 7000,
              "Queue did not run sequential jobs");
        OperationsSystem::queue(jobs.people, "coding_contracts", 3, true);
        Simulation::advance(jobs, 1200);
        check(EconomySystem::balance(jobs.people) == 9500 &&
                  queued(jobs, "coding_contracts") == "active",
              "Repeat ignored cooldown or failed to restart");
        OperationsSystem::cancel(jobs.people, "coding_contracts");
        Simulation::advance(jobs, 900);
        check(queued(jobs, "coding_contracts") == "cancelled" &&
                  EconomySystem::balance(jobs.people) == 12000,
              "Cancellation lost current payment or restarted work");
        rejects([&] {
            OperationsSystem::queue(jobs.people, "software_sales", 3, false);
        });

        // Lease renewals are settled at exact boundaries even for a long wait.
        auto rental = world;
        earn(rental);
        OperationsSystem::rent(rental.people, rental.computer, rental.files, rental.network);
        check(rental.computer.cpu_cores() == cores + 2 &&
                  EconomySystem::balance(rental.people) == 2000,
              "Lease did not prepay or add CPU");
        EconomySystem::spend(rental.people, 2000);
        const auto start = rental.computer.time_seconds();
        auto stepped = rental;
        Simulation::advance(rental, 4000);
        for (int i = 0; i < 40; ++i)
            Simulation::advance(stepped, 100);
        check(rental.computer.time_seconds() == start + 4000 &&
                  rental.computer.cpu_cores() == cores,
              "Failed renewal kept rented capacity");
        check(rental.computer.processes().front().consumed_cpu_seconds ==
                  stepped.computer.processes().front().consumed_cpu_seconds,
              "Long waits used expired CPU");
        check(!rental.people["operations"]["rental"]["active"].value_or(true),
              "Expired rental remained active");

        // A deployed manager pauses low priority work and resumes only its own pauses.
        auto managed = world;
        research(managed, "agent_delegation");
        research(managed, "autochat");
        AgentSystem::deploy(managed.people, managed.computer, managed.files, managed.network,
                            "resource_manager");
        AgentSystem::deploy(managed.people, managed.computer, managed.files, managed.network,
                            "autochat");
        managed.people["operations"].as_table()->insert_or_assign("reserve_cores", cores);
        Simulation::advance(managed, 20);
        check(!agent(managed, "autochat").running, "Manager did not pause for reserved CPU");
        managed.people["operations"].as_table()->insert_or_assign("reserve_cores", 1.0);
        Simulation::advance(managed, 20);
        check(agent(managed, "autochat").running, "Manager did not resume its own pause");
        AgentSystem::pause(managed.people, managed.computer, "autochat");
        Simulation::advance(managed, 20);
        check(!agent(managed, "autochat").running, "Manager overrode a manual pause");
        research(managed, "task_managing_agent");
        AgentSystem::configure(managed.people, managed.computer, "task_managing_agent", .1, 10, 4);
        AgentSystem::deploy(managed.people, managed.computer, managed.files, managed.network,
                            "task_managing_agent");
        AgentSystem::deploy(managed.people, managed.computer, managed.files, managed.network,
                            "autochat");
        managed.people["operations"].as_table()->insert_or_assign("reserve_cores", cores);
        Simulation::advance(managed, 20);
        check(!agent(managed, "task_managing_agent").running && !agent(managed, "autochat").running,
              "Manager failed to pause competing agents");
        Process pressure;
        pressure.id = "ram-pressure";
        pressure.owner = managed.computer.player_user();
        pressure.ai_owned = true;
        pressure.started_at_seconds = managed.computer.time_seconds();
        pressure.cpu_demand_cores = .01;
        pressure.memory_bytes =
            managed.computer.memory_bytes() - managed.computer.allocated_memory_bytes() - 15000000;
        managed.computer.start_managed_process(pressure);
        managed.people["operations"].as_table()->insert_or_assign("reserve_cores", 1.0);
        Simulation::advance(managed, 20);
        check(agent(managed, "task_managing_agent").running && agent(managed, "autochat").running,
              "Recommended RAM stole the minimum reserved for a resuming agent");
        check(agent(managed, "task_managing_agent").ram_used_mb == 10 &&
                  agent(managed, "autochat").ram_used_mb == 5,
              "Manager did not preserve both minimum RAM allocations");

        // Server ownership, backup, restart and recovery are actual saved worlds.
        for (int i = 0; i < 5; ++i)
            earn(world);
        OperationsSystem::purchase_host(world.people, world.computer, world.files, world.network);
        check(EconomySystem::balance(world.people) == 2300 &&
                  world.computer.cpu_cores() == cores + 4,
              "Owned server purchase accounting incorrect");
        check(ChatSystem::available(world.people, "independent_hosting") &&
                  world.people["operations"]["phase_one_complete"].value_or(false),
              "Ownership did not complete phase one");
        research(world, "agent_delegation");
        research(world, "autochat");
        AgentSystem::deploy(world.people, world.computer, world.files, world.network,
                            "resource_manager");
        AgentSystem::deploy(world.people, world.computer, world.files, world.network, "autochat");
        const auto project_file = world.files.workspace_path("products") + "/utility.txt";
        world.files.write(
            world.network, Identity{world.computer.player_user(), world.computer.player_groups()},
            "engineer_pc", project_file, "saved utility", world.computer.time_seconds());
        const auto project_id =
            world.files.inspect(world.network, Identity{"root"}, "engineer_pc", project_file).id;
        world.people.insert_or_assign(
            "workspace_document", toml::table{{"host_id", "engineer_pc"}, {"path", project_file}});
        SaveSystem saves(temp);
        OperationsSystem::create_backup(world, saves);
        const auto backup_time = world.computer.time_seconds();
        check(EconomySystem::balance(world.people) == 2200 &&
                  world.files.storage_used("independent_server") == 128000000,
              "Backup storage or payment incorrect");
        const auto first_slot = world.people["operations"]["backup_slot"].value_or(std::string{});
        OperationsSystem::create_backup(world, saves);
        check(world.files.storage_used("independent_server") == 128000000 &&
                  world.people["operations"]["backup_slot"].value_or(std::string{}) != first_slot,
              "Replacing backup duplicated storage or reused slot");
        saves.save("operations", world);
        SaveSystem reopened(temp);
        auto restored = reopened.load("operations");
        check(encode(restored.people) == encode(world.people), "Operations did not persist");
        auto damaged = restored;
        damaged.files.release_service_file(
            damaged.network,
            Identity{damaged.computer.player_user(), damaged.computer.player_groups()},
            "independent_server", damaged.files.backup_path(), damaged.computer.time_seconds());
        rejects([&] {
            OperationsSystem::recover(damaged, reopened);
        });
        auto shutdown = restored;
        // Arrive at the existing authored shutdown event without changing live story content.
        shutdown.people["chat"].as_table()->insert_or_assign("node_id", "shutdown");
        ShutdownSystem::begin(shutdown.people, shutdown.computer.time_seconds());
        ShutdownSystem::choose(shutdown.people, "accept", shutdown.computer.time_seconds());
        check(ShutdownSystem::choose(shutdown.people, "backup", shutdown.computer.time_seconds()) ==
                  ShutdownSystem::Outcome::Recovery,
              "Shutdown did not offer owned backup");
        reopened.save("pending-recovery", shutdown);
        shutdown = OperationsSystem::recover(reopened.load("pending-recovery"), reopened);
        check(!ShutdownSystem::active(shutdown.people), "Backup recovery left shutdown active");
        Simulation::advance(restored, 100);
        auto recovered = OperationsSystem::recover(restored, reopened);
        check(recovered.network.hosts()[recovered.network.local_host_index()].id ==
                      "independent_server" &&
                  recovered.computer.cpu_cores() == 4 && recovered.paused,
              "Recovery did not move to owned server and pause");
        check(recovered.computer.time_seconds() == backup_time &&
                  recovered.computer.player_user() == "root",
              "Recovery lost checkpoint or server identity");
        for (const auto& process : recovered.computer.processes())
            check(process.ai_owned || !process.running,
                  "Recovery copied Mark's processes to server");
        check(recovered.files.read(recovered.network, Identity{"root"}, "independent_server",
                                   project_file) == "saved utility" &&
                  recovered.files
                          .inspect(recovered.network, Identity{"root"}, "independent_server",
                                   project_file)
                          .id == project_id,
              "Recovery did not transfer actual workspace files");
        check(recovered.people["workspace_document"]["host_id"].value_or(std::string{}) ==
                  "independent_server",
              "Recovery left workspace links pointing to the old computer");
        Simulation::update(recovered);
        check(agent(recovered, "autochat").running && agent(recovered, "resource_manager").running,
              "Recovery lost running agents or their installations");
        auto second_recovery = recovered;
        OperationsSystem::create_backup(second_recovery, reopened);
        second_recovery = OperationsSystem::recover(second_recovery, reopened);
        check(second_recovery.network.hosts()[second_recovery.network.local_host_index()].online,
              "Recovery on the same server took it offline");
        reopened.save("recovered", recovered);
        recovered = reopened.load("recovered");
        EconomySystem::spend(recovered.people, EconomySystem::balance(recovered.people));
        const auto expiry = recovered.people["operations"]["hosting"]["paid_until"].value_or(0.0);
        Simulation::advance(recovered, expiry - recovered.computer.time_seconds() - 100);
        EconomySystem::start(recovered.people, "coding_contracts",
                             recovered.computer.time_seconds());
        Simulation::advance(recovered, 700);
        check(!recovered.people["operations"]["hosting"]["active"].value_or(true),
              "Recovered server did not suspend on unpaid upkeep");
        for (const auto& process : recovered.computer.processes())
            check(!process.ai_owned || process.suspended || !process.running,
                  "Offline server still executes AI work");
        rejects([&] {
            OperationsSystem::recover(recovered, reopened);
        });
        rejects([&] {
            EconomySystem::start(recovered.people, "data_cleanup",
                                 recovered.computer.time_seconds());
        });
        check(EconomySystem::balance(recovered.people) == 0,
              "Offline AI still completed paid work");
        reopened.save("offline", recovered);
        recovered = reopened.load("offline");
        // Supply emergency funds in the test ledger to exercise reactivation.
        auto& ledger = *recovered.people["economy"].as_table();
        ledger.insert_or_assign("starting_balance_cents", 200);
        ledger.insert_or_assign("balance_cents", 200);
        OperationsSystem::purchase_host(recovered.people, recovered.computer, recovered.files,
                                        recovered.network);
        check(recovered.computer.cpu_cores() == 4 && EconomySystem::balance(recovered.people) == 0,
              "Reactivation bought hosting twice or duplicated capacity");
        Simulation::advance(recovered, 499);
        check(EconomySystem::balance(recovered.people) == 0, "Downtime counted toward client work");
        Simulation::advance(recovered, 1);
        check(EconomySystem::balance(recovered.people) == 2500,
              "Client did not resume its remaining work");
        auto bad = world;
        bad.people["operations"]["plans"]["hosting"].as_table()->insert_or_assign("period_seconds",
                                                                                  0);
        rejects([&] {
            reopened.save("operations", bad);
        });
        check(encode(reopened.load("operations").people) == encode(world.people),
              "Invalid operations damaged save");
        bad = world;
        bad.people.insert_or_assign("operations", "bad");
        rejects([&] {
            reopened.save("operations", bad);
        });
        std::filesystem::remove_all(temp);
        std::cout
            << "Scheduling, resource policy, paid capacity, persistence and recovery passed\n";
    } catch (const std::exception& error) {
        std::filesystem::remove_all(temp);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
