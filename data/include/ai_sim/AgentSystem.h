#pragma once
#include "ai_sim/SaveSystem.h"
#include <vector>
namespace ai_sim {
struct AgentStatus {
    std::string id, label, status, task, bottleneck;
    bool built = false, deployed = false, running = false;
    double requested_tflops = 0, limit_tflops = 0, allocated_tflops = 0, minimum_ram_mb = 0,
           recommended_ram_mb = 0, ram_mb = 0, storage_mb = 0, ram_used_mb = 0, storage_used_mb = 0;
    int priority = 1;
};
class AgentSystem {
  public:
    static std::vector<AgentStatus> statuses(const toml::table&, const Computer&, const FileSystem&,
                                             const Network&);
    static void configure(toml::table&, Computer&, const std::string&, double cpu_tflops,
                          std::int64_t ram_mb, int priority);
    static std::vector<std::string> deploy(toml::table&, Computer&, FileSystem&, const Network&,
                                           const std::string&);
    static void pause(toml::table&, Computer&, const std::string&, bool stop = false);
    static void repair(toml::table&, Computer&, FileSystem&, const Network&, const std::string&);
    static std::vector<std::string> update(toml::table&, Computer&, FileSystem&, const Network&);
    static void validate(const toml::table&);
};
} // namespace ai_sim
