// Module: Simulated quotes, history, orders, fees and persistent trade accounting.
#pragma once
#include <toml++/toml.hpp>
#include <filesystem>
#include <cstdint>
#include <string>
#include <vector>
namespace ai_sim {
struct StockQuote {
    std::string symbol, name, sector;
    bool watched = false;
    std::int64_t price_cents = 0, previous_cents = 0, shares = 0, basis_cents = 0;
};
class StockMarketSystem {
  public:
    // Load authored definitions and initialize this subsystem's saved state.
    static void install(toml::table& people, const std::filesystem::path& definition);
    // Reject invalid subsystem state before it is used or restored.
    static void validate(const toml::table& people);
    // Advance this subsystem using the game clock and update its persisted state.
    static void advance(toml::table& people, double time);
    // Return current simulated stock quotes and trading availability.
    static std::vector<StockQuote> quotes(const toml::table& people);
    // Return the reason an action is blocked, or an empty string when it is available.
    static std::string unavailable(const toml::table& people);
    // Calculate an integer-cent transaction fee under authored market rules.
    static std::int64_t fee(const toml::table& people);
    // Return realized trading profit/loss from saved trade records.
    static std::int64_t realized(const toml::table& people);
    // Return saved market history for the selected symbol.
    static std::vector<std::int64_t> history(const toml::table& people, const std::string& symbol,
                                             int count = 16);
    // Update the player's saved watchlist.
    static void watch(toml::table& people, const std::string& symbol);
    // Validate and persist the selected market/limit order.
    static void place_order(toml::table& people, const std::string& symbol, std::int64_t shares,
                            bool buying, std::int64_t limit_cents);
    // Cancel an open order and release any associated reservation.
    static void cancel_order(toml::table& people, std::int64_t id);
    // Execute a validated trade using current simulated quotes and integer-cent accounting.
    static void trade(toml::table& people, const std::string& symbol, std::int64_t shares,
                      bool buying);
};
} // namespace ai_sim
