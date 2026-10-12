// Module: Validate, write and restore complete world snapshots behind named save-slot manifests.
#pragma once

#include "ai_sim/Computer.h"
#include "ai_sim/FileSystem.h"
#include "ai_sim/Network.h"
#include <filesystem>
#include <string>

namespace ai_sim {
// Complete persisted model. UI selections and unsaved editor buffers are not part of a save.
struct WorldState {
    Computer computer;
    Network network;
    FileSystem files;
    toml::table people;
    toml::table emails;
    toml::array pending_events;
    std::string scenario_id;
    std::string random_state;
    bool paused = false;
};

class SaveSystem {
  public:
    struct SlotDetails {
        std::uint64_t day;
        std::string date;
        int clock_seconds;
        std::filesystem::file_time_type saved_at;
    };
    // Initialize this object from the supplied configuration/identity.
    explicit SaveSystem(std::filesystem::path root = "saves");
    // Build a fresh world from a scenario manifest or supported legacy computer definition.
    static WorldState new_game(const std::filesystem::path& computer_definition);
    // Check whether the named save slot has a published snapshot.
    bool exists(const std::string& slot) const;
    // Read slot date/clock metadata without loading the full world.
    SlotDetails details(const std::string& slot) const;
    // Stage a complete snapshot and atomically publish the named slot after validation.
    void save(const std::string& slot, const WorldState& world) const;
    // Resolve the published slot manifest, validate and restore the complete world.
    WorldState load(const std::string& slot) const;

  private:
    std::filesystem::path root_;
    std::filesystem::path snapshot_path(const std::string& slot) const;
    static void validate_name(const std::string& name);
    static WorldState read_snapshot(const std::filesystem::path& path);
};
} // namespace ai_sim
