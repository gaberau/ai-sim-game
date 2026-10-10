#pragma once

#include <toml++/toml.hpp>
#include <filesystem>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ai_sim {
struct MoneyTool {
    std::string id, label, description, kind, status, unavailable;
    std::int64_t cost_cents = 0, payout_cents = 0, completed_cycles = 0, slots = 1;
    double duration_seconds = 0;
    std::optional<double> due_seconds;
};
class EconomySystem {
  public:
    static void install(toml::table& people, const std::filesystem::path& definition);
    static void validate(const toml::table& people);
    static std::vector<MoneyTool> tools(const toml::table& people, double time);
    static std::int64_t balance(const toml::table& people);
    static std::string money(std::int64_t cents);
    static void start(toml::table& people, const std::string& id, double time);
    // Products finish their prepaid cycle before stopping; client jobs finish once.
    static void stop(toml::table& people, const std::string& id);
    static std::optional<double> next_due(const toml::table& people, double limit);
    static std::vector<std::string> advance(toml::table& people, double time);
};
} // namespace ai_sim
