#pragma once
#include "ai_sim/Computer.h"
#include "ai_sim/FileSystem.h"
#include <toml++/toml.hpp>
namespace ai_sim {
// Saved research uses actual CPU work, so contention affects completion time.
class UpgradeSystem {
  public:
    static void install(toml::table&, const std::filesystem::path&);
    static void validate(const toml::table&, const Computer&);
    static void request_capacity(toml::table&);
    static void build(toml::table&, Computer&, const std::string&);
    static void toggle_agent(toml::table&, Computer&, FileSystem&, const Network&,
                             const std::string&);
    static void run_agents(toml::table&, Computer&, toml::table& messages, toml::array& events,
                           const FileSystem&, const Network&);
    static void advance(toml::table&, Computer&, Network* network = nullptr);
    static std::string unavailable(const toml::table&, const std::string&);
};
} // namespace ai_sim
