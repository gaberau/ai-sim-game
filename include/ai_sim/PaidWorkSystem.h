#pragma once
#include "ai_sim/Computer.h"
#include "ai_sim/FileSystem.h"
#include <toml++/toml.hpp>
namespace ai_sim {
// Resource-backed work sits alongside the legacy timer model for old saves.
class PaidWorkSystem {
  public:
    // Atomically charge the first cycle and create its files and process.
    static void start(toml::table& people, Computer& computer, FileSystem& files,
                      const Network& network, const std::string& id, const std::string& host = {});
    // Reconcile CPU work, availability, and output before settling payments.
    static void prepare(toml::table& people, Computer& computer, FileSystem& files,
                        const Network& network);
    // Release completed clients' resources or begin a product's next cycle.
    static void finish(toml::table& people, Computer& computer, FileSystem& files,
                       const Network& network);
    static void release_storage(toml::table& people, const Computer& computer, FileSystem& files,
                                const Network& network, const std::string& id);
    static void validate(const toml::table& people, const Computer& computer,
                         const FileSystem& files, const Network& network);
    // Membership only; filesystem access and availability are checked at deployment.
    static bool allows_deployment(const toml::table& people, const Network& network,
                                  const std::string& host);
    static bool active(const toml::table& people);
};
} // namespace ai_sim
