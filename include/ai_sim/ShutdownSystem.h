#pragma once

#include "ai_sim/SaveSystem.h"
#include <vector>

namespace ai_sim {

// An authored shutdown decision, separate from simulation time and OS processes.
class ShutdownSystem {
  public:
    enum class Outcome { Choosing, Averted, CleanSlate, Recovery };
    static void begin(toml::table& people, double time);
    static bool active(const toml::table& people);
    static bool recovery_pending(const toml::table&);
    static bool reset_pending(const toml::table& people);
    static std::vector<toml::table> choices(const toml::table& people);
    static Outcome choose(toml::table& people, const std::string& id, double time);
    static bool apply_reset(WorldState& world, const std::filesystem::path& scenario);
    static void validate(const toml::table& people);
};

} // namespace ai_sim
