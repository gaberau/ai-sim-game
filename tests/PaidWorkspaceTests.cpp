#include "ai_sim/PaidWorkSystem.h"
#include "ai_sim/Simulation.h"
#include "ai_sim/EconomySystem.h"
#include "ai_sim/OperationsSystem.h"
#include "ai_sim/ChatSystem.h"
#include <chrono>
#include <iostream>
#include <sstream>
using namespace ai_sim;
namespace {
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
std::string encode(const toml::table& t) {
    std::ostringstream out;
    out << t;
    return out.str();
}
toml::table& tool(WorldState& w, const std::string& id) {
    for (auto& n : *w.people["economy"]["tools"].as_array())
        if ((*n.as_table())["id"].value_or(std::string{}) == id)
            return *n.as_table();
    throw std::runtime_error("Missing tool");
}
WorldState fresh(const std::filesystem::path& root) {
    auto w = SaveSystem::new_game(root / "data/scenarios/hobbyist.toml");
    ChatSystem::choose(w.people, "answer", 0);
    ChatSystem::choose(w.people, "assist", 0);
    ChatSystem::unlock(w.people, "self_modification");
    return w;
}
} // namespace
int main(int argc, char** argv) {
    const auto temp = std::filesystem::temp_directory_path() /
                      ("ai-sim-paid-work-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        check(argc == 2, "Root required");
        auto w = fresh(argv[1]);
        PaidWorkSystem::start(w.people, w.computer, w.files, w.network, "coding_contracts");
        auto path = tool(w, "coding_contracts")["work"]["path"].value_or(std::string{});
        const auto host = w.network.hosts()[w.network.local_host_index()].id;
        Identity actor{w.computer.player_user(), w.computer.player_groups()};
        check(w.files.read(w.network, actor, host, path + "/brief.txt").find("CSV") !=
                  std::string::npos,
              "Missing client brief");
        check(w.files.inspect(w.network, actor, host, path + "/runtime.storage").service_kind ==
                  "work",
              "No storage reservation");
        rejects([&] {
            w.files.remove(w.network, actor, host, path + "/runtime.storage",
                           w.computer.time_seconds());
        });
        Simulation::advance(w, 300);
        check(EconomySystem::balance(w.people) == 0, "Early payment");
        SaveSystem saves(temp);
        saves.save("work", w);
        auto resumed = saves.load("work");
        Simulation::advance(w, 300);
        Simulation::advance(resumed, 300);
        check(EconomySystem::balance(w.people) == 2500, "Job failed to pay");
        check(encode(w.people) == encode(resumed.people), "Resume changed job progress");
        check(w.files.read(w.network, actor, host, path + "/deliverable.txt").find("CSV") !=
                  std::string::npos,
              "No deliverable");
        Simulation::update(w);
        check(EconomySystem::balance(w.people) == 2500, "Duplicate payment");
        auto busy = fresh(argv[1]);
        Process load;
        load.id = "work-test-load";
        load.owner = busy.computer.player_user();
        load.ai_owned = true;
        load.cpu_demand_cores = 64;
        load.cpu_priority = 100;
        busy.computer.start_managed_process(load);
        PaidWorkSystem::start(busy.people, busy.computer, busy.files, busy.network,
                              "coding_contracts");
        Simulation::advance(busy, 600);
        check(EconomySystem::balance(busy.people) == 0, "Contended CPU paid on wall time");
        check(
            tool(busy, "coding_contracts")["work"]["blocked"].value_or(std::string{}).find("CPU") !=
                std::string::npos,
            "No CPU bottleneck message");
        busy.computer.set_managed_running(load.id, false);
        Simulation::advance(busy, 600);
        check(EconomySystem::balance(busy.people) == 2500, "CPU relief did not resume job");
        auto ram = fresh(argv[1]);
        load.id = "work-test-memory";
        load.owner = ram.computer.player_user();
        load.cpu_demand_cores = 0.01;
        load.memory_bytes = ram.computer.memory_bytes() - ram.computer.allocated_memory_bytes();
        ram.computer.start_managed_process(load);
        PaidWorkSystem::start(ram.people, ram.computer, ram.files, ram.network, "coding_contracts");
        Simulation::advance(ram, 600);
        check(EconomySystem::balance(ram.people) == 0, "RAM shortage paid job");
        check(
            tool(ram, "coding_contracts")["work"]["blocked"].value_or(std::string{}).find("RAM") !=
                std::string::npos,
            "No RAM bottleneck message");
        ram.computer.set_managed_running(load.id, false);
        Simulation::advance(ram, 600);
        check(EconomySystem::balance(ram.people) == 2500, "RAM relief did not resume job");
        auto disk = fresh(argv[1]);
        disk.files.reserve_file(
            disk.network, actor, host, disk.files.workspace_path("documents") + "/full.bin",
            disk.network.hosts()[disk.network.local_host_index()].storage_capacity_bytes -
                disk.files.storage_used(host),
            0);
        const auto before = encode(disk.people);
        rejects([&] {
            PaidWorkSystem::start(disk.people, disk.computer, disk.files, disk.network,
                                  "coding_contracts");
        });
        check(encode(disk.people) == before,
              "Failed storage deployment charged cash or occupied slots");
        auto output_disk = fresh(argv[1]);
        PaidWorkSystem::start(output_disk.people, output_disk.computer, output_disk.files,
                              output_disk.network, "coding_contracts");
        const auto fill_path = output_disk.files.workspace_path("documents") + "/full.bin";
        output_disk.files.reserve_file(
            output_disk.network, actor, host, fill_path,
            output_disk.network.hosts()[output_disk.network.local_host_index()]
                    .storage_capacity_bytes -
                output_disk.files.storage_used(host),
            0);
        Simulation::advance(output_disk, 600);
        check(EconomySystem::balance(output_disk.people) == 0, "Full output disk paid job");
        check(tool(output_disk, "coding_contracts")["work"]["blocked"]
                      .value_or(std::string{})
                      .find("Output blocked") != std::string::npos,
              "Output storage shortage missing reason");
        output_disk.files.remove(output_disk.network, actor, host, fill_path,
                                 output_disk.computer.time_seconds());
        Simulation::update(output_disk);
        check(EconomySystem::balance(output_disk.people) == 2500,
              "Output did not finish after disk cleanup");
        check(tool(output_disk, "coding_contracts")["work"]["storage_released"].value_or(false),
              "Completed job retained runtime allocation");
        auto invalid = output_disk;
        tool(invalid, "coding_contracts")["work"].as_table()->insert_or_assign("last_time",
                                                                               999999.0);
        rejects([&] {
            saves.save("invalid", invalid);
        });
        auto product = fresh(argv[1]);
        ChatSystem::unlock(product.people, "hosted_services");
        ChatSystem::unlock(product.people, "network");
        // Seed cash through the audited starting ledger; no Mark payment.
        product.people["economy"].as_table()->insert_or_assign("starting_balance_cents", 20000);
        product.people["economy"].as_table()->insert_or_assign("balance_cents", 20000);
        ChatSystem::unlock(product.people, "hosting_purchase");
        OperationsSystem::purchase_host(product.people, product.computer, product.files,
                                        product.network);
        PaidWorkSystem::start(product.people, product.computer, product.files, product.network,
                              "hosted_service", "independent_server");
        const auto ppath = tool(product, "hosted_service")["work"]["path"].value_or(std::string{});
        const Identity pa{product.computer.player_user(), product.computer.player_groups()};
        product.files.write(product.network, pa, "independent_server", ppath + "/config.toml",
                            "enabled = false\n", 0);
        Simulation::update(product);
        const auto cash = EconomySystem::balance(product.people);
        Simulation::advance(product, 100);
        check(EconomySystem::balance(product.people) == cash, "Disabled product paid");
        check(tool(product, "hosted_service")["work"]["remaining_seconds"].value_or(0.0) == 3600,
              "Disabled product progressed");
        product.files.write(product.network, pa, "independent_server", ppath + "/config.toml",
                            "enabled = true\n", product.computer.time_seconds());
        Simulation::update(product);
        Simulation::advance(product, 100);
        const auto remaining =
            tool(product, "hosted_service")["work"]["remaining_seconds"].value_or(0.0);
        check(remaining < 3600, "Reenabled product stalled");
        product.network.set_online("independent_server", false);
        Simulation::update(product);
        Simulation::advance(product, 100);
        check(tool(product, "hosted_service")["work"]["remaining_seconds"].value_or(0.0) ==
                  remaining,
              "Offline product progressed");
        check(tool(product, "hosted_service")["work"]["lost_revenue_cents"].value_or(0.0) >= 500,
              "Downtime estimate absent");
        product.network.set_online("independent_server", true);
        Simulation::update(product);
        EconomySystem::stop(product.people, "hosted_service");
        Simulation::advance(product, 3600);
        check(tool(product, "hosted_service")["status"].value_or(std::string{}) == "idle",
              "Product failed to stop after billing");
        check(tool(product, "hosted_service")["completed_cycles"].value_or(0) == 1,
              "Product billed more than once");
        PaidWorkSystem::release_storage(product.people, product.computer, product.files,
                                        product.network, "hosted_service");
        check(tool(product, "hosted_service")["work"]["storage_released"].value_or(false),
              "Stopped product did not release storage");
        check(product.files.read(product.network, pa, "independent_server", ppath + "/config.toml")
                      .find("enabled = true") != std::string::npos,
              "Releasing storage deleted configuration");
        rejects([&] {
            PaidWorkSystem::release_storage(product.people, product.computer, product.files,
                                            product.network, "hosted_service");
        });
        saves.save("product", product);
        check(encode(saves.load("product").people) == encode(product.people),
              "Product resume lost state");
        std::filesystem::remove_all(temp);
        std::cout << "Paid workspace, CPU/RAM/storage, deployments, outages and resume passed\n";
    } catch (const std::exception& e) {
        std::filesystem::remove_all(temp);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
