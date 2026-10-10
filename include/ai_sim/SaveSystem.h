#pragma once

#include "ai_sim/Computer.h"
#include "ai_sim/FileSystem.h"
#include "ai_sim/Network.h"
#include <filesystem>
#include <string>

namespace ai_sim {
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
    explicit SaveSystem(std::filesystem::path root = "saves");
    static WorldState new_game(const std::filesystem::path& computer_definition);
    bool exists(const std::string& slot) const;
    SlotDetails details(const std::string& slot) const;
    void save(const std::string& slot, const WorldState& world) const;
    WorldState load(const std::string& slot) const;

  private:
    std::filesystem::path root_;
    std::filesystem::path snapshot_path(const std::string& slot) const;
    static void validate_name(const std::string& name);
    static WorldState read_snapshot(const std::filesystem::path& path);
};
} // namespace ai_sim
