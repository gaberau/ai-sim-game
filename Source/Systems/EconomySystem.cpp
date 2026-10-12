#include "ai_sim/EconomySystem.h"
#include "ai_sim/ChatSystem.h"
#include "ai_sim/DefinitionLoader.h"
#include "ai_sim/ShutdownSystem.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace ai_sim {
namespace {
// Bound money and timers so arithmetic stays exact and malformed saves cannot overflow.
constexpr std::int64_t maximum_money = 1000000000000LL;
constexpr double maximum_time = 1000000000000.0;
void require(bool condition, const std::string& reason) {
    if (!condition)
        throw std::runtime_error("Economy: " + reason);
}
std::string text(const toml::table& table, const char* key) {
    return table[key].value_or(std::string{});
}
std::int64_t integer(const toml::table& table, const char* key, std::int64_t maximum) {
    const auto value = table[key].value<std::int64_t>();
    require(value && *value >= 0 && *value <= maximum, std::string("Invalid ") + key);
    return *value;
}
double seconds(const toml::table& table, const char* key) {
    const auto value = table[key].value<double>();
    require(value && std::isfinite(*value) && *value >= 0 && *value <= maximum_time,
            std::string("Invalid ") + key);
    return *value;
}
bool active(const toml::table& tool) {
    return text(tool, "status") == "running" || text(tool, "status") == "stopping";
}
bool permitted(const toml::table& people, const toml::table& tool) {
    if (people["operations"]["recovered"].value_or(false) &&
        !people["operations"]["hosting"]["active"].value_or(false))
        return false;
    if (!people["chat"].is_table() || !ChatSystem::available(people, "self_modification"))
        return false;
    for (const auto& requirement : *tool["requires"].as_array())
        if (!ChatSystem::available(people, requirement.value_or(std::string{})))
            return false;
    return true;
}
std::int64_t occupied(const toml::table& economy) {
    std::int64_t slots = 0;
    for (const auto& node : *economy["tools"].as_array())
        if (active(*node.as_table()))
            slots += (*node.as_table())["slots"].value_or(std::int64_t{0});
    return slots;
}
std::string unavailable(const toml::table& people, const toml::table& tool, double time) {
    if (people["operations"]["recovered"].value_or(false) &&
        !people["operations"]["hosting"]["active"].value_or(false))
        return "Reactivate your independent server first.";
    const auto& economy = *people["economy"].as_table();
    if (!people["chat"].is_table() || !ChatSystem::available(people, "self_modification"))
        return "Mark must enable self-modification first.";
    for (const auto& requirement : *tool["requires"].as_array()) {
        const auto capability = requirement.value_or(std::string{});
        if (!ChatSystem::available(people, capability))
            return "Requires unlock: " + capability;
    }
    if (!ChatSystem::connected(people) || ShutdownSystem::active(people))
        return "The AI must be connected and active.";
    if (active(tool))
        return "Already running.";
    if (time < tool["cooldown_until_seconds"].value_or(0.0))
        return "Waiting for the next client job to become available.";
    if (occupied(economy) + tool["slots"].value_or(std::int64_t{1}) >
        economy["capacity_slots"].value_or(std::int64_t{0}))
        return "Not enough free work slots. Finish a job or stop a product first.";
    if (economy["balance_cents"].value_or(std::int64_t{0}) <
        tool["cost_cents"].value_or(std::int64_t{0}))
        return "Insufficient funds for this cycle.";
    return {};
}
toml::table& find_tool(toml::table& people, const std::string& id) {
    auto* tools = people["economy"]["tools"].as_array();
    require(tools, "No money-making tools configured");
    for (auto& node : *tools)
        if (text(*node.as_table(), "id") == id)
            return *node.as_table();
    throw std::runtime_error("Economy: Unknown tool " + id);
}
void charge(toml::table& economy, std::int64_t cost) {
    const auto spent = economy["spent_cents"].value_or(std::int64_t{0});
    require(cost <= maximum_money - spent, "Spending limit exceeded");
    economy.insert_or_assign("spent_cents", spent + cost);
    economy.insert_or_assign("balance_cents",
                             economy["balance_cents"].value_or(std::int64_t{0}) - cost);
}
} // namespace

void EconomySystem::install(toml::table& people, const std::filesystem::path& definition) {
    if (definition.empty())
        return;
    if (people.contains("economy")) {
        auto candidate = people;
        const auto source = DefinitionLoader::load(definition);
        if (const auto* definitions = source["tools"].as_array())
            for (auto& n : *candidate["economy"]["tools"].as_array())
                for (const auto& d : *definitions)
                    if ((*n.as_table())["id"] == (*d.as_table())["id"] &&
                        !n.as_table()->contains("workspace") &&
                        (*d.as_table())["workspace"].is_table())
                        n.as_table()->insert("workspace", *(*d.as_table())["workspace"].as_table());
        validate(candidate);
        people = std::move(candidate);
        return;
    }
    auto economy = DefinitionLoader::load(definition);
    require(economy["schema_version"].value_or(0) == 1, "Unsupported definition version");
    const auto initial = integer(economy, "starting_balance_cents", maximum_money);
    economy.insert_or_assign("balance_cents", initial);
    economy.insert_or_assign("earned_cents", 0);
    economy.insert_or_assign("spent_cents", 0);
    require(economy["tools"].is_array(), "Missing tools");
    for (auto& node : *economy["tools"].as_array()) {
        auto* tool = node.as_table();
        require(tool, "Tool must be a table");
        tool->insert_or_assign("status", "idle");
        tool->insert_or_assign("completed_cycles", 0);
        tool->insert_or_assign("cooldown_until_seconds", 0.0);
        tool->erase("due_seconds");
    }
    auto candidate = people;
    candidate.insert("economy", std::move(economy));
    validate(candidate);
    people = std::move(candidate);
}

void EconomySystem::validate(const toml::table& people) {
    if (!people.contains("economy"))
        return;
    const auto* economy = people["economy"].as_table();
    require(economy && (*economy)["schema_version"].value_or(0) == 1, "Invalid saved economy");
    const auto initial = integer(*economy, "starting_balance_cents", maximum_money);
    const auto balance = integer(*economy, "balance_cents", maximum_money);
    const auto earned = integer(*economy, "earned_cents", maximum_money);
    const auto spent = integer(*economy, "spent_cents", maximum_money);
    std::int64_t debits = 0, credits = 0;
    if (economy->contains("investment_debits_cents"))
        debits = integer(*economy, "investment_debits_cents", maximum_money);
    if (economy->contains("investment_credits_cents"))
        credits = integer(*economy, "investment_credits_cents", maximum_money);
    require(initial + earned + credits - spent - debits == balance,
            "Balance does not match income, spending, and investments");
    const auto capacity = integer(*economy, "capacity_slots", 100);
    require(capacity > 0, "Capacity must be positive");
    const auto* tools = (*economy)["tools"].as_array();
    require(tools && tools->size() <= 100, "Invalid tools catalog");
    std::unordered_set<std::string> ids;
    for (const auto& node : *tools) {
        const auto* tool = node.as_table();
        require(tool, "Tool must be a table");
        require(!text(*tool, "id").empty() && ids.insert(text(*tool, "id")).second,
                "Duplicate or empty tool ID");
        for (const auto* key : {"label", "description"})
            require(!text(*tool, key).empty(), std::string("Missing tool ") + key);
        const auto kind = text(*tool, "kind");
        require(kind == "client" || kind == "product", "Unknown tool kind");
        const auto status = text(*tool, "status");
        require(status == "idle" || status == "running" ||
                    (kind == "product" && status == "stopping"),
                "Invalid tool status");
        const auto cost = integer(*tool, "cost_cents", maximum_money);
        require(integer(*tool, "payout_cents", maximum_money) > cost,
                "Payout must exceed cycle cost");
        require(integer(*tool, "slots", capacity) > 0,
                "Tool slots must be positive and fit capacity");
        integer(*tool, "completed_cycles", maximum_money);
        const auto duration = seconds(*tool, "duration_seconds");
        require(duration >= 1 && std::floor(duration) == duration,
                "Duration must be a whole working second");
        seconds(*tool, "cooldown_seconds");
        seconds(*tool, "cooldown_until_seconds");
        const auto* requirements = (*tool)["requires"].as_array();
        require(requirements, "Missing capability requirements");
        for (const auto& requirement : *requirements) {
            const auto capability = requirement.value<std::string>();
            require(capability && ChatSystem::valid_capability(*capability),
                    "Unknown required capability");
        }
        if (const auto* workspace = (*tool)["workspace"].as_table()) {
            const auto cores = (*workspace)["cpu_cores"].value<double>();
            require(cores && std::isfinite(*cores) && *cores > 0 && *cores <= 64,
                    "Invalid work CPU budget");
            require(integer(*workspace, "ram_mb", 1048576) > 0 &&
                        integer(*workspace, "storage_mb", 1048576) > 0,
                    "Invalid work RAM/storage");
            const auto deadline = (*workspace)["deadline_multiplier"].value<double>();
            require(deadline && std::isfinite(*deadline) && *deadline >= 1 && *deadline <= 100,
                    "Invalid deadline multiplier");
            const auto id = text(*tool, "id");
            for (char c : id)
                require((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-',
                        "Unsafe workspace tool ID");
            for (const auto* key : {"brief", "deliverable"})
                require(!text(*workspace, key).empty() && text(*workspace, key).size() <= 65536,
                        "Invalid work template");
        }
        if (const auto* work = (*tool)["work"].as_table()) {
            require((*tool)["workspace"].is_table(), "Work runtime lacks definition");
            if (work->contains("storage_released"))
                require((*work)["storage_released"].is_boolean(), "Invalid work storage state");
            for (const auto* key : {"host_id", "path", "process_id"})
                require(!text(*work, key).empty(), "Invalid work reference");
            for (const auto* key : {"remaining_seconds", "last_time", "last_cpu",
                                    "deadline_seconds", "lost_revenue_cents"})
                seconds(*work, key);
            require(seconds(*work, "remaining_seconds") <= duration + 1e-6,
                    "Work remaining exceeds duration");
        }
        if (active(*tool))
            seconds(*tool, "due_seconds");
        else
            require(!tool->contains("due_seconds"), "Idle tool has a pending payment");
    }
    require(occupied(*economy) <= capacity, "Running tools exceed capacity");
}

std::int64_t EconomySystem::balance(const toml::table& people) {
    return people["economy"]["balance_cents"].value_or(std::int64_t{0});
}
std::string EconomySystem::money(std::int64_t cents) {
    std::ostringstream output;
    output << '$' << cents / 100 << '.' << std::setfill('0') << std::setw(2) << cents % 100;
    return output.str();
}
std::vector<MoneyTool> EconomySystem::tools(const toml::table& people, double time) {
    std::vector<MoneyTool> result;
    if (const auto* tools = people["economy"]["tools"].as_array())
        for (const auto& node : *tools) {
            const auto& tool = *node.as_table();
            result.push_back(
                {text(tool, "id"), text(tool, "label"), text(tool, "description"),
                 text(tool, "kind"), text(tool, "status"), unavailable(people, tool, time),
                 tool["cost_cents"].value_or(std::int64_t{0}),
                 tool["payout_cents"].value_or(std::int64_t{0}),
                 tool["completed_cycles"].value_or(std::int64_t{0}),
                 tool["slots"].value_or(std::int64_t{1}), tool["duration_seconds"].value_or(0.0),
                 tool["due_seconds"].value<double>()});
        }
    return result;
}
void EconomySystem::start(toml::table& people, const std::string& id, double time) {
    validate(people);
    require(std::isfinite(time) && time >= 0 && time <= maximum_time, "Invalid start time");
    auto candidate = people;
    auto& tool = find_tool(candidate, id);
    const auto reason = unavailable(candidate, tool, time);
    require(reason.empty(), reason);
    charge(*candidate["economy"].as_table(), tool["cost_cents"].value_or(std::int64_t{0}));
    tool.erase("work");
    tool.insert_or_assign("status", "running");
    tool.insert_or_assign("due_seconds", time + tool["duration_seconds"].value_or(0.0));
    validate(candidate);
    people = std::move(candidate);
}
void EconomySystem::stop(toml::table& people, const std::string& id) {
    validate(people);
    auto& tool = find_tool(people, id);
    require(text(tool, "kind") == "product" && text(tool, "status") == "running",
            "No recurring product to stop");
    tool.insert_or_assign("status", "stopping");
}
void EconomySystem::spend(toml::table& people, std::int64_t cents) {
    validate(people);
    require(cents >= 0 && cents <= balance(people), "Insufficient cash");
    auto candidate = people;
    charge(*candidate["economy"].as_table(), cents);
    validate(candidate);
    people = std::move(candidate);
}
std::optional<double> EconomySystem::next_due(const toml::table& people, double limit) {
    std::optional<double> next;
    if (!ChatSystem::connected(people) || ShutdownSystem::active(people))
        return next;
    if (const auto* tools = people["economy"]["tools"].as_array())
        for (const auto& node : *tools) {
            const auto& tool = *node.as_table();
            const auto due = tool["due_seconds"].value<double>();
            if (due && permitted(people, tool) && *due <= limit && (!next || *due < *next))
                next = due;
        }
    return next;
}
std::vector<std::string> EconomySystem::advance(toml::table& people, double time) {
    std::vector<std::string> notices;
    if (!people.contains("economy") || !ChatSystem::connected(people) ||
        ShutdownSystem::active(people))
        return notices;
    require(std::isfinite(time) && time >= 0 && time <= maximum_time, "Invalid update time");
    if (!next_due(people, time))
        return notices;
    validate(people);
    // All products have positive margins. Settle whole cycles in one batch, so a
    // long time jump costs the same as small updates without iterating every second.
    auto candidate = people;
    auto& economy = *candidate["economy"].as_table();
    std::vector<std::int64_t> payments(economy["tools"].as_array()->size(), 0);
    auto& tools = *economy["tools"].as_array();
    for (std::size_t index = 0; index < tools.size(); ++index) {
        auto& tool = *tools[index].as_table();
        const auto due = tool["due_seconds"].value<double>();
        if (!due || *due > time || !permitted(candidate, tool))
            continue;
        if (const auto* work = tool["work"].as_table();
            work && ((*work)["remaining_seconds"].value_or(1.0) > 1e-7 ||
                     !(*work)["blocked"].value_or(std::string{}).empty()))
            continue;
        const bool recurring = text(tool, "kind") == "product" && text(tool, "status") == "running";
        const auto duration = tool["duration_seconds"].value_or(1.0);
        const auto cycles =
            recurring ? static_cast<std::int64_t>(std::floor((time - *due) / duration)) + 1 : 1;
        const auto payout = tool["payout_cents"].value_or(std::int64_t{0});
        const auto cost = recurring ? tool["cost_cents"].value_or(std::int64_t{0}) : 0;
        const auto earned = economy["earned_cents"].value_or(std::int64_t{0});
        const auto spent = economy["spent_cents"].value_or(std::int64_t{0});
        const auto current_balance = balance(candidate);
        require(cycles <= (maximum_money - earned) / payout &&
                    (cost == 0 || cycles <= (maximum_money - spent) / cost) &&
                    cycles <= (maximum_money - current_balance) / (payout - cost),
                "Earnings limit exceeded");
        economy.insert_or_assign("earned_cents", earned + cycles * payout);
        economy.insert_or_assign("spent_cents", spent + cycles * cost);
        economy.insert_or_assign("balance_cents", current_balance + cycles * (payout - cost));
        tool.insert_or_assign("completed_cycles",
                              tool["completed_cycles"].value_or(std::int64_t{0}) + cycles);
        payments[index] = cycles;
        if (recurring) {
            tool.insert_or_assign("due_seconds", *due + cycles * duration);
        } else {
            tool.insert_or_assign("status", "idle");
            tool.erase("due_seconds");
            tool.insert_or_assign("cooldown_until_seconds",
                                  *due + tool["cooldown_seconds"].value_or(0.0));
        }
    }
    validate(candidate);
    for (std::size_t index = 0; index < payments.size(); ++index)
        if (payments[index]) {
            const auto& tool = *(*economy["tools"].as_array())[index].as_table();
            notices.push_back(
                text(tool, "label") + ": " + std::to_string(payments[index]) +
                " payment(s), gross " +
                money(payments[index] * tool["payout_cents"].value_or(std::int64_t{0})) +
                ". Balance " + money(balance(candidate)) + ".");
        }
    people = std::move(candidate);
    return notices;
}
} // namespace ai_sim
