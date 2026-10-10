#pragma once
#include "ai_sim/SaveSystem.h"
#include <optional>
namespace ai_sim {
class PhaseWorkSystem {
public:
    static void install(WorldState&, const std::filesystem::path&);
    static void validate(const toml::table&);
    static std::optional<double> next_due(const toml::table&, double limit, bool include_commitments = true);
    static void advance(toml::table&, toml::table& emails, double time);
};
}
