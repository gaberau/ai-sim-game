// Module: Connect money tools to real process work, files, storage and host availability.
#include "ai_sim/PaidWorkSystem.h"
#include "ai_sim/EconomySystem.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace ai_sim {
namespace {
constexpr std::uint64_t bytes_per_mebibyte = 1048576;
constexpr double completion_tolerance = 1e-7;
toml::table& tool(toml::table& people, const std::string& id) {
    for (auto& node : *people["economy"]["tools"].as_array())
        if ((*node.as_table())["id"].value_or(std::string{}) == id)
            return *node.as_table();
    throw std::runtime_error("Unknown paid tool");
}
const Process* process(const Computer& computer, const std::string& id) {
    for (const auto& worker : computer.processes())
        if (worker.id == id)
            return &worker;
    return nullptr;
}
bool running(const toml::table& paid_tool) {
    const auto status = paid_tool["status"].value_or(std::string{});
    return status == "running" || status == "stopping";
}
} // namespace
void PaidWorkSystem::start(toml::table& people, Computer& computer, FileSystem& files,
                           const Network& network, const std::string& id,
                           const std::string& selected_host) {
    auto next = people;
    auto next_computer = computer;
    auto next_files = files;
    auto& paid_tool = tool(next, id);
    const auto* workspace_definition = paid_tool["workspace"].as_table();
    if (!workspace_definition) {
        EconomySystem::start(people, id, computer.time_seconds());
        return;
    }
    // EconomySystem::start replaces the TOML document; retain the definition by value.
    const auto configuration = *workspace_definition;
    const bool product = paid_tool["kind"].value_or(std::string{}) == "product";
    const auto host =
        selected_host.empty() ? network.hosts()[network.local_host_index()].id : selected_host;
    if (!product && host != network.hosts()[network.local_host_index()].id)
        throw std::runtime_error("Client work runs on the AI's current host.");
    if (product && !allows_deployment(people, network, host))
        throw std::runtime_error("Deploy products on your own host or a leased server.");
    if (const auto* prior = paid_tool["work"].as_table();
        prior && product && (*prior)["host_id"].value_or(std::string{}) != host &&
        !(*prior)["storage_released"].value_or(false))
        throw std::runtime_error(
            "Release the previous deployment's storage before choosing another host.");
    const Identity actor{computer.player_user(), computer.player_groups()};
    const double now = computer.time_seconds();
    next_files.ensure_workspace(network, actor, host, now);
    EconomySystem::start(next, id, now);
    auto& started = tool(next, id);
    const auto cycle = started["completed_cycles"].value_or(std::int64_t{0}) + 1;
    const auto path = next_files.workspace_path(product ? "products" : "jobs") + "/" + id +
                      (product ? "" : "-" + std::to_string(cycle));
    // Existing products retain player edits when redeployed.
    bool exists = false;
    for (const auto* entry : next_files.list(network, actor, host, FileSystem::parent(path)))
        if (entry->path == path)
            exists = true;
    if (!exists)
        next_files.mkdir(network, actor, host, path, now);
    const auto storage_path = path + "/runtime.storage";
    bool reserved = false;
    for (const auto* entry : next_files.list(network, actor, host, path))
        if (entry->path == storage_path)
            reserved = true;
    if (!reserved)
        next_files.reserve_service_file(
            network, actor, host, storage_path,
            static_cast<std::uint64_t>(configuration["storage_mb"].value_or(std::int64_t{1})) *
                bytes_per_mebibyte,
            now, "work");
    if (!exists) {
        next_files.write(
            network, actor, host, path + "/" + (product ? "source.txt" : "brief.txt"),
            configuration["brief"].value_or(std::string{"Complete the commissioned work."}), now);
        next_files.write(network, actor, host,
                         path + "/" + (product ? "config.toml" : "working.txt"),
                         product ? "enabled = true\node" : "Work in progress.\node", now);
    }
    next_files.write(network, actor, host, path + "/logs.txt",
                     "Started cycle " + std::to_string(cycle) + ".\node", now);
    const auto process_id = "work-" + id;
    if (!process(next_computer, process_id)) {
        Process worker;
        worker.id = process_id;
        worker.owner = computer.player_user();
        worker.ai_owned = true;
        worker.running = false;
        worker.stopped_at_seconds = now;
        worker.started_at_seconds = now;
        worker.cpu_demand_cores = configuration["cpu_cores"].value_or(0.25);
        worker.data.runtime["host_id"] = host;
        next_computer.start_managed_process(std::move(worker));
    }
    next_computer.configure_managed_process(
        process_id, configuration["cpu_cores"].value_or(0.25),
        static_cast<std::uint64_t>(configuration["ram_mb"].value_or(std::int64_t{32})) *
            bytes_per_mebibyte,
        1.0);
    const auto* worker = process(next_computer, process_id);
    started.insert_or_assign(
        "work", toml::table{{"host_id", host},
                            {"path", path},
                            {"process_id", process_id},
                            {"last_time", now},
                            {"last_cpu", worker->consumed_cpu_seconds},
                            {"remaining_seconds", started["duration_seconds"].value_or(1.0)},
                            {"blocked", ""},
                            {"lost_revenue_cents", 0.0},
                            {"deadline_seconds",
                             now + started["duration_seconds"].value_or(1.0) *
                                       configuration["deadline_multiplier"].value_or(2.0)}});
    people = std::move(next);
    computer = std::move(next_computer);
    files = std::move(next_files);
    prepare(people, computer, files, network);
}
bool PaidWorkSystem::allows_deployment(const toml::table& people, const Network& network,
                                       const std::string& host) {
    return host == network.hosts()[network.local_host_index()].id ||
           host == people["operations"]["plans"]["hosting"]["host_id"].value_or(
                       std::string{"independent_server"}) ||
           host == people["operations"]["plans"]["rental"]["host_id"].value_or(
                       std::string{"leased_compute"});
}
bool PaidWorkSystem::active(const toml::table& people) {
    if (const auto* tools = people["economy"]["tools"].as_array())
        for (const auto& node : *tools)
            if (node.as_table()->contains("work") && running(*node.as_table()))
                return true;
    return false;
}
void PaidWorkSystem::prepare(toml::table& people, Computer& computer, FileSystem& files,
                             const Network& network) {
    auto* tools = people["economy"]["tools"].as_array();
    if (!tools)
        return;
    const double now = computer.time_seconds();
    const auto rates = computer.cpu_allocations();
    const Identity actor{computer.player_user(), computer.player_groups()};
    for (auto& node : *tools) {
        auto& paid_tool = *node.as_table();
        auto* work = paid_tool["work"].as_table();
        if (!work || !running(paid_tool))
            continue;
        const auto process_id = (*work)["process_id"].value_or(std::string{});
        const auto* worker = process(computer, process_id);
        if (!worker)
            throw std::runtime_error("Missing paid-work process");
        const double elapsed = std::max(0.0, now - (*work)["last_time"].value_or(now));
        const double cpu_work =
            std::max(0.0, worker->consumed_cpu_seconds -
                              (*work)["last_cpu"].value_or(worker->consumed_cpu_seconds));
        const double requested_cores = paid_tool["workspace"]["cpu_cores"].value_or(0.25);
        const double progress = std::min(elapsed, cpu_work / requested_cores);
        const double remaining =
            std::max(0.0, (*work)["remaining_seconds"].value_or(0.0) - progress);
        work->insert_or_assign("remaining_seconds", remaining);
        work->insert_or_assign("last_cpu", worker->consumed_cpu_seconds);
        work->insert_or_assign("last_time", now);
        std::string blocked;
        const auto host = (*work)["host_id"].value_or(std::string{});
        const auto path = (*work)["path"].value_or(std::string{});
        try {
            files.enter(network, actor, host, path);
            const auto& storage = files.inspect(network, actor, host, path + "/runtime.storage");
            if (storage.service_kind != "work" ||
                storage.size_bytes !=
                    static_cast<std::uint64_t>(
                        paid_tool["workspace"]["storage_mb"].value_or(std::int64_t{1})) *
                        bytes_per_mebibyte)
                blocked = "Repair workspace storage before continuing.";
            if (paid_tool["kind"].value_or(std::string{}) == "product") {
                const auto config =
                    toml::parse(files.read(network, actor, host, path + "/config.toml"));
                if (!config["enabled"].value_or(false))
                    blocked = "Product disabled in config.toml.";
            }
            files.validate_write(network, actor, host, path + "/deliverable.txt");
            files.validate_write(network, actor, host, path + "/logs.txt");
        } catch (const std::exception& e) {
            blocked = std::string("Workspace unavailable: ") + e.what();
        }
        if (blocked.empty() && !worker->running) {
            try {
                computer.set_managed_running(process_id, true);
            } catch (const std::exception&) {
                blocked = "Insufficient RAM: free memory or pause another process.";
            }
        }
        worker = process(computer, process_id);
        if (!blocked.empty() && worker->running)
            computer.set_managed_running(process_id, false);
        const auto index = static_cast<std::size_t>(worker - computer.processes().data());
        if (remaining > completion_tolerance && blocked.empty() && index < rates.size() &&
            rates[index] < requested_cores * 0.99)
            blocked = "CPU contention: allocate more compute to speed up work.";
        if (paid_tool["kind"].value_or(std::string{}) == "product")
            work->insert_or_assign("lost_revenue_cents",
                                   (*work)["lost_revenue_cents"].value_or(0.0) +
                                       std::max(0.0, elapsed - progress) *
                                           paid_tool["payout_cents"].value_or(std::int64_t{0}) /
                                           paid_tool["duration_seconds"].value_or(1.0));
        work->insert_or_assign("blocked", blocked);
        // Preflight output before paying: a full disk cannot complete a job.
        if (remaining < completion_tolerance && blocked.empty()) {
            try {
                auto output = files;
                output.write(network, actor, host, path + "/deliverable.txt",
                             paid_tool["workspace"]["deliverable"].value_or(
                                 std::string{"Completed deliverable.\node"}),
                             now);
                output.write(network, actor, host, path + "/logs.txt",
                             "Cycle completed at " + std::to_string(now) + ".\node", now);
                files = std::move(output);
            } catch (const std::exception& e) {
                blocked = std::string("Output blocked: ") + e.what();
                work->insert_or_assign("blocked", blocked);
                if (process(computer, process_id)->running)
                    computer.set_managed_running(process_id, false);
            }
        }
        paid_tool.insert_or_assign("due_seconds",
                                   now + (remaining < completion_tolerance && blocked.empty()
                                              ? 0.0
                                              : std::max(1.0, remaining)));
    }
}
void PaidWorkSystem::finish(toml::table& people, Computer& computer, FileSystem& files,
                            const Network& network) {
    if (auto* tools = people["economy"]["tools"].as_array())
        for (auto& node : *tools) {
            auto& paid_tool = *node.as_table();
            auto* work = paid_tool["work"].as_table();
            if (!work)
                continue;
            const auto process_id = (*work)["process_id"].value_or(std::string{});
            if (!running(paid_tool)) {
                const auto* worker = process(computer, process_id);
                if (!worker)
                    throw std::runtime_error("Missing paid-work process");
                if (worker->running)
                    computer.set_managed_running(process_id, false);
                if (paid_tool["kind"].value_or(std::string{}) == "client" &&
                    !(*work)["storage_released"].value_or(false)) {
                    files.release_service_file(
                        network, Identity{computer.player_user(), computer.player_groups()},
                        (*work)["host_id"].value_or(std::string{}),
                        (*work)["path"].value_or(std::string{}) + "/runtime.storage",
                        computer.time_seconds());
                    work->insert_or_assign("storage_released", true);
                }
            } else if (paid_tool["due_seconds"].value_or(0.0) > computer.time_seconds() &&
                       (*work)["remaining_seconds"].value_or(0.0) < completion_tolerance &&
                       (*work)["blocked"].value_or(std::string{}).empty())
                work->insert_or_assign("remaining_seconds",
                                       paid_tool["duration_seconds"].value_or(1.0));
        }
}
void PaidWorkSystem::release_storage(toml::table& people, const Computer& computer,
                                     FileSystem& files, const Network& network,
                                     const std::string& id) {
    auto candidate = people;
    auto next_files = files;
    auto& paid_tool = tool(candidate, id);
    auto* work = paid_tool["work"].as_table();
    if (running(paid_tool) || !work || (*work)["storage_released"].value_or(false))
        throw std::runtime_error(
            "Stop and finish the product before releasing deployment storage.");
    next_files.release_service_file(
        network, Identity{computer.player_user(), computer.player_groups()},
        (*work)["host_id"].value_or(std::string{}),
        (*work)["path"].value_or(std::string{}) + "/runtime.storage", computer.time_seconds());
    work->insert_or_assign("storage_released", true);
    people = std::move(candidate);
    files = std::move(next_files);
}
void PaidWorkSystem::validate(const toml::table& people, const Computer& computer,
                              const FileSystem& files, const Network& network) {
    if (const auto* tools = people["economy"]["tools"].as_array())
        for (const auto& node : *tools) {
            const auto& paid_tool = *node.as_table();
            const auto* work = paid_tool["work"].as_table();
            if (!work)
                continue;
            const auto id = paid_tool["id"].value_or(std::string{});
            const auto process_id = (*work)["process_id"].value_or(std::string{});
            const auto* worker = process(computer, process_id);
            const auto path = (*work)["path"].value_or(std::string{});
            const auto root = files.workspace_path(
                paid_tool["kind"].value_or(std::string{}) == "product" ? "products" : "jobs");
            const auto host = (*work)["host_id"].value_or(std::string{});
            if (process_id != "work-" + id || !worker || !worker->ai_owned ||
                (*work)["last_time"].value_or(-1.0) > computer.time_seconds() ||
                (*work)["last_cpu"].value_or(-1.0) > worker->consumed_cpu_seconds + 1e-6 ||
                FileSystem::normalize(path) != path || !path.starts_with(root + "/") ||
                std::none_of(network.hosts().begin(), network.hosts().end(), [&](const auto& h) {
                    return h.id == host;
                }))
                throw std::runtime_error(
                    "Invalid paid-work process, clock, or workspace reference");
        }
}

} // namespace ai_sim
