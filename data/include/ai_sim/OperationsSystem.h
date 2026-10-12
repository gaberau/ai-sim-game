#pragma once
#include "ai_sim/SaveSystem.h"
#include <vector>
namespace ai_sim {
// Saved scheduling and paid infrastructure; external backups use atomic world snapshots.
class OperationsSystem {
  public:
    static void install(toml::table& people);
    static void validate(const toml::table& people, const Computer& computer,
                         const Network& network);
    static void queue(toml::table& people, const std::string& tool, int priority, bool repeat);
    static void cancel(toml::table& people, const std::string& tool);
    static void rent(toml::table& people, Computer& computer, FileSystem& files, Network& network);
    static void stop_rental(toml::table& people, Computer& computer, Network& network);
    static void purchase_host(toml::table& people, Computer& computer, FileSystem& files,
                              Network& network);
    static void create_backup(WorldState& world, SaveSystem& saves);
    static WorldState recover(const WorldState& world, SaveSystem& saves);
    static std::vector<std::string> advance(toml::table& people, Computer& computer,
                                            FileSystem& files, Network& network);
};
} // namespace ai_sim
