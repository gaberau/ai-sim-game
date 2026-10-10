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
    static void install(toml::table& people, const std::filesystem::path& definition);
    static void validate(const toml::table& people);
    static void advance(toml::table& people, double time);
    static std::vector<StockQuote> quotes(const toml::table& people);
    static std::string unavailable(const toml::table& people);
    static std::int64_t fee(const toml::table& people);
    static std::int64_t realized(const toml::table& people);
    static std::vector<std::int64_t> history(const toml::table& people, const std::string& symbol,
                                             int count = 16);
    static void watch(toml::table& people, const std::string& symbol);
    static void place_order(toml::table& people, const std::string& symbol, std::int64_t shares,
                            bool buying, std::int64_t limit_cents);
    static void cancel_order(toml::table& people, std::int64_t id);
    static void trade(toml::table& people, const std::string& symbol, std::int64_t shares,
                      bool buying);
};
} // namespace ai_sim
