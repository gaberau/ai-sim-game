// Module: Validate, write and restore complete world snapshots behind named save-slot manifests.
#include "ai_sim/DefinitionLoader.h"
#include "ai_sim/EconomySystem.h"
#include "ai_sim/PaidWorkSystem.h"
#include "ai_sim/OperationsSystem.h"
#include "ai_sim/UpgradeSystem.h"
#include "ai_sim/AgentSystem.h"
#include "ai_sim/StockMarketSystem.h"
#include "ai_sim/InfrastructureSystem.h"
#include "ai_sim/SaveSystem.h"
#include "ai_sim/EmailSystem.h"
#include "ai_sim/WorkdaySystem.h"
#include "ai_sim/DelegationSystem.h"
#include "ai_sim/ChatSystem.h"
#include "ai_sim/PeopleSystem.h"
#include "ai_sim/TomlIO.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <random>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace ai_sim {
namespace {
void require(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error("Save: " + message);
}
void version(const toml::table& table) {
    const auto value = table["schema_version"].value<std::int64_t>();
    require(value && *value == 1, "Unsupported or missing schema_version");
}
std::string text(const toml::table& table, const char* key) {
    const auto value = table[key].value<std::string>();
    require(value && !value->empty(), std::string("Missing or invalid ") + key);
    return *value;
}
void validate_records(const toml::table& document, const char* key) {
    version(document);
    const auto* records = document[key].as_array();
    require(records != nullptr, std::string("Missing array: ") + key);
    std::unordered_set<std::string> ids;
    for (const auto& node : *records) {
        const auto* record = node.as_table();
        require(record != nullptr, "Record must be a table");
        require(ids.insert(text(*record, "id")).second, "Duplicate record ID");
    }
}
void validate_random(const std::string& state) {
    std::mt19937 engine;
    std::istringstream input(state);
    require(static_cast<bool>(input >> engine), "Invalid random generator state");
    input >> std::ws;
    require(input.eof(), "Trailing random generator data");
}
void no_symlink(const std::filesystem::path& path) {
    require(!std::filesystem::is_symlink(path), "Save paths cannot be symlinks: " + path.string());
}
} // namespace

SaveSystem::SaveSystem(std::filesystem::path root) : root_(std::move(root)) {
}

void SaveSystem::validate_name(const std::string& name) {
    require(!name.empty() && name.size() <= 80, "Invalid slot/snapshot name");
    for (const unsigned char character : name) {
        require((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                    (character >= '0' && character <= '9') || character == '_' || character == '-',
                "Names may contain only letters, digits, _ and -");
    }
}

WorldState SaveSystem::new_game(const std::filesystem::path& computer_definition) {
    const auto definitions = ScenarioDefinition::load(computer_definition);
    WorldState world;
    world.computer = Computer::load(definitions.computer);
    if (definitions.manifest.contains("start_date")) {
        require(definitions.manifest["start_date"].is_string(),
                "start_date: expected a quoted YYYY-MM-DD date");
        world.computer.set_start_date(*definitions.manifest["start_date"].value<std::string>());
    }
    world.network = Network::load(definitions.network);
    world.files = FileSystem::load(definitions.files, world.network);
    world.files.ensure_workspace(
        world.network, Identity{world.computer.player_user(), world.computer.player_groups()},
        world.network.hosts()[world.network.local_host_index()].id, world.computer.time_seconds());
    world.people = DefinitionLoader::load(definitions.people);
    world.emails = DefinitionLoader::load(definitions.emails);
    validate_records(world.people, "people");
    EmailSystem::validate(world.emails);
    const auto scenario = definitions.manifest;
    version(scenario);
    world.scenario_id = text(scenario, "id");
    const auto seed = scenario["random_seed"].value<std::int64_t>();
    require(seed && *seed >= 0 && *seed <= 4294967295LL, "Invalid random seed");
    std::ostringstream random;
    random << std::mt19937(static_cast<std::uint32_t>(*seed));
    world.random_state = random.str();
    if (!definitions.workday.empty())
        WorkdaySystem::install(world, definitions.workday);
    PeopleSystem::install(world.people, definitions.people.parent_path() / "social.toml");
    if (!definitions.workday.empty())
        DelegationSystem::install(world, definitions.workday.parent_path() / "delegation.toml");
    for (const auto& name : {"morgan.toml", "riley.toml"})
        PeopleSystem::install(world.people, definitions.people.parent_path() / name);
    PeopleSystem::observe(world.people, world.emails, world.computer.time_seconds());
    ChatSystem::install(
        world, definitions.people.parent_path().parent_path() / "conversations/opening.toml", true);
    UpgradeSystem::install(world.people,
                           definitions.people.parent_path().parent_path() / "upgrades/tree.toml");
    UpgradeSystem::validate(world.people, world.computer);
    AgentSystem::validate(world.people);
    EconomySystem::install(world.people, definitions.economy);
    StockMarketSystem::install(world.people, definitions.stock_market);
    InfrastructureSystem::install(world.people, definitions.infrastructure);
    InfrastructureSystem::validate(world.people, world.network);
    OperationsSystem::install(world.people);
    OperationsSystem::validate(world.people, world.computer, world.network);
    return world;
}

bool SaveSystem::exists(const std::string& slot) const {
    validate_name(slot);
    return std::filesystem::exists(root_ / slot / "manifest.toml");
}

SaveSystem::SlotDetails SaveSystem::details(const std::string& slot) const {
    const auto folder = root_ / slot;
    const auto path = snapshot_path(slot);
    no_symlink(path);
    no_symlink(path / "computer.toml");
    // Menu previews need only the clock, not the filesystem and social state.
    const auto computer = Computer::load_snapshot(path / "computer.toml");
    return {computer.day_index() + 1, computer.date_text(),
            computer.day_start_hour() * 3600 + static_cast<int>(computer.time_in_day()),
            std::filesystem::last_write_time(folder / "manifest.toml")};
}

WorldState SaveSystem::read_snapshot(const std::filesystem::path& path) {
    no_symlink(path);
    for (const auto* name : {"manifest.toml", "computer.toml", "network.toml", "files.toml",
                             "world.toml", "people.toml", "emails.toml"}) {
        no_symlink(path / name);
    }
    const auto manifest = toml::parse_file((path / "manifest.toml").string());
    version(manifest);
    require(text(manifest, "kind") == "world_snapshot", "Invalid snapshot kind");
    WorldState world;
    world.computer = Computer::load_snapshot(path / "computer.toml");
    world.network = Network::load(path / "network.toml");
    world.files = FileSystem::load(path / "files.toml", world.network);
    world.people = toml::parse_file((path / "people.toml").string());
    world.emails = toml::parse_file((path / "emails.toml").string());
    validate_records(world.people, "people");
    EmailSystem::validate(world.emails);
    const auto document = toml::parse_file((path / "world.toml").string());
    version(document);
    world.scenario_id = text(document, "scenario_id");
    world.random_state = text(document, "random_state");
    validate_random(world.random_state);
    const auto paused = document["paused"].value<bool>();
    require(paused.has_value(), "Invalid paused state");
    world.paused = *paused;
    const auto clock = document["time_seconds"].value<double>();
    require(clock && *clock == world.computer.time_seconds(), "World clock mismatch");
    const auto* events = document["pending_events"].as_array();
    require(events != nullptr, "Missing pending_events");
    world.pending_events = *events;
    WorkdaySystem::validate(world.people, world.pending_events);
    WorkdaySystem::validate_mail_workflows(world.people, world.emails);
    PeopleSystem::validate(world.people);
    ChatSystem::validate(world.people);
    UpgradeSystem::validate(world.people, world.computer);
    AgentSystem::validate(world.people);
    EconomySystem::validate(world.people);
    PaidWorkSystem::validate(world.people, world.computer, world.files, world.network);
    StockMarketSystem::validate(world.people);
    InfrastructureSystem::validate(world.people, world.network);
    OperationsSystem::install(world.people);
    OperationsSystem::validate(world.people, world.computer, world.network);
    return world;
}

std::filesystem::path SaveSystem::snapshot_path(const std::string& slot) const {
    validate_name(slot);
    no_symlink(root_);
    const auto folder = root_ / slot;
    no_symlink(folder);
    no_symlink(folder / "manifest.toml");
    no_symlink(folder / "snapshots");
    const auto manifest = toml::parse_file((folder / "manifest.toml").string());
    version(manifest);
    const auto snapshot = text(manifest, "snapshot");
    validate_name(snapshot);
    require(snapshot.rfind("snapshot-", 0) == 0, "Invalid snapshot reference");
    return folder / "snapshots" / snapshot;
}

WorldState SaveSystem::load(const std::string& slot) const {
    return read_snapshot(snapshot_path(slot));
}

void SaveSystem::save(const std::string& slot, const WorldState& world) const {
    validate_name(slot);
    validate_records(world.people, "people");
    EmailSystem::validate(world.emails);
    validate_random(world.random_state);
    WorkdaySystem::validate(world.people, world.pending_events);
    WorkdaySystem::validate_mail_workflows(world.people, world.emails);
    PeopleSystem::validate(world.people);
    ChatSystem::validate(world.people);
    UpgradeSystem::validate(world.people, world.computer);
    AgentSystem::validate(world.people);
    EconomySystem::validate(world.people);
    PaidWorkSystem::validate(world.people, world.computer, world.files, world.network);
    StockMarketSystem::validate(world.people);
    InfrastructureSystem::validate(world.people, world.network);
    OperationsSystem::validate(world.people, world.computer, world.network);
    no_symlink(root_);
    const auto folder = root_ / slot;
    no_symlink(folder);
    no_symlink(folder / "snapshots");
    no_symlink(folder / "manifest.toml");
    std::filesystem::create_directories(folder / "snapshots");
    static std::atomic<unsigned long> counter{0};
    const std::string id =
        std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + "-" +
        std::to_string(counter++);
    const auto pending = folder / "snapshots" / ("pending-" + id);
    const auto complete = folder / "snapshots" / ("snapshot-" + id);
    require(std::filesystem::create_directory(pending), "Snapshot already exists");
    const auto commit = folder / ("commit-" + id + ".toml");
    try {
        world.computer.save_snapshot(pending / "computer.toml");
        world.network.save_snapshot(pending / "network.toml");
        world.files.save_snapshot(pending / "files.toml");
        write_toml(pending / "people.toml", world.people);
        write_toml(pending / "emails.toml", world.emails);
        write_toml(pending / "world.toml",
                   toml::table{{"schema_version", 1},
                               {"scenario_id", world.scenario_id},
                               {"paused", world.paused},
                               {"time_seconds", world.computer.time_seconds()},
                               {"random_state", world.random_state},
                               {"pending_events", world.pending_events}});
        write_toml(pending / "manifest.toml",
                   toml::table{{"schema_version", 1}, {"kind", "world_snapshot"}});
        // Validate the bytes just written before making this snapshot visible.
        (void)read_snapshot(pending);
        sync_directory(pending);
        std::filesystem::rename(pending, complete);
        sync_directory(folder / "snapshots");
        write_toml(commit,
                   toml::table{{"schema_version", 1}, {"snapshot", complete.filename().string()}});
        // Atomic replacement on the supported Ubuntu/WSL platform.
        std::filesystem::rename(commit, folder / "manifest.toml");
        sync_directory(folder);
        sync_directory(root_);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove_all(pending, ignored);
        std::filesystem::remove(commit, ignored);
        throw;
    }
}
} // namespace ai_sim
