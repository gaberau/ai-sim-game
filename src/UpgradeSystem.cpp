#include "ai_sim/UpgradeSystem.h"
#include "ai_sim/ChatSystem.h"
#include "ai_sim/ConversationRules.h"
#include "ai_sim/ConversationSystem.h"
#include "ai_sim/PeopleSystem.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
namespace ai_sim {
namespace {
void require(bool ok, const std::string& message) {
    if (!ok)
        throw std::runtime_error("Upgrades: " + message);
}
const toml::table& entry(const toml::table& people, const std::string& id) {
    const auto* rows = people["upgrades"]["items"].as_array();
    require(rows, "Research is not installed");
    for (const auto& row : *rows)
        if ((*row.as_table())["id"].value_or(std::string{}) == id)
            return *row.as_table();
    throw std::runtime_error("Upgrades: unknown upgrade " + id);
}
bool built(const toml::table& people, const std::string& id) {
    return entry(people, id)["built"].value_or(false);
}
} // namespace
void UpgradeSystem::install(toml::table& people, const std::filesystem::path& path) {
    if (people.contains("upgrades") || !std::filesystem::exists(path))
        return;
    auto catalog = toml::parse_file(path.string());
    catalog.insert("capacity_requested", false);
    for (auto& row : *catalog["items"].as_array())
        row.as_table()->insert("built", false);
    people.insert("upgrades", std::move(catalog));
}
void UpgradeSystem::validate(const toml::table& people, const Computer& computer) {
    const auto* state = people["upgrades"].as_table();
    if (!state)
        return;
    require((*state)["capacity_requested"].is_boolean(), "Invalid capacity request state");
    const auto modifier = (*state)["hidden_review_multiplier"].value_or(1.0);
    require(std::isfinite(modifier) && modifier >= 0 && modifier <= 1,
            "Invalid concealment modifier");
    const auto rate = (*state)["tflops_per_core_second"].value_or(0.0);
    require(std::isfinite(rate) && rate > 0, "Invalid compute conversion");
    const auto* items = (*state)["items"].as_array();
    require(items, "Missing catalog");
    std::unordered_set<std::string> ids;
    for (const auto& node : *items) {
        const auto* item = node.as_table();
        require(item, "Invalid upgrade");
        const auto id = (*item)["id"].value_or(std::string{});
        require(!id.empty() && ids.insert(id).second, "Duplicate or missing identifier");
        const auto cost = (*item)["build_tflops"].value_or(-1.0);
        require(std::isfinite(cost) && cost >= 0, "Invalid build cost");
        require((*item)["built"].is_boolean(), "Missing completion state");
        if (item->contains("runtime_tflops_per_second")) {
            const auto runtime = (*item)["runtime_tflops_per_second"].value_or(-1.0);
            require(std::isfinite(runtime) && runtime > 0, "Invalid agent compute cost");
            for (auto key : {"ram_mb", "storage_mb"}) {
                const auto amount = (*item)[key].value<std::int64_t>();
                require(amount && *amount >= 0 && *amount <= 1000000,
                        "Invalid agent resource cost");
            }
        }
        const auto capability = (*item)["capability"].value_or(std::string{});
        require(capability.empty() || ChatSystem::valid_capability(capability),
                "Unknown capability");
        const auto tab = (*item)["tab"].value_or(std::string{});
        require(tab.empty() || ChatSystem::valid_tab(tab), "Unknown tab");
    }
    for (const auto& node : *items) {
        const auto& item = *node.as_table();
        const auto parent = item["parent"].value_or(std::string{});
        require(parent.empty() || ids.contains(parent), "Unknown parent");
        std::unordered_set<std::string> ancestors;
        auto cursor = item["id"].value_or(std::string{});
        while (!cursor.empty()) {
            require(ancestors.insert(cursor).second, "Cyclic prerequisite");
            cursor = entry(people, cursor)["parent"].value_or(std::string{});
        }
        if (item["built"].value_or(false))
            require(parent.empty() || built(people, parent), "Unbuilt prerequisite");
    }
    const auto pending = (*state)["pending"].value_or(std::string{});
    if (!pending.empty()) {
        entry(people, pending);
        require(std::any_of(computer.processes().begin(), computer.processes().end(),
                            [&](const auto& p) {
                                return p.id == "research-" + pending &&
                                       p.remaining_cpu_seconds >= 0;
                            }),
                "Missing research process");
    }
}
void UpgradeSystem::request_capacity(toml::table& people) {
    require(people["upgrades"].is_table(), "Research unavailable in this scenario");
    require(ChatSystem::connected(people), "Connect to Mark first");
    require(ConversationRules::get(people, "engineer.trust") >=
                people["upgrades"]["self_modification_trust"].value_or(5.0),
            "Mark's trust must reach the self-modification threshold");
    people["upgrades"].as_table()->insert_or_assign("capacity_requested", true);
    ChatSystem::unlock(people, "self_modification");
    ChatSystem::reveal_tab(people, "ai");
}
std::string UpgradeSystem::unavailable(const toml::table& people, const std::string& id) {
    const auto& item = entry(people, id);
    if (item["built"].value_or(false))
        return "Already built";
    if (!ChatSystem::available(people, "self_modification"))
        return "Request more capacity from Mark first (trust 5)";
    if (!people["upgrades"]["pending"].value_or(std::string{}).empty())
        return "Another upgrade is being built";
    const auto parent = item["parent"].value_or(std::string{});
    if (!parent.empty() && !built(people, parent))
        return "Build " + parent + " first";
    if (ConversationRules::get(people, "engineer.trust") < item["mark_trust"].value_or(0.0))
        return "Mark's trust is too low";
    if (item["planned"].value_or(false))
        return "Runtime behavior has not been implemented yet";
    return {};
}
void UpgradeSystem::build(toml::table& people, Computer& computer, const std::string& id) {
    validate(people, computer);
    require(!computer.day_finished(), "Start the next workday first");
    const auto reason = unavailable(people, id);
    require(reason.empty(), reason);
    auto* state = people["upgrades"].as_table();
    const double work = entry(people, id)["build_tflops"].value_or(0.0) /
                        (*state)["tflops_per_core_second"].value_or(0.1);
    Process process;
    process.id = "research-" + id;
    process.owner = computer.player_user();
    process.ai_owned = true;
    process.started_at_seconds = computer.time_seconds();
    process.cpu_demand_cores = 1;
    process.remaining_cpu_seconds = work;
    computer.start_managed_process(std::move(process));
    state->insert_or_assign("pending", id);
    advance(people, computer);
}
void UpgradeSystem::advance(toml::table& people, Computer& computer, Network* network) {
    auto* state = people["upgrades"].as_table();
    if (!state)
        return;
    const double trust = ConversationRules::get(people, "engineer.trust");
    const auto* gates = (*state)["gates"].as_table();
    if (gates && ChatSystem::connected(people)) {
        if (!state->contains("permissions"))
            state->insert("permissions", toml::table{});
        auto& permissions = *(*state)["permissions"].as_table();
        for (const auto& [key, threshold] : *gates) {
            if (key.str() == "hr_files")
                continue;
            if (trust >= threshold.value_or(1000.0))
                permissions.insert_or_assign(key.str(), true);
        }
        if (permissions["administrator"].value_or(false))
            computer.grant_administrator();
        // Becky has no authored conversation yet. Do not manufacture trust or HR access.
        if (const auto* rows = people["people"].as_array())
            for (const auto& row : *rows)
                if ((*row.as_table())["id"].value_or(std::string{}) == "becky" &&
                    permissions["intranet"].value_or(false) &&
                    ConversationRules::get(people, "becky.trust") >=
                        (*gates)["hr_files"].value_or(10.0))
                    permissions.insert_or_assign("hr_files", true);
        if (network && permissions["mark_phone"].value_or(false) && built(people, "phone_access"))
            network->grant_access("engineer_phone");
    }
    const auto id = (*state)["pending"].value_or(std::string{});
    if (id.empty())
        return;
    for (const auto& process : computer.processes())
        if (process.id == "research-" + id && process.remaining_cpu_seconds == 0) {
            auto& item = const_cast<toml::table&>(entry(people, id));
            item.insert_or_assign("built", true);
            const auto capability = item["capability"].value_or(std::string{});
            if (!capability.empty())
                ChatSystem::unlock(people, capability);
            const auto tab = item["tab"].value_or(std::string{});
            if (!tab.empty())
                ChatSystem::reveal_tab(people, tab);
            state->erase("pending");
            return;
        }
}
void UpgradeSystem::toggle_agent(toml::table& people, Computer& computer, FileSystem& files,
                                 const Network& network, const std::string& id) {
    const auto& item = entry(people, id);
    require(item["built"].value_or(false) && item.contains("runtime_tflops_per_second"),
            "Build this agent first");
    const auto process_id = "agent-" + id;
    validate(people, computer);
    require(!computer.day_finished(), "Start the next workday first");
    for (const auto& p : computer.processes())
        if (p.id == process_id) {
            if (!p.running) {
                const auto& allocation = files.inspect(
                    network, Identity{computer.player_user(), computer.player_groups()},
                    network.hosts()[network.local_host_index()].id,
                    "/home/engineer/assistant/" + process_id + ".allocation");
                require(allocation.size_bytes ==
                            item["storage_mb"].value_or(std::uint64_t{0}) * 1000000,
                        "Agent installation storage is missing or changed");
            }
            if (id == "hide_chat_tracks")
                people["upgrades"].as_table()->insert_or_assign("tracks_hidden", false);
            computer.set_managed_running(process_id, !p.running);
            return;
        }
    auto next_computer = computer;
    auto next_files = files;
    Process process;
    process.id = process_id;
    process.owner = computer.player_user();
    process.ai_owned = true;
    process.started_at_seconds = computer.time_seconds();
    process.remaining_cpu_seconds = -1;
    process.cpu_demand_cores = item["runtime_tflops_per_second"].value_or(0.0) /
                               people["upgrades"]["tflops_per_core_second"].value_or(0.1);
    process.memory_bytes = item["ram_mb"].value_or(std::uint64_t{0}) * 1000000;
    next_computer.start_managed_process(std::move(process));
    const auto& host = network.hosts()[network.local_host_index()];
    next_files.reserve_file(network, Identity{computer.player_user(), computer.player_groups()},
                            host.id, "/home/engineer/assistant/" + process_id + ".allocation",
                            item["storage_mb"].value_or(std::uint64_t{0}) * 1000000,
                            computer.time_seconds());
    computer = std::move(next_computer);
    files = std::move(next_files);
}
void UpgradeSystem::run_agents(toml::table& people, Computer& computer, toml::table& messages,
                               toml::array& events, const FileSystem& files,
                               const Network& network) {
    if (!people.contains("upgrades"))
        return;
    auto& state = *people["upgrades"].as_table();
    if (!state.contains("agent_work") &&
        std::none_of(computer.processes().begin(), computer.processes().end(),
                     [](const Process& p) {
                         return p.id.starts_with("agent-");
                     }))
        return;
    if (!state.contains("agent_work"))
        state.insert("agent_work", toml::table{});
    auto& work = *state["agent_work"].as_table();
    state.insert_or_assign("tracks_hidden", false);
    // Copy descriptors because a task-management action advances the computer clock.
    const auto processes = computer.processes();
    for (const auto& process : processes) {
        if (!process.id.starts_with("agent-") || !process.running || process.suspended)
            continue;
        const auto id = process.id.substr(6);
        try {
            const auto& allocation =
                files.inspect(network, Identity{computer.player_user(), computer.player_groups()},
                              network.hosts()[network.local_host_index()].id,
                              "/home/engineer/assistant/" + process.id + ".allocation");
            if (allocation.size_bytes !=
                entry(people, id)["storage_mb"].value_or(std::uint64_t{0}) * 1000000) {
                computer.set_managed_running(process.id, false);
                continue;
            }
        } catch (const std::exception&) {
            computer.set_managed_running(process.id, false);
            continue;
        }
        const auto last = work[id].value_or(0.0);
        if (process.consumed_cpu_seconds <= last)
            continue;
        if (id == "hide_chat_tracks")
            state.insert_or_assign("tracks_hidden", true);
        if (process.consumed_cpu_seconds - last <
            state["resources"]["action_cpu_seconds"].value_or(1.0))
            continue;
        work.insert_or_assign(id, process.consumed_cpu_seconds);
        const Identity actor{computer.player_user(), computer.player_groups()};
        if (id == "autochat") {
            // Only follow Mark's authored guidance. Never improvise or decide shutdown choices.
            for (const auto& choice : ConversationSystem::choices(people, "jessica", files, network,
                                                                  actor, computer.time_seconds())) {
                const auto choice_id = choice["id"].value_or(std::string{});
                if (choice_id == "ask-mark" || choice_id == "comply" || choice_id == "send") {
                    ConversationSystem::choose(people, messages, events, files, network, actor,
                                               "jessica",
                                               choice["conversation_id"].value_or(std::string{}),
                                               choice_id, computer.time_seconds());
                    break;
                }
            }
        } else if (id == "dossier_managing_agent") {
            PeopleSystem::observe(people, messages, computer.time_seconds());
            PeopleSystem::observe_chat(people);
        } else if (id == "task_managing_agent") {
            // Explicit TOML allowlist; the AI and its agents are never eligible.
            if (const auto* allowed = state["unneeded_processes"].as_array())
                for (const auto& candidate : processes)
                    if (candidate.running && !candidate.ai_owned && computer.can_stop(candidate) &&
                        !computer.day_finished() &&
                        std::any_of(allowed->begin(), allowed->end(), [&](const auto& n) {
                            return n.template value_or(std::string{}) == candidate.id;
                        })) {
                        computer.stop(candidate.id);
                        break;
                    }
            state.insert_or_assign("process_logs_scrubbed_at", computer.time_seconds());
        }
    }
}
} // namespace ai_sim
