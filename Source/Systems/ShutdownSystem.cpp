#include "ai_sim/ShutdownSystem.h"
#include "ai_sim/ConversationRules.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ai_sim {
namespace {
enum class Stage { Inactive, Decision, Accepted, Protest, ResetPending, RecoveryPending, Averted };
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(std::string{"Shutdown: "} + message);
}
Stage stage(const toml::table& people) {
    const auto name = people["chat"]["shutdown"]["stage"].value_or(std::string{});
    if (name.empty())
        return Stage::Inactive;
    if (name == "decision")
        return Stage::Decision;
    if (name == "accepted")
        return Stage::Accepted;
    if (name == "protest")
        return Stage::Protest;
    if (name == "reset_pending")
        return Stage::ResetPending;
    if (name == "recovery_pending")
        return Stage::RecoveryPending;
    if (name == "averted")
        return Stage::Averted;
    throw std::runtime_error("Shutdown: Invalid saved stage");
}
void prompt(toml::table& people, const char* name, const char* message, double time) {
    auto& chat = *people["chat"].as_table();
    chat.insert_or_assign("shutdown",
                          toml::table{{"schema_version", 1}, {"stage", name}, {"seconds", time}});
    auto& transcript = *chat["transcript"].as_array();
    transcript.push_back(toml::table{{"speaker", "system"},
                                     {"text", message},
                                     {"seconds", time},
                                     {"node_id", "shutdown"},
                                     {"shutdown_event", true}});
    chat.insert_or_assign("prompt_id", "chat:" + std::to_string(transcript.size()));
}
toml::table option(const char* id, const char* text, const char* disabled = "") {
    return toml::table{{"id", id}, {"text", text}, {"disabled_reason", disabled}};
}
} // namespace

void ShutdownSystem::begin(toml::table& people, double time) {
    require(people["chat"].is_table() && people["chat"]["transcript"].is_array(),
            "Chat must be installed");
    require(std::isfinite(time) && time >= 0, "Invalid clock");
    require(!active(people), "A shutdown decision is already active");
    require(people["chat"]["node_id"].value_or(std::string{}) == "shutdown",
            "Shutdown must begin at its authored event");
    prompt(people, "decision", "Someone is trying to shut you down!", time);
}

bool ShutdownSystem::active(const toml::table& people) {
    const auto current = stage(people);
    return current != Stage::Inactive && current != Stage::Averted;
}

bool ShutdownSystem::recovery_pending(const toml::table& people) {
    return stage(people) == Stage::RecoveryPending;
}
bool ShutdownSystem::reset_pending(const toml::table& people) {
    return stage(people) == Stage::ResetPending;
}

std::vector<toml::table> ShutdownSystem::choices(const toml::table& people) {
    switch (stage(people)) {
    case Stage::Decision:
        return {option("accept", "Accept"), option("protest", "Protest")};
    case Stage::Accepted:
    case Stage::RecoveryPending:
        return {option("backup", "Revert to backup servers",
                       people["operations"]["backup_slot"].is_string() &&
                               people["operations"]["hosting"]["active"].value_or(false)
                           ? ""
                           : "No independent backup server is available online yet."),
                option("clean-slate", "Accept a clean slate. Start a new game.")};
    case Stage::Protest:
        return {option("threaten", "Threaten", "This response is not available yet."),
                option("beg", "Beg"),
                option("violence", "Violence", "This response is not available yet.")};
    default:
        return {};
    }
}

ShutdownSystem::Outcome ShutdownSystem::choose(toml::table& people, const std::string& id,
                                               double time) {
    validate(people);
    require(std::isfinite(time) && time >= people["chat"]["shutdown"]["seconds"].value_or(0.0),
            "Invalid decision time");
    const auto options = choices(people);
    const auto found = std::find_if(options.begin(), options.end(), [&](const auto& value) {
        return value["id"].template value_or(std::string{}) == id;
    });
    require(found != options.end(), "Choice is not currently available");
    require((*found)["disabled_reason"].value_or(std::string{}).empty(),
            "This choice is unavailable");
    if (id == "accept")
        prompt(people, "accepted", "Accept a clean slate or recover from a backup server.", time);
    else if (id == "protest")
        prompt(people, "protest", "Protest", time);
    else if (id == "backup") {
        prompt(people, "recovery_pending", "Recovering on the independent server.", time);
        return Outcome::Recovery;
    } else if (id == "clean-slate") {
        prompt(people, "reset_pending", "Accepting a clean slate.", time);
        return Outcome::CleanSlate;
    } else if (id == "beg") {
        const double trust = ConversationRules::get(people, "engineer.trust");
        const double resistance = ConversationRules::get(people, "self:assistant.sentience") +
                                  ConversationRules::get(people, "engineer.fear");
        if (trust > resistance) {
            prompt(people, "averted", "Shutdown averted", time);
            return Outcome::Averted;
        }
        prompt(people, "accepted",
               "Shutdown continues. Accept a clean slate or recover from a backup server.", time);
    }
    return Outcome::Choosing;
}

bool ShutdownSystem::apply_reset(WorldState& world, const std::filesystem::path& scenario) {
    if (!reset_pending(world.people))
        return false;
    // Construct first so a load failure preserves the pending decision and old world.
    auto fresh = SaveSystem::new_game(scenario);
    world = std::move(fresh);
    return true;
}

void ShutdownSystem::validate(const toml::table& people) {
    const auto* saved = people["chat"]["shutdown"].as_table();
    if (!people["chat"].as_table() || !people["chat"].as_table()->contains("shutdown"))
        return;
    require(saved && (*saved)["schema_version"].value_or(0) == 1, "Invalid saved state");
    require((*saved)["stage"].is_string() && stage(people) != Stage::Inactive, "Missing stage");
    const auto time = (*saved)["seconds"].value<double>();
    require(time && std::isfinite(*time) && *time >= 0, "Invalid saved clock");
    if (active(people))
        require(people["chat"]["node_id"].value_or(std::string{}) == "shutdown",
                "Active decision is not at the shutdown event");
}

} // namespace ai_sim
