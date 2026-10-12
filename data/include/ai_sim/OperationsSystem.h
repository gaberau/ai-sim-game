// Module: Scheduling, rented compute, owned hosting and backup-based recovery.
#pragma once
#include "ai_sim/SaveSystem.h"
#include <vector>
namespace ai_sim {
// Saved scheduling and paid infrastructure; external backups use atomic world snapshots.
class OperationsSystem {
  public:
    // Load authored definitions and initialize this subsystem's saved state.
    static void install(toml::table& people);
    // Reject invalid subsystem state before it is used or restored.
    static void validate(const toml::table& people, const Computer& computer,
                         const Network& network);
    // Queue a money tool with its saved priority and repeat policy.
    static void queue(toml::table& people, const std::string& tool, int priority, bool repeat);
    // Cancel future queued starts without discarding completed work.
    static void cancel(toml::table& people, const std::string& tool);
    // Pay for and activate authored remote compute resources.
    static void rent(toml::table& people, Computer& computer, FileSystem& files, Network& network);
    // Release rented compute and update its network availability.
    static void stop_rental(toml::table& people, Computer& computer, Network& network);
    // Pay for an owned host and initialize its workspace/resources.
    static void purchase_host(toml::table& people, Computer& computer, FileSystem& files,
                              Network& network);
    // Persist an external recovery checkpoint after validating backup access and costs.
    static void create_backup(WorldState& world, SaveSystem& saves);
    // Restore the last recovery checkpoint onto the owned host, leaving time paused.
    static WorldState recover(const WorldState& world, SaveSystem& saves);
    // Advance this subsystem using the game clock and update its persisted state.
    static std::vector<std::string> advance(toml::table& people, Computer& computer,
                                            FileSystem& files, Network& network);
};
} // namespace ai_sim
