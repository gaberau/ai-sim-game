// Module: Agent deployment, resource budgets, allocation shortages and repairable installations.
#pragma once
#include "ai_sim/SaveSystem.h"
#include <vector>
namespace ai_sim {
struct AgentStatus {
    std::string id, label, status, task, bottleneck;
    bool built = false, deployed = false, running = false;
    // CPU rates use TFLOP/s; RAM/storage values use decimal MB, matching authored budgets.
    double requested_tflops = 0, limit_tflops = 0, allocated_tflops = 0, minimum_ram_mb = 0,
           recommended_ram_mb = 0, ram_mb = 0, storage_mb = 0, ram_used_mb = 0, storage_used_mb = 0;
    int priority = 1;
};
class AgentSystem {
  public:
    // Combine installed state and current resource allocations into display-ready agent status.
    static std::vector<AgentStatus> statuses(const toml::table&, const Computer&, const FileSystem&,
                                             const Network&);
    // Persist CPU/RAM budgets and priority, then update an existing managed process.
    static void configure(toml::table&, Computer&, const std::string&, double cpu_tflops,
                          std::int64_t ram_mb, int priority);
    // Install or resume an agent; report shortages without inventing available resources.
    static std::vector<std::string> deploy(toml::table&, Computer&, FileSystem&, const Network&,
                                           const std::string&);
    // Pause this subsystem; agent stop mode also releases running allocations.
    static void pause(toml::table&, Computer&, const std::string&, bool stop = false);
    // Restore a deployed agent's managed storage allocation while retaining its history.
    static void repair(toml::table&, Computer&, FileSystem&, const Network&, const std::string&);
    // Synchronize subsystem state at the current game clock.
    static std::vector<std::string> update(toml::table&, Computer&, FileSystem&, const Network&);
    // Reject invalid subsystem state before it is used or restored.
    static void validate(const toml::table&);
};
} // namespace ai_sim
