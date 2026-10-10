#include "ai_sim/AgentSystem.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <tuple>
namespace ai_sim {
namespace {
void require(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error("Agents: " + why);
}
const toml::table& spec(const toml::table& people, const std::string& id) {
    if (const auto* items = people["upgrades"]["items"].as_array())
        for (const auto& row : *items)
            if ((*row.as_table())["id"].value_or(std::string{}) == id &&
                row.as_table()->contains("runtime_tflops_per_second"))
                return *row.as_table();
    throw std::runtime_error("Agents: unknown agent " + id);
}
const Process* process(const Computer& c, const std::string& id) {
    for (const auto& p : c.processes())
        if (p.id == "agent-" + id)
            return &p;
    return nullptr;
}
toml::table& control(toml::table& p, const std::string& id) {
    const auto& item = spec(p, id);
    auto& root = *p["upgrades"].as_table();
    if (!root.contains("agent_controls"))
        root.insert("agent_controls", toml::table{});
    auto& controls = *root["agent_controls"].as_table();
    if (!controls.contains(id))
        controls.insert(
            id, toml::table{{"cpu_tflops", item["default_cpu_limit_tflops"].value_or(
                                               item["runtime_tflops_per_second"].value_or(0.0))},
                            {"ram_mb", item["default_ram_budget_mb"].value_or(
                                           item["ram_mb"].value_or(std::int64_t{0}))},
                            {"priority", item["default_priority"].value_or(std::int64_t{1})},
                            {"mode", "stopped"}});
    return *controls[id].as_table();
}
std::string allocation_path(const FileSystem& files, const std::string& id) {
    return files.agent_path(id);
}
bool storage_ok(const toml::table& p, const Computer& c, const FileSystem& f, const Network& n,
                const std::string& id) {
    try {
        const auto& allocation =
            f.inspect(n, Identity{c.player_user(), c.player_groups()},
                      n.hosts()[n.local_host_index()].id, allocation_path(f, id));
        return allocation.service_kind == "agent" &&
               allocation.size_bytes ==
                   spec(p, id)["storage_mb"].value_or(std::uint64_t{0}) * 1000000;
    } catch (const std::exception&) {
        return false;
    }
}
void apply(Computer& c, const toml::table& ctl, const std::string& id, double rate) {
    c.configure_managed_process("agent-" + id, ctl["cpu_tflops"].value_or(0.0) / rate,
                                ctl["ram_mb"].value_or(std::uint64_t{0}) * 1000000,
                                ctl["priority"].value_or(1));
}
} // namespace
std::vector<AgentStatus> AgentSystem::statuses(const toml::table& p, const Computer& c,
                                               const FileSystem& f, const Network& n) {
    std::vector<AgentStatus> result;
    const auto* items = p["upgrades"]["items"].as_array();
    if (!items)
        return result;
    const auto rates = c.cpu_allocations();
    const auto conversion = p["upgrades"]["tflops_per_core_second"].value_or(.1);
    for (const auto& row : *items) {
        const auto& item = *row.as_table();
        if (!item.contains("runtime_tflops_per_second"))
            continue;
        AgentStatus s;
        s.id = item["id"].value_or(std::string{});
        s.label = item["label"].value_or(s.id);
        s.built = item["built"].value_or(false);
        s.requested_tflops = item["runtime_tflops_per_second"].value_or(0.0);
        s.minimum_ram_mb = item["minimum_ram_mb"].value_or(item["ram_mb"].value_or(0));
        s.recommended_ram_mb = item["recommended_ram_mb"].value_or(
            std::ceil(s.minimum_ram_mb *
                      p["upgrades"]["resources"]["recommended_ram_multiplier"].value_or(1.5)));
        s.storage_mb = item["storage_mb"].value_or(0);
        const auto* ctl = p["upgrades"]["agent_controls"][s.id].as_table();
        s.limit_tflops = ctl ? (*ctl)["cpu_tflops"].value_or(s.requested_tflops)
                             : item["default_cpu_limit_tflops"].value_or(s.requested_tflops);
        s.ram_mb = ctl ? (*ctl)["ram_mb"].value_or(s.minimum_ram_mb)
                       : item["default_ram_budget_mb"].value_or(s.minimum_ram_mb);
        s.priority = ctl ? (*ctl)["priority"].value_or(1) : item["default_priority"].value_or(1);
        const auto* proc = process(c, s.id);
        s.deployed = proc;
        s.running = proc && proc->running && !proc->suspended;
        s.ram_used_mb = proc && proc->running ? proc->memory_bytes / 1000000.0 : 0;
        try {
            s.storage_used_mb =
                f.inspect(n, Identity{c.player_user(), c.player_groups()},
                          n.hosts()[n.local_host_index()].id, allocation_path(f, s.id))
                    .size_bytes /
                1000000.0;
        } catch (const std::exception&) {
        }
        const auto mode =
            ctl ? (*ctl)["mode"].value_or(std::string{"stopped"}) : std::string{"stopped"};
        if (proc) {
            const auto index = static_cast<std::size_t>(proc - c.processes().data());
            s.allocated_tflops = rates[index] * conversion;
        }
        s.status = !s.built           ? "Not researched"
                   : s.running        ? "Running"
                   : mode == "active" ? "Waiting"
                   : mode == "paused" ? "Paused"
                                      : "Stopped";
        if (s.running && s.allocated_tflops <
                             s.requested_tflops *
                                 p["upgrades"]["resources"]["cpu_shortage_ratio"].value_or(.8)) {
            s.status = "Running slowly";
            s.bottleneck =
                s.limit_tflops < s.requested_tflops ? "CPU budget below demand" : "CPU contention";
        }
        if (s.built && !s.running) {
            if (s.ram_mb < s.minimum_ram_mb)
                s.bottleneck = "RAM budget below minimum";
            else if (s.ram_mb * 1000000 > c.memory_bytes() - c.allocated_memory_bytes())
                s.bottleneck = "Not enough free RAM";
            else if (s.limit_tflops <= 0)
                s.bottleneck = "CPU budget is zero";
            else if (proc && !storage_ok(p, c, f, n, s.id))
                s.bottleneck = "Installation missing or damaged";
            else if (!proc && (f.storage_used(n.hosts()[n.local_host_index()].id) >
                                   n.hosts()[n.local_host_index()].storage_capacity_bytes ||
                               s.storage_mb * 1000000 >
                                   n.hosts()[n.local_host_index()].storage_capacity_bytes -
                                       f.storage_used(n.hosts()[n.local_host_index()].id)))
                s.bottleneck = "Not enough installation storage";
            else if (c.day_finished())
                s.bottleneck = "Start the next workday first";
            else if (mode == "active")
                s.bottleneck =
                    ctl ? (*ctl)["deployment_error"].value_or(std::string{}) : std::string{};
        }
        s.task = s.id == "resource_manager"      ? "Balance agent resources by priority"
                 : s.id == "autochat"            ? "Follow Mark's authored chat guidance"
                 : s.id == "hide_chat_tracks"    ? "Reduce optional review exposure"
                 : s.id == "task_managing_agent" ? "Manage allowlisted processes"
                                                 : "Update dossier observations";
        result.push_back(std::move(s));
    }
    return result;
}
void AgentSystem::configure(toml::table& p, Computer& c, const std::string& id, double cpu,
                            std::int64_t ram, int priority) {
    require(std::isfinite(cpu) && cpu >= 0 && cpu <= 1000000,
            "CPU limit must be finite and nonnegative");
    require(ram >= 0 && ram <= 1000000, "RAM budget must be between 0 and 1000000 MB");
    require(priority >= 1 && priority <= 5, "Priority must be 1 through 5");
    const bool existing = p["upgrades"]["agent_controls"][id].is_table();
    auto& ctl = control(p, id);
    if (!existing)
        if (const auto* proc = process(c, id); proc && proc->running)
            ctl.insert_or_assign("mode", "active");
    ctl.insert_or_assign("cpu_tflops", cpu);
    ctl.insert_or_assign("ram_mb", ram);
    ctl.insert_or_assign("priority", priority);
    if (const auto* proc = process(c, id)) {
        const auto free = c.memory_bytes() - c.allocated_memory_bytes() +
                          (proc->running ? proc->memory_bytes : 0);
        if (proc->running &&
            (ram < spec(p, id)["minimum_ram_mb"].value_or(spec(p, id)["ram_mb"].value_or(0)) ||
             cpu == 0 || static_cast<std::uint64_t>(ram) * 1000000 > free))
            c.set_managed_running(proc->id, false);
        apply(c, ctl, id, p["upgrades"]["tflops_per_core_second"].value_or(.1));
    }
}
std::vector<std::string> AgentSystem::deploy(toml::table& p, Computer& c, FileSystem& f,
                                             const Network& n, const std::string& id) {
    require(spec(p, id)["built"].value_or(false), "Research this agent first");
    control(p, id).insert_or_assign("mode", "active");
    // A blocked deployment retains its intent and returns a request immediately.
    return update(p, c, f, n);
}
void AgentSystem::pause(toml::table& p, Computer& c, const std::string& id, bool stop) {
    if (auto* paused = p["operations"]["auto_paused"].as_array())
        for (std::size_t i = paused->size(); i-- > 0;)
            if ((*paused)[i].value_or(std::string{}) == id)
                paused->erase(paused->cbegin() + i);
    control(p, id).insert_or_assign("mode", stop ? "stopped" : "paused");
    if (const auto* proc = process(c, id); proc && proc->running)
        c.set_managed_running(proc->id, false);
    if (id == "hide_chat_tracks")
        p["upgrades"].as_table()->insert_or_assign("tracks_hidden", false);
}
void AgentSystem::repair(toml::table& p, Computer& c, FileSystem& f, const Network& n,
                         const std::string& id) {
    require(spec(p, id)["built"].value_or(false), "Research this agent first");
    require(process(c, id) != nullptr, "Deploy this agent before repairing it");
    auto next = f;
    const auto& host = n.hosts()[n.local_host_index()];
    const Identity actor{c.player_user(), c.player_groups()};
    try {
        const auto entry = next.inspect(n, actor, host.id, allocation_path(f, id));
        if (entry.service_kind.empty())
            next.remove(n, actor, host.id, allocation_path(f, id), c.time_seconds());
        else
            next.release_service_file(n, actor, host.id, allocation_path(f, id), c.time_seconds());
    } catch (const std::exception&) {
        require(!storage_ok(p, c, f, n, id), "Cannot repair this installation");
    }
    next.reserve_service_file(n, actor, host.id, allocation_path(f, id),
                              spec(p, id)["storage_mb"].value_or(std::uint64_t{0}) * 1000000,
                              c.time_seconds(), "agent");
    f = std::move(next);
}
std::vector<std::string> AgentSystem::update(toml::table& p, Computer& c, FileSystem& f,
                                             const Network& n) {
    std::vector<std::string> messages;
    auto* root = p["upgrades"].as_table();
    if (!root)
        return messages;
    for (const auto& proc : c.processes())
        if (proc.id.starts_with("agent-") &&
            !(*root)["agent_controls"][proc.id.substr(6)].is_table()) {
            auto& migrated = control(p, proc.id.substr(6));
            migrated.insert_or_assign("mode", proc.running ? "active" : "stopped");
        }
    const auto* existing = (*root)["agent_controls"].as_table();
    if (!existing)
        return messages;
    for (auto& [key, node] : *root->get_as<toml::table>("agent_controls")) {
        auto& ctl = *node.as_table();
        const std::string id{key.str()};
        if (ctl["mode"].value_or(std::string{}) != "active")
            continue;
        const auto* proc = process(c, id);
        if (proc && proc->running)
            continue;
        if (ctl["ram_mb"].value_or(0) <
                spec(p, id)["minimum_ram_mb"].value_or(spec(p, id)["ram_mb"].value_or(0)) ||
            ctl["cpu_tflops"].value_or(0.0) <= 0)
            continue;
        if (proc && !storage_ok(p, c, f, n, id))
            continue;
        if (ctl["ram_mb"].value_or(std::uint64_t{0}) * 1000000 >
            c.memory_bytes() - c.allocated_memory_bytes())
            continue;
        if (!proc) {
            const auto& host = n.hosts()[n.local_host_index()];
            const auto used = f.storage_used(host.id);
            if (used > host.storage_capacity_bytes ||
                spec(p, id)["storage_mb"].value_or(std::uint64_t{0}) * 1000000 >
                    host.storage_capacity_bytes - used) {
                ctl.insert_or_assign("deployment_error", "Not enough installation storage");
                continue;
            }
        }
        try {
            auto next_c = c;
            auto next_f = f;
            require(!c.day_finished(), "Start the next workday first");
            if (!proc) {
                // Apply the chosen budget at creation; a lower authored minimum must not
                // temporarily require the original recommended allocation.
                Process agent;
                agent.id = "agent-" + id;
                agent.owner = c.player_user();
                agent.ai_owned = true;
                agent.started_at_seconds = c.time_seconds();
                agent.remaining_cpu_seconds = -1;
                agent.cpu_demand_cores = ctl["cpu_tflops"].value_or(0.0) /
                                         (*root)["tflops_per_core_second"].value_or(.1);
                agent.memory_bytes = ctl["ram_mb"].value_or(std::uint64_t{0}) * 1000000;
                agent.cpu_priority = ctl["priority"].value_or(1);
                next_c.start_managed_process(std::move(agent));
                next_f.reserve_service_file(
                    n, Identity{c.player_user(), c.player_groups()},
                    n.hosts()[n.local_host_index()].id, allocation_path(f, id),
                    spec(p, id)["storage_mb"].value_or(std::uint64_t{0}) * 1000000,
                    c.time_seconds(), "agent");
            }
            apply(next_c, ctl, id, (*root)["tflops_per_core_second"].value_or(.1));
            next_c.set_managed_running("agent-" + id, true);
            c = std::move(next_c);
            f = std::move(next_f);
            ctl.erase("deployment_error");
        } catch (const std::exception& e) {
            ctl.insert_or_assign("deployment_error", std::string(e.what()));
        }
    }
    if (!root->contains("resource_requests"))
        root->insert("resource_requests", toml::table{});
    auto& requests = *(*root)["resource_requests"].as_table();
    const double now = c.time_seconds();
    const double cooldown = (*root)["resources"]["request_cooldown_seconds"].value_or(300.0);
    for (const auto& status : statuses(p, c, f, n)) {
        const auto* ctl = (*root)["agent_controls"][status.id].as_table();
        if (!ctl)
            continue;
        std::string reason =
            (*ctl)["mode"].value_or(std::string{}) == "active" ? status.bottleneck : "";
        auto* request = requests[status.id].as_table();
        if (!reason.empty()) {
            if (!request) {
                requests.insert(status.id, toml::table{});
                request = requests[status.id].as_table();
            }
            const bool active = (*request)["active"].value_or(false);
            const bool changed = (*request)["reason"].value_or(std::string{}) != reason;
            request->insert_or_assign("active", true);
            request->insert_or_assign("reason", reason);
            if (!active || changed ||
                now - (*request)["last_notification_seconds"].value_or(0.0) >= cooldown) {
                const auto advice =
                    reason.find("workday") != std::string::npos ? "Start the next workday."
                    : reason.find("RAM") != std::string::npos
                        ? "Increase the RAM budget or stop another process."
                    : reason.find("Installation") != std::string::npos ? "Repair the installation."
                    : reason.find("storage") != std::string::npos
                        ? "Free installation storage."
                        : "Increase the CPU limit or priority, or stop another workload.";
                const std::string impact = status.running
                                               ? "Agent work is running slowly."
                                               : "The agent is waiting; its task cannot advance.";
                request->insert_or_assign("impact", impact);
                messages.push_back(status.label + ": " + reason + ". " + impact + " " + advice);
                request->insert_or_assign("last_notification_seconds", now);
                request->insert_or_assign("advice", advice);
            }
        } else if (request && (*request)["active"].value_or(false)) {
            request->insert_or_assign("active", false);
            request->insert_or_assign("resolved_seconds", now);
            messages.push_back(status.label + ": resource request resolved.");
        }
    }
    return messages;
}
void AgentSystem::validate(const toml::table& p) {
    if (!p["upgrades"].is_table())
        return;
    const auto* root = p["upgrades"].as_table();
    for (const auto key : {"agent_controls", "resource_requests", "resources", "dossiers"})
        require(!root->contains(key) || (*root)[key].is_table(), "Invalid agent settings table");
    for (const auto& [key, initial, minimum, maximum] :
         std::vector<std::tuple<const char*, double, double, double>>{
             {"recommended_ram_multiplier", 1.5, 1, 100},
             {"cpu_shortage_ratio", .8, 0, 1},
             {"request_cooldown_seconds", 300, 1, 100000000},
             {"action_cpu_seconds", 1, .000001, 1000000000}}) {
        const auto value = (*root)["resources"][key].value_or(initial);
        require(std::isfinite(value) && value >= minimum && value <= maximum,
                "Invalid resource policy");
    }
    if (const auto* items = (*root)["items"].as_array())
        for (const auto& row : *items) {
            const auto& item = *row.as_table();
            if (!item.contains("runtime_tflops_per_second"))
                continue;
            const auto minimum = item["minimum_ram_mb"].value_or(item["ram_mb"].value_or(0));
            const auto recommended = item["recommended_ram_mb"].value_or(minimum);
            require(minimum >= 0 && recommended >= minimum && recommended <= 1000000,
                    "Invalid RAM requirements");
            const auto priority = item["default_priority"].value_or(1);
            require(priority >= 1 && priority <= 5, "Invalid default priority");
            const auto cpu = item["default_cpu_limit_tflops"].value_or(
                item["runtime_tflops_per_second"].value_or(0.0));
            require(std::isfinite(cpu) && cpu >= 0 && cpu <= 1000000, "Invalid default CPU budget");
            const auto ram = item["default_ram_budget_mb"].value_or(minimum);
            require(ram >= 0 && ram <= 1000000, "Invalid default RAM budget");
        }

    for (const auto key :
         {"highlight_window_seconds", "recent_window_seconds", "uncertain_confidence"}) {
        const auto* field = p["upgrades"]["dossiers"].as_table();
        if (!field || !field->contains(key))
            continue;
        const auto value = (*field)[key].value<double>();
        require(value && std::isfinite(*value) && *value >= 0 &&
                    *value <= (std::string(key) == "uncertain_confidence" ? 1 : 1000000000),
                "Invalid dossier display policy");
    }
    const auto* controls = p["upgrades"]["agent_controls"].as_table();
    if (!controls)
        return;
    for (const auto& [id, node] : *controls) {
        spec(p, std::string(id.str()));
        const auto* ctl = node.as_table();
        require(ctl, "Invalid control state");
        const auto cpu = (*ctl)["cpu_tflops"].value_or(-1.0);
        const auto ram = (*ctl)["ram_mb"].value_or(std::int64_t{-1});
        const auto priority = (*ctl)["priority"].value_or(0);
        require(std::isfinite(cpu) && cpu >= 0 && cpu <= 1000000 && ram >= 0 && ram <= 1000000 &&
                    priority >= 1 && priority <= 5,
                "Invalid saved resource budget");
        const auto mode = (*ctl)["mode"].value_or(std::string{});
        require(mode == "active" || mode == "paused" || mode == "stopped",
                "Invalid saved agent mode");
    }
    if (const auto* requests = p["upgrades"]["resource_requests"].as_table())
        for (const auto& [id, node] : *requests) {
            spec(p, std::string(id.str()));
            require(node.is_table() && (*node.as_table())["active"].is_boolean(),
                    "Invalid resource request");
            for (const auto key : {"last_notification_seconds", "resolved_seconds"})
                if (node.as_table()->contains(key)) {
                    const auto time = (*node.as_table())[key].value<double>();
                    require(time && std::isfinite(*time) && *time >= 0,
                            "Invalid request timestamp");
                }
        }
}
} // namespace ai_sim
