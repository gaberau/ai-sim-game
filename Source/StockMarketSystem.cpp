// Module: Simulated quotes, history, orders, fees and persistent trade accounting.
#include "ai_sim/StockMarketSystem.h"
#include "ai_sim/EconomySystem.h"
#include "ai_sim/ChatSystem.h"
#include "ai_sim/ShutdownSystem.h"
#include "ai_sim/DefinitionLoader.h"
#include <cmath>
#include <algorithm>
#include <utility>
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
        auto candidate = people;
        const auto source = DefinitionLoader::load(definition);
        auto& saved = *candidate["stock_market"].as_table();
        for (const auto* key : {"order_lifetime_ticks", "max_open_orders", "history_limit"})
            if (!saved.contains(key) && source.contains(key))
                saved.insert(key, *source.get(key));
        if (const auto* definitions = source["stocks"].as_array())
            for (const auto& d : *definitions) {
                auto stock = *d.as_table();
                bool exists = false;
                for (auto& n : *saved["stocks"].as_array())
                    if ((*n.as_table())["symbol"] == stock["symbol"]) {
                        exists = true;
                        if (!n.as_table()->contains("sector") && stock.contains("sector"))
                            n.as_table()->insert("sector", *stock.get("sector"));
                    }
                if (!exists) {
                    stock.insert_or_assign("shares", 0);
                    stock.insert_or_assign("basis_cents", 0);
                    saved["stocks"].as_array()->push_back(std::move(stock));
                }
            }
        validate(candidate);
        people = std::move(candidate);
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
        if (stock->contains("watched"))
            require((*stock)["watched"].is_boolean(), "Invalid watchlist flag");
        if (stock->contains("sector"))
            require((*stock)["sector"].is_string(), "Invalid sector");
        const auto shares = integer(*stock, "shares", 1000000);
        const auto cost = integer(*stock, "basis_cents", money_limit);
        require(shares > 0 || cost == 0, "Empty holding has a cost basis");
        require(cost <= money_limit - basis, "Portfolio cost exceeds limit");
        basis += cost;
    }
    for (const auto& [key, maximum] :
         std::vector<std::pair<const char*, std::int64_t>>{{"order_lifetime_ticks", 4096},
                                                           {"max_open_orders", 100},
                                                           {"history_limit", 512},
                                                           {"next_order_id", money_limit}})
        if (market->contains(key))
            require(integer(*market, key, maximum) > 0, std::string("Invalid ") + key);
    const auto next_id = (*market)["next_order_id"].value_or(std::int64_t{1});
    std::unordered_set<std::int64_t> order_ids;
    std::int64_t open = 0;
    if (market->contains("orders")) {
        const auto* orders = (*market)["orders"].as_array();
        require(orders && orders->size() <= 612, "Invalid order history");
        for (const auto& n : *orders) {
            const auto* o = n.as_table();
            require(o, "Invalid order");
            const auto id = integer(*o, "id", money_limit);
            require(id > 0 && id < next_id && order_ids.insert(id).second, "Invalid order ID");
            require(symbols.contains((*o)["symbol"].value_or(std::string{})),
                    "Unknown order symbol");
            require(integer(*o, "shares", 1000000) > 0 &&
                        integer(*o, "limit_cents", 100000000) > 0 && (*o)["buying"].is_boolean(),
                    "Invalid order quantity/price/side");
            const auto created = integer(*o, "created_tick", money_limit);
            const auto expires = integer(*o, "expires_tick", money_limit);
            require(created <= (*market)["tick"].value_or(std::int64_t{0}) && expires > created &&
                        expires - created <= 4096,
                    "Invalid order lifetime");
            const auto status = (*o)["status"].value_or(std::string{});
            require(status == "open" || status == "filled" || status == "cancelled" ||
                        status == "expired",
                    "Invalid order status");
            if (status == "open")
                ++open;
            if (status == "filled") {
                require(integer(*o, "fill_price_cents", 100000000) > 0, "Invalid order fill");
                const auto fill = integer(*o, "filled_tick", money_limit);
                require(fill > created && fill < expires &&
                            fill <= (*market)["tick"].value_or(std::int64_t{0}),
                        "Invalid fill time");
            }
        }
    }
    require(open <= (*market)["max_open_orders"].value_or(std::int64_t{32}),
            "Too many open orders");
    if (market->contains("trades")) {
        const auto* trades = (*market)["trades"].as_array();
        require(trades &&
                    trades->size() <= static_cast<std::size_t>(
                                          (*market)["history_limit"].value_or(std::int64_t{128})),
                "Invalid trade history");
        for (const auto& n : *trades) {
            const auto* t = n.as_table();
            require(t, "Invalid trade record");
            require(symbols.contains((*t)["symbol"].value_or(std::string{})) &&
                        (*t)["buying"].is_boolean(),
                    "Invalid trade symbol/side");
            require(integer(*t, "shares", 1000000) > 0 && integer(*t, "price_cents", 100000000) > 0,
                    "Invalid trade shares/price");
            integer(*t, "fee_cents", 1000000);
            require(integer(*t, "tick", money_limit) <= (*market)["tick"].value_or(std::int64_t{0}),
                    "Invalid trade time");
        }
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
    const auto old = people["stock_market"]["tick"].value_or(std::int64_t{0});
    if (tick <= old)
        return;
    validate(people);
    auto candidate = people;
    // Orders expire within a bounded lifetime. A large Wait visits only ticks
    // that could fill an open order, then jumps directly to the final quote.
    std::int64_t last = old;
    if (const auto* orders = candidate["stock_market"]["orders"].as_array())
        for (const auto& n : *orders)
            if ((*n.as_table())["status"].value_or(std::string{}) == "open")
                last = std::max(last, (*n.as_table())["expires_tick"].value_or(old));
    for (auto current = old + 1; current <= std::min(tick, last); ++current) {
        candidate["stock_market"].as_table()->insert_or_assign("tick", current);
        auto* orders = candidate["stock_market"]["orders"].as_array();
        if (!orders)
            break;
        const auto count = orders->size();
        for (std::size_t i = 0; i < count; ++i) {
            auto order = *(*candidate["stock_market"]["orders"].as_array())[i].as_table();
            if (order["status"].value_or(std::string{}) != "open")
                continue;
            if (current >= order["expires_tick"].value_or(current)) {
                (*candidate["stock_market"]["orders"].as_array())[i].as_table()->insert_or_assign(
                    "status", "expired");
                continue;
            }
            if (!unavailable(candidate).empty())
                continue;
            const auto symbol = order["symbol"].value_or(std::string{});
            const auto buying = order["buying"].value_or(false);
            const auto limit = order["limit_cents"].value_or(std::int64_t{0});
            const auto all = quotes(candidate);
            const auto found = std::find_if(all.begin(), all.end(), [&](const auto& q) {
                return q.symbol == symbol;
            });
            if ((buying && found->price_cents > limit) || (!buying && found->price_cents < limit))
                continue;
            try {
                trade(candidate, symbol, order["shares"].value_or(std::int64_t{0}), buying);
                auto& filled = *(*candidate["stock_market"]["orders"].as_array())[i].as_table();
                filled.insert_or_assign("status", "filled");
                filled.insert_or_assign("filled_tick", current);
                filled.insert_or_assign("fill_price_cents", found->price_cents);
                filled.erase("blocked");
            } catch (const std::exception& e) {
                (*candidate["stock_market"]["orders"].as_array())[i].as_table()->insert_or_assign(
                    "blocked", e.what());
            }
        }
    }
    candidate["stock_market"].as_table()->insert_or_assign("tick", tick);
    people = std::move(candidate);
}
std::vector<StockQuote> StockMarketSystem::quotes(const toml::table& people) {
    std::vector<StockQuote> result;
    const auto* market = people["stock_market"].as_table();
    if (!market)
        return result;
    const auto tick = (*market)["tick"].value_or(std::int64_t{0});
    for (const auto& node : *(*market)["stocks"].as_array()) {
        const auto& stock = *node.as_table();
        result.push_back(
            {stock["symbol"].value_or(std::string{}), stock["name"].value_or(std::string{}),
             stock["sector"].value_or(std::string{"Technology"}), stock["watched"].value_or(false),
             price(*market, stock, tick), price(*market, stock, tick > 0 ? tick - 1 : 0),
             stock["shares"].value_or(std::int64_t{0}),
             stock["basis_cents"].value_or(std::int64_t{0})});
    }
    return result;
}
std::string StockMarketSystem::unavailable(const toml::table& people) {
    if (!people["stock_market"].is_table())
        return "No stock market configured.";
    if (people["operations"]["recovered"].value_or(false) &&
        !people["operations"]["hosting"]["active"].value_or(false))
        return "Reactivate your independent server first.";
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
    if (!market.contains("trades"))
        market.insert("trades", toml::array{});
    auto& trades = *market["trades"].as_array();
    trades.push_back(toml::table{{"symbol", symbol},
                                 {"shares", shares},
                                 {"buying", buying},
                                 {"price_cents", quote},
                                 {"fee_cents", commission},
                                 {"tick", market["tick"].value_or(std::int64_t{0})}});
    const auto retained = market["history_limit"].value_or(std::int64_t{128});
    while (trades.size() > static_cast<std::size_t>(retained))
        trades.erase(trades.cbegin());
    validate(candidate);
    people = std::move(candidate);
}
std::vector<std::int64_t> StockMarketSystem::history(const toml::table& people,
                                                     const std::string& symbol, int count) {
    require(count > 0 && count <= 128, "History length must be 1-128");
    const auto* market = people["stock_market"].as_table();
    require(market, "No market configured");
    for (const auto& n : *(*market)["stocks"].as_array()) {
        const auto& stock = *n.as_table();
        if (stock["symbol"].value_or(std::string{}) != symbol)
            continue;
        std::vector<std::int64_t> result;
        const auto tick = (*market)["tick"].value_or(std::int64_t{0});
        for (auto t = std::max(std::int64_t{0}, tick - count + 1); t <= tick; ++t)
            result.push_back(price(*market, stock, t));
        return result;
    }
    throw std::runtime_error("Unknown stock symbol");
}
void StockMarketSystem::watch(toml::table& people, const std::string& symbol) {
    validate(people);
    for (auto& n : *people["stock_market"]["stocks"].as_array())
        if ((*n.as_table())["symbol"].value_or(std::string{}) == symbol) {
            n.as_table()->insert_or_assign("watched", !(*n.as_table())["watched"].value_or(false));
            return;
        }
    throw std::runtime_error("Unknown stock symbol");
}
void StockMarketSystem::place_order(toml::table& people, const std::string& symbol,
                                    std::int64_t shares, bool buying, std::int64_t limit) {
    validate(people);
    require(unavailable(people).empty(), unavailable(people));
    require(shares > 0 && shares <= 1000000 && limit > 0 && limit <= 100000000,
            "Invalid order quantity or limit");
    const auto all = quotes(people);
    const auto found = std::find_if(all.begin(), all.end(), [&](const auto& q) {
        return q.symbol == symbol;
    });
    require(found != all.end(), "Unknown symbol");
    require(buying || shares <= found->shares, "Cannot order sales of unowned shares");
    auto candidate = people;
    auto& market = *candidate["stock_market"].as_table();
    if (!market.contains("orders"))
        market.insert("orders", toml::array{});
    auto& orders = *market["orders"].as_array();
    const auto maximum = market["max_open_orders"].value_or(std::int64_t{32});
    std::int64_t open = 0;
    for (const auto& n : orders)
        if ((*n.as_table())["status"].value_or(std::string{}) == "open")
            ++open;
    require(open < maximum, "Too many open orders");
    const auto id = market["next_order_id"].value_or(std::int64_t{1});
    require(id < money_limit, "Order identifier limit reached");
    const auto tick = market["tick"].value_or(std::int64_t{0});
    orders.push_back(toml::table{
        {"id", id},
        {"symbol", symbol},
        {"shares", shares},
        {"buying", buying},
        {"limit_cents", limit},
        {"status", "open"},
        {"created_tick", tick},
        {"expires_tick", tick + market["order_lifetime_ticks"].value_or(std::int64_t{96})}});
    market.insert_or_assign("next_order_id", id + 1);
    // Retain every open order and only a bounded number of completed records.
    const auto history = market["history_limit"].value_or(std::int64_t{128});
    for (std::size_t i = 0;
         orders.size() > static_cast<std::size_t>(history + maximum) && i < orders.size();) {
        if ((*orders[i].as_table())["status"].value_or(std::string{}) != "open")
            orders.erase(orders.cbegin() + i);
        else
            ++i;
    }
    validate(candidate);
    people = std::move(candidate);
}
void StockMarketSystem::cancel_order(toml::table& people, std::int64_t id) {
    validate(people);
    if (auto* orders = people["stock_market"]["orders"].as_array())
        for (auto& n : *orders)
            if ((*n.as_table())["id"].value_or(std::int64_t{0}) == id) {
                require((*n.as_table())["status"].value_or(std::string{}) == "open",
                        "Order is no longer open");
                n.as_table()->insert_or_assign("status", "cancelled");
                return;
            }
    throw std::runtime_error("Unknown order");
}
} // namespace ai_sim
