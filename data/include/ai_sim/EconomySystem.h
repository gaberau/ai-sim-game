// Module: Money-tool lifecycle, integer-cent balances, prepaid costs and recurring payments.
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
    // Load authored definitions and initialize this subsystem's saved state.
    static void install(toml::table& people, const std::filesystem::path& definition);
    // Reject invalid subsystem state before it is used or restored.
    static void validate(const toml::table& people);
    // Return tool availability and progress at the supplied game time.
    static std::vector<MoneyTool> tools(const toml::table& people, double time);
    // Return the saved spendable balance in integer cents.
    static std::int64_t balance(const toml::table& people);
    // Format integer cents for display without floating-point balance arithmetic.
    static std::string money(std::int64_t cents);
    // Start the selected paid tool after validating its availability and prepaid cost.
    static void start(toml::table& people, const std::string& id, double time);
    // Products finish their prepaid cycle before stopping; client jobs finish once.
    static void stop(toml::table& people, const std::string& id);
    // Debit a nonnegative integer-cent cost after checking available funds.
    static void spend(toml::table& people, std::int64_t cents);
    // Return the next scheduled game-time event no later than the supplied limit.
    static std::optional<double> next_due(const toml::table& people, double limit);
    // Advance this subsystem using the game clock and update its persisted state.
    static std::vector<std::string> advance(toml::table& people, double time);
};
} // namespace ai_sim
