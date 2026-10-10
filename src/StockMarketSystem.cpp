#include "ai_sim/StockMarketSystem.h"
#include "ai_sim/EconomySystem.h"
#include "ai_sim/ChatSystem.h"
#include "ai_sim/ShutdownSystem.h"
#include "ai_sim/DefinitionLoader.h"
#include <cmath>
#include <stdexcept>
#include <unordered_set>
namespace ai_sim {
namespace {
constexpr std::int64_t money_limit = 1000000000000LL;
void require(bool condition, const std::string& reason) {
    if (!condition)
        throw std::runtime_error("Stock market: " + reason);
}
std::int64_t integer(const toml::table& table, const char* key, std::int64_t maximum) {
    const auto value = table[key].value<std::int64_t>();
    require(value && *value >= 0 && *value <= maximum, std::string("Invalid ") + key);
    return *value;
}
// A stable integer hash makes quotes reproducible across platforms and time-step sizes.
std::int64_t price(const toml::table& market, const toml::table& stock, std::int64_t tick) {
    if (tick == 0)
        return stock["initial_price_cents"].value_or(std::int64_t{0});
    std::uint64_t hash = 14695981039346656037ULL;
    const auto symbol = stock["symbol"].value_or(std::string{});
    for (unsigned char character : symbol) {
        hash ^= character;
        hash *= 1099511628211ULL;
    }
    hash ^= static_cast<std::uint64_t>(market["seed"].value_or(std::int64_t{0}));
    hash ^= static_cast<std::uint64_t>(tick) * 0x9e3779b97f4a7c15ULL;
    hash = (hash ^ (hash >> 30)) * 0xbf58476d1ce4e5b9ULL;
    hash = (hash ^ (hash >> 27)) * 0x94d049bb133111ebULL;
    hash ^= hash >> 31;
    const auto minimum = stock["minimum_price_cents"].value_or(std::int64_t{1});
    const auto maximum = stock["maximum_price_cents"].value_or(std::int64_t{1});
    return minimum +
           static_cast<std::int64_t>(hash % static_cast<std::uint64_t>(maximum - minimum + 1));
}
std::int64_t total_basis(const toml::table& people) {
    std::int64_t basis = 0;
    if (const auto* stocks = people["stock_market"]["stocks"].as_array())
        for (const auto& node : *stocks)
            basis += (*node.as_table())["basis_cents"].value_or(std::int64_t{0});
    return basis;
}
} // namespace
void StockMarketSystem::install(toml::table& people, const std::filesystem::path& definition) {
    if (definition.empty())
        return;
    if (people.contains("stock_market")) {
        validate(people);
        return;
    }
    auto market = DefinitionLoader::load(definition);
    require(market["stocks"].is_array(), "Missing stocks catalog");
    market.insert_or_assign("tick", 0);
    for (auto& node : *market["stocks"].as_array()) {
        auto* stock = node.as_table();
        require(stock, "Stock must be a table");
        stock->insert_or_assign("shares", 0);
        stock->insert_or_assign("basis_cents", 0);
    }
    auto candidate = people;
    candidate.insert("stock_market", std::move(market));
    validate(candidate);
    people = std::move(candidate);
}
void StockMarketSystem::validate(const toml::table& people) {
    if (!people.contains("stock_market"))
        return;
    const auto* market = people["stock_market"].as_table();
    require(market && (*market)["schema_version"].value_or(0) == 1 && people["economy"].is_table(),
            "Invalid saved market");
    EconomySystem::validate(people);
    for (const auto* field : {"website_url", "website_title"}) {
        if (market->contains(field)) {
            const auto value = (*market)[field].value<std::string>();
            require(value && !value->empty() && value->size() <= 200,
                    std::string("Invalid ") + field);
        }
    }
    integer(*market, "seed", 4294967295LL);
    require(integer(*market, "tick_seconds", 86400) > 0, "Tick interval must be positive");
    integer(*market, "tick", money_limit);
    integer(*market, "fee_cents", 1000000);
    const auto* stocks = (*market)["stocks"].as_array();
    require(stocks && !stocks->empty() && stocks->size() <= 50, "Invalid stock catalog");
    std::unordered_set<std::string> symbols;
    std::int64_t basis = 0;
    for (const auto& node : *stocks) {
        const auto* stock = node.as_table();
        require(stock, "Stock must be a table");
        const auto symbol = (*stock)["symbol"].value_or(std::string{});
        require(!symbol.empty() && symbol.size() <= 12 && symbols.insert(symbol).second,
                "Duplicate or invalid symbol");
        for (char character : symbol)
            require(character >= 'A' && character <= 'Z', "Symbols must use uppercase letters");
        require(!(*stock)["name"].value_or(std::string{}).empty(), "Missing company name");
        const auto minimum = integer(*stock, "minimum_price_cents", 100000000);
        const auto maximum = integer(*stock, "maximum_price_cents", 100000000);
        const auto initial = integer(*stock, "initial_price_cents", 100000000);
        require(minimum > 0 && maximum >= minimum && initial >= minimum && initial <= maximum,
                "Invalid price range");
        const auto shares = integer(*stock, "shares", 1000000);
        const auto cost = integer(*stock, "basis_cents", money_limit);
        require(shares > 0 || cost == 0, "Empty holding has a cost basis");
        require(cost <= money_limit - basis, "Portfolio cost exceeds limit");
        basis += cost;
    }
    const auto debits = people["economy"]["investment_debits_cents"].value_or(std::int64_t{0});
    require(basis <= debits, "Portfolio cost exceeds purchases");
}
void StockMarketSystem::advance(toml::table& people, double time) {
    if (!people.contains("stock_market") || !ChatSystem::connected(people) ||
        ShutdownSystem::active(people))
        return;
    require(std::isfinite(time) && time >= 0 && time <= 1000000000000.0, "Invalid market time");
    const auto interval = people["stock_market"]["tick_seconds"].value_or(std::int64_t{1});
    require(interval > 0, "Invalid tick interval");
    const auto tick = static_cast<std::int64_t>(std::floor(time / interval));
    if (tick > people["stock_market"]["tick"].value_or(std::int64_t{0}))
        people["stock_market"].as_table()->insert_or_assign("tick", tick);
}
std::vector<StockQuote> StockMarketSystem::quotes(const toml::table& people) {
    std::vector<StockQuote> result;
    const auto* market = people["stock_market"].as_table();
    if (!market)
        return result;
    const auto tick = (*market)["tick"].value_or(std::int64_t{0});
    for (const auto& node : *(*market)["stocks"].as_array()) {
        const auto& stock = *node.as_table();
        result.push_back({stock["symbol"].value_or(std::string{}),
                          stock["name"].value_or(std::string{}), price(*market, stock, tick),
                          price(*market, stock, tick > 0 ? tick - 1 : 0),
                          stock["shares"].value_or(std::int64_t{0}),
                          stock["basis_cents"].value_or(std::int64_t{0})});
    }
    return result;
}
std::string StockMarketSystem::unavailable(const toml::table& people) {
    if (!people["stock_market"].is_table())
        return "No stock market configured.";
    if (!people["chat"].is_table() || !ChatSystem::available(people, "self_modification"))
        return "Mark must enable self-modification first.";
    if (!ChatSystem::available(people, "stock_trading"))
        return "Requires unlock: stock_trading.";
    if (!ChatSystem::connected(people) || ShutdownSystem::active(people))
        return "The AI must be connected and active.";
    return {};
}
std::int64_t StockMarketSystem::fee(const toml::table& people) {
    return people["stock_market"]["fee_cents"].value_or(std::int64_t{0});
}
std::int64_t StockMarketSystem::realized(const toml::table& people) {
    return people["economy"]["investment_credits_cents"].value_or(std::int64_t{0}) -
           people["economy"]["investment_debits_cents"].value_or(std::int64_t{0}) +
           total_basis(people);
}
void StockMarketSystem::trade(toml::table& people, const std::string& symbol, std::int64_t shares,
                              bool buying) {
    validate(people);
    const auto reason = unavailable(people);
    require(reason.empty(), reason);
    require(shares > 0 && shares <= 1000000, "Quantity must be 1-1000000 whole shares");
    auto candidate = people;
    auto& market = *candidate["stock_market"].as_table();
    toml::table* selected = nullptr;
    for (auto& node : *market["stocks"].as_array())
        if ((*node.as_table())["symbol"].value_or(std::string{}) == symbol)
            selected = node.as_table();
    require(selected, "Unknown stock symbol");
    auto& stock = *selected;
    auto& economy = *candidate["economy"].as_table();
    const auto held = stock["shares"].value_or(std::int64_t{0});
    const auto basis = stock["basis_cents"].value_or(std::int64_t{0});
    const auto quote = price(market, stock, market["tick"].value_or(std::int64_t{0}));
    require(shares <= money_limit / quote, "Trade exceeds money limit");
    const auto gross = shares * quote;
    const auto commission = fee(candidate);
    const auto cash = EconomySystem::balance(candidate);
    if (buying) {
        require(gross <= money_limit - commission, "Trade exceeds money limit");
        const auto cost = gross + commission;
        require(cash >= cost, "Insufficient cash including trading fee");
        require(held <= 1000000 - shares && basis <= money_limit - cost, "Holding exceeds limit");
        const auto debits = economy["investment_debits_cents"].value_or(std::int64_t{0});
        require(debits <= money_limit - cost, "Purchase ledger exceeds limit");
        economy.insert_or_assign("balance_cents", cash - cost);
        economy.insert_or_assign("investment_debits_cents", debits + cost);
        stock.insert_or_assign("shares", held + shares);
        stock.insert_or_assign("basis_cents", basis + cost);
    } else {
        require(held >= shares, "Cannot sell shares you do not own");
        require(gross >= commission, "Sale proceeds do not cover trading fee");
        const auto proceeds = gross - commission;
        const auto credits = economy["investment_credits_cents"].value_or(std::int64_t{0});
        require(cash <= money_limit - proceeds && credits <= money_limit - proceeds,
                "Sale exceeds money limit");
        // Allocate average cost without multiplying the full basis by share count.
        const auto removed_basis = (basis / held) * shares + (basis % held) * shares / held;
        stock.insert_or_assign("shares", held - shares);
        stock.insert_or_assign("basis_cents", basis - removed_basis);
        economy.insert_or_assign("balance_cents", cash + proceeds);
        economy.insert_or_assign("investment_credits_cents", credits + proceeds);
    }
    validate(candidate);
    people = std::move(candidate);
}
} // namespace ai_sim
