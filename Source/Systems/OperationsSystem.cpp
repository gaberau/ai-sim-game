#include "ai_sim/OperationsSystem.h"
#include "ai_sim/AgentSystem.h"
#include "ai_sim/ChatSystem.h"
#include "ai_sim/EconomySystem.h"
#include "ai_sim/PaidWorkSystem.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
namespace ai_sim {
namespace {
void relocate_references(toml::node& node, const std::string& source, const std::string& target,
                         const std::string& root) {
    if (auto* table = node.as_table()) {
        const auto path = (*table)["path"].value_or(std::string{});
        if ((*table)["host_id"].value_or(std::string{}) == source &&
            (path == root || path.starts_with(root + "/")))
            table->insert_or_assign("host_id", target);
        for (auto& [key, value] : *table)
            relocate_references(value, source, target, root);
    } else if (auto* array = node.as_array())
        for (auto& value : *array)
            relocate_references(value, source, target, root);
}
void require(bool ok, const std::string& message) {
    if (!ok)
        throw std::runtime_error("Operations: " + message);
}
toml::table& state(toml::table& people) {
    auto* operations = people["operations"].as_table();
    require(operations, "Unavailable in this scenario");
    return *operations;
}
const toml::table& plan(const toml::table& people, const std::string& id) {
    const auto* operations = people["operations"]["plans"][id].as_table();
    require(operations, "Unknown infrastructure plan");
    return *operations;
}
void permitted(const toml::table& people, const std::string& capability) {
    require(ChatSystem::connected(people) && ChatSystem::available(people, capability),
            "Research " + capability + " first");
}
void capacity(Computer& computer, const toml::table& config, int sign) {
    computer.adjust_resource_pool(sign * config["cores"].value_or(0),
                                  sign * config["ram_mb"].value_or(std::int64_t{0}) * 1000000);
}
void activate(toml::table& people, Computer& computer, FileSystem& files, Network& network,
              const std::string& id) {
    const auto config = plan(people, id);
    auto& service = *state(people)[id].as_table();
    require(!service["active"].value_or(false), "Service is already active");
    const bool owned = service["owned"].value_or(false);
    const auto upfront = config["period_cost_cents"].value_or(std::int64_t{0}) +
                         (owned ? 0 : config["purchase_cents"].value_or(std::int64_t{0}));
    EconomySystem::spend(people, upfront);
    // Cash updates replace the people table; reacquire mutable state afterward.
    auto& updated = *state(people)[id].as_table();
    const auto host_id = config["host_id"].value_or(std::string{});
    const bool exists =
        std::any_of(network.hosts().begin(), network.hosts().end(), [&](const auto& h) {
            return h.id == host_id;
        });
    require(!exists || owned, "Configured server identity is already in use");
    if (!exists) {
        NetworkHost host;
        host.id = host_id;
        host.name = config["label"].value_or(id);
        host.address = "https://" + host_id + ".sim";
        host.kind = "server";
        host.description =
            id == "hosting" ? "Your independently owned server" : "Prepaid remote compute capacity";
        host.known = host.online = host.access_granted = true;
        host.ai_present = id == "hosting";
        host.x = network.hosts()[network.local_host_index()].x + (id == "hosting" ? 25 : 15);
        host.y = network.hosts()[network.local_host_index()].y + 8;
        host.storage_capacity_bytes = config["storage_mb"].value_or(std::uint64_t{0}) * 1000000;
        std::string uplink = network.hosts()[network.local_host_index()].id;
        if (std::any_of(network.hosts().begin(), network.hosts().end(), [](const auto& h) {
                return h.id == "isp_gateway";
            }))
            uplink = "isp_gateway";
        network.add_owned_host(std::move(host), uplink);
        files.mount_host(network, host_id, computer.player_user(), computer.time_seconds());
    } else
        network.set_online(host_id, true);
    if (id == "hosting" && state(people)["recovered"].value_or(false)) {
        computer.set_execution_available(true);
        // Jobs cannot make progress while their primary server is offline.
        const auto downtime =
            std::max(0.0, computer.time_seconds() -
                              updated["suspended_at"].value_or(computer.time_seconds()));
        if (auto* tools = people["economy"]["tools"].as_array())
            for (auto& row : *tools) {
                auto& tool = *row.as_table();
                if (const auto due = tool["due_seconds"].value<double>())
                    tool.insert_or_assign("due_seconds", *due + downtime);
                const auto cooldown = tool["cooldown_until_seconds"].value_or(0.0);
                if (cooldown > updated["suspended_at"].value_or(computer.time_seconds()))
                    tool.insert_or_assign("cooldown_until_seconds", cooldown + downtime);
            }
        updated.erase("suspended_at");
    } else
        capacity(computer, config, 1);
    updated.insert_or_assign("active", true);
    updated.insert_or_assign("owned", true);
    updated.insert_or_assign("paid_until",
                             computer.time_seconds() + config["period_seconds"].value_or(3600.0));
    if (id == "hosting") {
        state(people).insert_or_assign("phase_one_complete", true);
        ChatSystem::unlock(people, "independent_hosting");
    }
}
void balance_agents(toml::table& people, Computer& computer, FileSystem& files, Network& network,
                    std::vector<std::string>& messages) {
    const auto manager = std::find_if(
        computer.processes().begin(), computer.processes().end(), [](const auto& proc) {
            return proc.id == "agent-resource_manager" && proc.running && !proc.suspended;
        });
    if (manager == computer.processes().end())
        return;
    const auto consumed = manager->consumed_cpu_seconds;
    if (consumed - state(people)["manager_work"].value_or(0.0) <
        people["upgrades"]["resources"]["action_cpu_seconds"].value_or(1.0))
        return;
    state(people).insert_or_assign("manager_work", consumed);
    auto statuses = AgentSystem::statuses(people, computer, files, network);
    std::stable_sort(statuses.begin(), statuses.end(), [](const auto& a, const auto& b) {
        return a.priority > b.priority;
    });
    auto paused = *state(people)["auto_paused"].as_array();
    const double cpu_budget =
        std::max(0.0, computer.cpu_cores() - state(people)["reserve_cores"].value_or(1.0));
    double admitted_cpu = manager->demand_at(0);
    std::uint64_t protected_ram = 0;
    for (const auto& proc : computer.processes())
        if (proc.running && (!proc.id.starts_with("agent-") || proc.id == "agent-resource_manager"))
            protected_ram += proc.memory_bytes;
    double ram_left =
        (computer.memory_bytes() - std::min(computer.memory_bytes(), protected_ram)) / 1000000.0;
    std::vector<AgentStatus> admitted;
    for (const auto& agent : statuses) {
        if (!agent.deployed || agent.id == "resource_manager")
            continue;
        const auto* ctl = people["upgrades"]["agent_controls"][agent.id].as_table();
        if (!ctl)
            continue;
        const bool managed_pause =
            std::any_of(paused.begin(), paused.end(), [&](const auto& value) {
                return value.template value_or(std::string{}) == agent.id;
            });
        const auto mode = (*ctl)["mode"].value_or(std::string{});
        if (!(mode == "active" || (managed_pause && mode == "paused")) || agent.limit_tflops <= 0 ||
            agent.ram_mb < agent.minimum_ram_mb)
            continue;
        const double cores =
            agent.limit_tflops / people["upgrades"]["tflops_per_core_second"].value_or(0.1);
        if (admitted_cpu + cores > cpu_budget || ram_left < agent.minimum_ram_mb) {
            AgentSystem::pause(people, computer, agent.id);
            if (!managed_pause) {
                paused.push_back(agent.id);
                messages.push_back("Resource Manager paused " + agent.label +
                                   " for higher-priority work.");
            }
            continue;
        }
        admitted_cpu += cores;
        ram_left -= agent.minimum_ram_mb;
        admitted.push_back(agent);
    }
    // First give all admitted agents their minimum RAM, then distribute spare RAM by priority.
    for (const auto& agent : admitted)
        AgentSystem::configure(people, computer, agent.id, agent.limit_tflops,
                               static_cast<std::int64_t>(agent.minimum_ram_mb), agent.priority);
    for (const auto& agent : admitted) {
        const bool managed_pause =
            std::any_of(paused.begin(), paused.end(), [&](const auto& value) {
                return value.template value_or(std::string{}) == agent.id;
            });
        if (managed_pause || !agent.running) {
            const auto output = AgentSystem::deploy(people, computer, files, network, agent.id);
            messages.insert(messages.end(), output.begin(), output.end());
            for (std::size_t i = paused.size(); i-- > 0;)
                if (paused[i].value_or(std::string{}) == agent.id)
                    paused.erase(paused.cbegin() + i);
            if (managed_pause)
                messages.push_back("Resource Manager resumed " + agent.label + ".");
        }
    }
    for (const auto& agent : admitted) {
        const auto free_mb =
            (computer.memory_bytes() - computer.allocated_memory_bytes()) / 1000000;
        const auto target =
            std::min(agent.recommended_ram_mb, agent.minimum_ram_mb + static_cast<double>(free_mb));
        AgentSystem::configure(people, computer, agent.id, agent.limit_tflops,
                               static_cast<std::int64_t>(target), agent.priority);
    }
    state(people).insert_or_assign("auto_paused", std::move(paused));
}
} // namespace
void OperationsSystem::install(toml::table& people) {
    if (people.contains("operations") || !people["upgrades"]["operations"].is_table())
        return;
    auto operations = *people["upgrades"]["operations"].as_table();
    operations.insert_or_assign("queue", toml::array{});
    operations.insert_or_assign("auto_paused", toml::array{});
    operations.insert_or_assign("manager_work", 0.0);
    operations.insert_or_assign("phase_one_complete", false);
    operations.insert_or_assign("recovered", false);
    for (const auto* id : {"rental", "hosting"})
        operations.insert_or_assign(
            id, toml::table{{"active", false}, {"owned", false}, {"paid_until", 0.0}});
    people.insert("operations", std::move(operations));
}
void OperationsSystem::validate(const toml::table& people, const Computer& computer,
                                const Network& network) {
    const auto* operations = people["operations"].as_table();
    if (!people.contains("operations"))
        return;
    require(operations, "Invalid Operations state");
    require((*operations)["queue"].is_array() && (*operations)["queue"].as_array()->size() <= 100 &&
                (*operations)["auto_paused"].is_array(),
            "Invalid queues");
    require((*operations)["phase_one_complete"].is_boolean() &&
                (*operations)["recovered"].is_boolean(),
            "Invalid progression state");
    const auto reserve = (*operations)["reserve_cores"].value_or(-1.0);
    require(std::isfinite(reserve) && reserve >= 0 && reserve <= 1000000, "Invalid reserved CPU");
    const auto work = (*operations)["manager_work"].value_or(-1.0);
    require(std::isfinite(work) && work >= 0, "Invalid manager work");
    for (const auto& id : *(*operations)["auto_paused"].as_array())
        require(id.is_string() && !id.value_or(std::string{}).empty(), "Invalid managed pause");
    require(plan(people, "rental")["host_id"] != plan(people, "hosting")["host_id"],
            "Compute and hosting require different server identities");
    const auto backup_size = (*operations)["backup_storage_mb"].value_or(std::int64_t{-1});
    const auto backup_cost = (*operations)["backup_cost_cents"].value_or(std::int64_t{-1});
    require(backup_size > 0 && backup_size <= 1000000 && backup_cost >= 0 &&
                backup_cost <= 1000000000,
            "Invalid backup cost/size");
    std::unordered_set<std::string> queued;
    for (const auto& node : *(*operations)["queue"].as_array()) {
        const auto* job = node.as_table();
        require(job, "Invalid scheduled job");
        const auto id = (*job)["tool"].value_or(std::string{});
        const auto status = (*job)["status"].value_or(std::string{});
        require(!id.empty() && queued.insert(id).second && (*job)["repeat"].is_boolean(),
                "Duplicate or malformed scheduled job");
        require(status == "queued" || status == "active" || status == "complete" ||
                    status == "cancelled",
                "Invalid scheduled status");
        const auto priority = (*job)["priority"].value_or(0);
        require(priority >= 1 && priority <= 5, "Invalid job priority");
        const auto tools = EconomySystem::tools(people, computer.time_seconds());
        require(std::any_of(tools.begin(), tools.end(),
                            [&](const auto& t) {
                                return t.id == id;
                            }),
                "Unknown scheduled tool");
    }
    for (const auto* id : {"rental", "hosting"}) {
        const auto config = plan(people, id);
        const auto cores = config["cores"].value_or(0);
        const auto ram = config["ram_mb"].value_or(std::int64_t{0});
        const auto storage = config["storage_mb"].value_or(std::int64_t{0});
        const auto period = config["period_seconds"].value_or(0.0);
        require(cores > 0 && cores <= 1000 && ram > 0 && ram <= 100000 && storage > 0 &&
                    storage <= 1000000 && std::isfinite(period) && period >= 1 && period <= 1000000,
                "Invalid infrastructure capacity/period");
        for (const auto* key : {"purchase_cents", "period_cost_cents"}) {
            const auto price = config[key].value_or(std::int64_t{-1});
            require(price >= 0 && price <= 1000000000, "Invalid infrastructure price");
        }
        const auto host = config["host_id"].value_or(std::string{});
        require(!host.empty() && !config["label"].value_or(std::string{}).empty(),
                "Invalid server identity");
        const auto* service = (*operations)[id].as_table();
        require(service && (*service)["active"].is_boolean() && (*service)["owned"].is_boolean(),
                "Invalid subscription");
        const double until = (*service)["paid_until"].value_or(-1.0);
        require(std::isfinite(until) && until >= 0 && until <= 1000000000000.0,
                "Invalid subscription deadline");
        if ((*service)["active"].value_or(false))
            require(std::any_of(network.hosts().begin(), network.hosts().end(),
                                [&](const auto& h) {
                                    return h.id == host && h.online && h.access_granted;
                                }),
                    "Active server missing");
    }
    if (operations->contains("backup_slot")) {
        require((*operations)["backup_slot"].is_string(), "Invalid recovery slot type");
        const auto time = (*operations)["backup_seconds"].value_or(-1.0);
        require(std::isfinite(time) && time >= 0 && time <= computer.time_seconds(),
                "Invalid backup clock");
        const auto slot = (*operations)["backup_slot"].value_or(std::string{});
        require(slot.starts_with("recovery-") && slot.size() <= 80 &&
                    std::all_of(slot.begin(), slot.end(),
                                [](unsigned char ch) {
                                    return std::isalnum(ch) || ch == '-';
                                }),
                "Invalid recovery slot");
    }
}
void OperationsSystem::queue(toml::table& people, const std::string& tool, int priority,
                             bool repeat) {
    permitted(people, "task_scheduler");
    require(priority >= 1 && priority <= 5, "Priority must be 1-5");
    const auto tools = EconomySystem::tools(people, 0);
    require(std::any_of(tools.begin(), tools.end(),
                        [&](const auto& t) {
                            return t.id == tool && t.kind == "client";
                        }),
            "Only client jobs can be scheduled");
    auto& jobs = *state(people)["queue"].as_array();
    for (auto& row : jobs)
        if ((*row.as_table())["tool"].value_or(std::string{}) == tool) {
            require((*row.as_table())["status"].value_or(std::string{}) != "active",
                    "Job is already active");
            *row.as_table() = toml::table{
                {"tool", tool}, {"priority", priority}, {"repeat", repeat}, {"status", "queued"}};
            return;
        }
    require(jobs.size() < 100, "Queue is full");
    jobs.push_back(toml::table{
        {"tool", tool}, {"priority", priority}, {"repeat", repeat}, {"status", "queued"}});
}
void OperationsSystem::cancel(toml::table& people, const std::string& tool) {
    for (auto& row : *state(people)["queue"].as_array())
        if ((*row.as_table())["tool"].value_or(std::string{}) == tool)
            row.as_table()->insert_or_assign("status", "cancelled");
}
void OperationsSystem::rent(toml::table& people, Computer& computer, FileSystem& files,
                            Network& network) {
    permitted(people, "remote_compute");
    auto next_p = people;
    auto next_c = computer;
    auto next_f = files;
    auto next_n = network;
    activate(next_p, next_c, next_f, next_n, "rental");
    validate(next_p, next_c, next_n);
    people = std::move(next_p);
    computer = std::move(next_c);
    files = std::move(next_f);
    network = std::move(next_n);
}
void OperationsSystem::stop_rental(toml::table& people, Computer& computer, Network& network) {
    require(state(people)["rental"]["active"].value_or(false), "No active rental");
    capacity(computer, plan(people, "rental"), -1);
    network.set_online(plan(people, "rental")["host_id"].value_or(std::string{}), false);
    state(people)["rental"].as_table()->insert_or_assign("active", false);
}
void OperationsSystem::purchase_host(toml::table& people, Computer& computer, FileSystem& files,
                                     Network& network) {
    permitted(people, "hosting_purchase");
    auto next_p = people;
    auto next_c = computer;
    auto next_f = files;
    auto next_n = network;
    activate(next_p, next_c, next_f, next_n, "hosting");
    validate(next_p, next_c, next_n);
    people = std::move(next_p);
    computer = std::move(next_c);
    files = std::move(next_f);
    network = std::move(next_n);
}
void OperationsSystem::create_backup(WorldState& world, SaveSystem& saves) {
    permitted(world.people, "backup_recovery");
    require(state(world.people)["hosting"]["active"].value_or(false),
            "Activate your owned server first");
    auto next = world;
    const auto config = plan(next.people, "hosting");
    const auto host = config["host_id"].value_or(std::string{});
    const Identity actor{next.computer.player_user(), next.computer.player_groups()};
    const auto bytes = state(next.people)["backup_storage_mb"].value_or(std::uint64_t{0}) * 1000000;
    next.files.ensure_workspace(next.network, actor, host, next.computer.time_seconds());
    const auto backup_path = next.files.backup_path();
    const auto entries =
        next.files.list(next.network, actor, host, next.files.workspace_path("backups"));
    if (std::any_of(entries.begin(), entries.end(), [&](const auto* entry) {
            return entry->path == backup_path;
        }))
        next.files.release_service_file(next.network, actor, host, backup_path,
                                        next.computer.time_seconds());
    next.files.reserve_service_file(next.network, actor, host, backup_path, bytes,
                                    next.computer.time_seconds(), "backup");
    EconomySystem::spend(next.people,
                         state(next.people)["backup_cost_cents"].value_or(std::int64_t{0}));
    const auto slot =
        "recovery-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    state(next.people).insert_or_assign("backup_slot", slot);
    state(next.people).insert_or_assign("backup_seconds", next.computer.time_seconds());
    saves.save(slot, next);
    world = std::move(next);
}
WorldState OperationsSystem::recover(const WorldState& world, SaveSystem& saves) {
    permitted(world.people, "backup_recovery");
    const auto slot = world.people["operations"]["backup_slot"].value_or(std::string{});
    require(!slot.empty() && saves.exists(slot), "No valid recovery backup");
    require(world.people["operations"]["hosting"]["active"].value_or(false),
            "Recovery server is offline");
    const auto current_host = plan(world.people, "hosting")["host_id"].value_or(std::string{});
    world.files.inspect(world.network,
                        Identity{world.computer.player_user(), world.computer.player_groups()},
                        current_host, world.files.backup_path());
    auto next = saves.load(slot);
    require(next.scenario_id == world.scenario_id &&
                next.people["operations"]["backup_slot"].value_or(std::string{}) == slot,
            "Backup belongs to another game");
    const auto config = plan(next.people, "hosting");
    const auto host = config["host_id"].value_or(std::string{});
    const auto previous_host = next.network.hosts()[next.network.local_host_index()].id;
    next.files.transfer_workspace(next.network, Identity{"root"}, previous_host, host,
                                  next.computer.time_seconds());
    relocate_references(next.people, previous_host, host, next.files.workspace_root());
    relocate_references(next.emails, previous_host, host, next.files.workspace_root());
    relocate_references(next.pending_events, previous_host, host, next.files.workspace_root());
    next.network.relocate(host);
    if (auto* mailbox = next.emails["mailbox"].as_table();
        mailbox && (*mailbox)["host_id"].value_or(previous_host) == previous_host)
        mailbox->insert_or_assign("host_id", host);
    next.computer.relocate(host, config["label"].value_or(host), config["cores"].value_or(1),
                           config["ram_mb"].value_or(std::uint64_t{0}) * 1000000);
    const auto rental_host = plan(next.people, "rental")["host_id"].value_or(std::string{});
    if (state(next.people)["rental"]["active"].value_or(false))
        next.network.set_online(rental_host, false);
    state(next.people)["rental"].as_table()->insert_or_assign("active", false);
    state(next.people).insert_or_assign("recovered", true);
    state(next.people).insert_or_assign("recovery_seconds", world.computer.time_seconds());
    next.people["chat"].as_table()->erase("shutdown");
    next.paused = true;
    validate(next.people, next.computer, next.network);
    return next;
}
std::vector<std::string> OperationsSystem::advance(toml::table& people, Computer& computer,
                                                   FileSystem& files, Network& network) {
    std::vector<std::string> messages;
    if (!people.contains("operations"))
        return messages;
    for (const auto* id : {"rental", "hosting"}) {
        const auto config = plan(people, id);
        auto& service = *state(people)[id].as_table();
        if (!service["active"].value_or(false) ||
            computer.time_seconds() < service["paid_until"].value_or(0.0))
            continue;
        const auto period = config["period_seconds"].value_or(3600.0);
        const auto price = config["period_cost_cents"].value_or(std::int64_t{0});
        const double until = service["paid_until"].value_or(0.0);
        const auto cycles =
            static_cast<std::int64_t>(std::floor((computer.time_seconds() - until) / period)) + 1;
        const auto affordable =
            price == 0 ? cycles : std::min(cycles, EconomySystem::balance(people) / price);
        EconomySystem::spend(people, affordable * price);
        auto& updated = *state(people)[id].as_table();
        updated.insert_or_assign("paid_until", until + affordable * period);
        if (affordable < cycles) {
            if (std::string(id) == "hosting" && state(people)["recovered"].value_or(false)) {
                computer.set_execution_available(false);
                updated.insert_or_assign("suspended_at", until + affordable * period);
            } else
                capacity(computer, config, -1);
            network.set_online(config["host_id"].value_or(std::string{}), false);
            updated.insert_or_assign("active", false);
            messages.push_back(config["label"].value_or(std::string(id)) +
                               " suspended: insufficient cash for renewal.");
        }
    }
    balance_agents(people, computer, files, network, messages);
    if (!ChatSystem::available(people, "task_scheduler"))
        return messages;
    std::vector<toml::table> jobs;
    for (const auto& row : *state(people)["queue"].as_array())
        jobs.push_back(*row.as_table());
    std::stable_sort(jobs.begin(), jobs.end(), [](const auto& a, const auto& b) {
        return a["priority"].template value_or(1) > b["priority"].template value_or(1);
    });
    for (auto& job : jobs) {
        const auto id = job["tool"].value_or(std::string{});
        auto tools = EconomySystem::tools(people, computer.time_seconds());
        auto tool = std::find_if(tools.begin(), tools.end(), [&](const auto& t) {
            return t.id == id;
        });
        if (tool == tools.end())
            continue;
        if (job["status"].value_or(std::string{}) == "active" && tool->status == "idle") {
            job.insert_or_assign("status", job["repeat"].value_or(false) ? "queued" : "complete");
            messages.push_back("Scheduled job finished: " + tool->label);
        }
        if (job["status"].value_or(std::string{}) != "queued")
            continue;
        if (computer.day_finished() || !tool->unavailable.empty()) {
            job.insert_or_assign("blocked", computer.day_finished() ? "Start the next workday"
                                                                    : tool->unavailable);
            continue;
        }
        try { PaidWorkSystem::start(people, computer, files, network, id); }
        catch (const std::exception& e) { job.insert_or_assign("blocked", e.what()); continue; }
        job.insert_or_assign("status", "active");
        job.erase("blocked");
        messages.push_back("Scheduler started " + tool->label);
    }
    toml::array saved_jobs;
    for (auto& job : jobs)
        saved_jobs.push_back(std::move(job));
    state(people).insert_or_assign("queue", std::move(saved_jobs));
    return messages;
}
} // namespace ai_sim
