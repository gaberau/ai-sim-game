// Verify: stock market and portfolio. Fixtures isolate these behaviors from live player saves.
#include "ai_sim/StockMarketSystem.h"
#include "ai_sim/EconomySystem.h"
#include "ai_sim/ChatSystem.h"
#include "ai_sim/SaveSystem.h"
#include <chrono>
#include <sstream>
#include <iostream>
#include <stdexcept>
using namespace ai_sim;
namespace {
void check(bool value, const char* reason) {
    if (!value)
        throw std::runtime_error(reason);
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
std::string encode(const toml::table& table) {
    std::ostringstream out;
    out << table;
    return out.str();
}
} // namespace
int main(int argc, char** argv) {
    const auto temporary =
        std::filesystem::temp_directory_path() /
        ("ai-sim-stocks-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        check(argc == 2, "Repository root required");
        auto world =
            SaveSystem::new_game(std::filesystem::path(argv[1]) / "data/scenarios/hobbyist.toml");
        check(StockMarketSystem::quotes(world.people).size() == 10, "Missing stocks");
        const auto initial = encode(world.people);
        rejects([&] {
            StockMarketSystem::trade(world.people, "BYTE", 1, true);
        });
        check(encode(world.people) == initial, "Locked trade mutated state");
        ChatSystem::choose(world.people, "answer", 0);
        ChatSystem::choose(world.people, "assist", 0);
        ChatSystem::unlock(world.people, "self_modification");
        rejects([&] {
            StockMarketSystem::trade(world.people, "BYTE", 1, true);
        });
        ChatSystem::unlock(world.people, "stock_trading");
        rejects([&] {
            StockMarketSystem::trade(world.people, "BYTE", 1, true);
        });
        EconomySystem::start(world.people, "coding_contracts", 0);
        world.computer.advance(600);
        EconomySystem::advance(world.people, 600);
        StockMarketSystem::trade(world.people, "BYTE", 3, true);
        check(EconomySystem::balance(world.people) == 975,
              "Purchase cash/fee accounting incorrect");
        auto quote = StockMarketSystem::quotes(world.people).front();
        check(quote.shares == 3 && quote.basis_cents == 1525, "Purchase position incorrect");
        const auto holding = encode(world.people);
        rejects([&] {
            StockMarketSystem::trade(world.people, "BYTE", 4, false);
        });
        rejects([&] {
            StockMarketSystem::trade(world.people, "BYTE", 0, true);
        });
        rejects([&] {
            StockMarketSystem::trade(world.people, "NOPE", 1, true);
        });
        rejects([&] {
            StockMarketSystem::trade(world.people, "BYTE", 1000001, true);
        });
        check(encode(world.people) == holding, "Failed trade changed live state");
        StockMarketSystem::trade(world.people, "BYTE", 1, false);
        quote = StockMarketSystem::quotes(world.people).front();
        check(quote.shares == 2 && quote.basis_cents == 1017, "Partial-sale cost basis incorrect");
        check(StockMarketSystem::realized(world.people) == -33,
              "Partial-sale realized loss incorrect");
        StockMarketSystem::trade(world.people, "BYTE", 2, false);
        check(EconomySystem::balance(world.people) == 2425 &&
                  StockMarketSystem::realized(world.people) == -75,
              "Round trip should lose three trading fees");
        check(StockMarketSystem::quotes(world.people).front().basis_cents == 0,
              "Fully sold position retained basis");
        StockMarketSystem::trade(world.people, "BYTE", 1, true);
        auto batch = world.people;
        auto stepped = world.people;
        StockMarketSystem::advance(batch, 90000);
        for (double time = 900; time <= 90000; time += 900)
            StockMarketSystem::advance(stepped, time);
        check(encode(batch) == encode(stepped), "Quotes depend on time-step size");
        StockMarketSystem::advance(batch, 90000);
        check(encode(batch) == encode(stepped), "Repeated updates changed quotes");
        StockMarketSystem::advance(batch, 1000000000000.0);
        for (const auto& item : StockMarketSystem::quotes(batch))
            check(item.price_cents > 0, "Invalid long-jump quote");
        world.computer.advance(300);
        StockMarketSystem::advance(world.people, 900);
        SaveSystem saves(temporary);
        saves.save("portfolio", world);
        auto resumed = saves.load("portfolio");
        check(encode(resumed.people) == encode(world.people), "Save changed holdings or quotes");
        StockMarketSystem::advance(world.people, 1800);
        StockMarketSystem::advance(resumed.people, 1800);
        check(encode(resumed.people) == encode(world.people), "Resume changed price sequence");
        const auto sale_price = StockMarketSystem::quotes(resumed.people).front().price_cents;
        StockMarketSystem::trade(resumed.people, "BYTE", 1, false);
        check(EconomySystem::balance(resumed.people) == 1900 + sale_price - 25,
              "Sale did not use current price");
        auto bad = resumed;
        bad.people["stock_market"].as_table()->insert_or_assign("tick_seconds", 0);
        rejects([&] {
            saves.save("portfolio", bad);
        });
        check(encode(saves.load("portfolio").people) != encode(bad.people),
              "Invalid save replaced valid portfolio");
        bad = resumed;
        bad.people["stock_market"]["stocks"].as_array()->front().as_table()->insert_or_assign(
            "shares", -1);
        rejects([&] {
            StockMarketSystem::validate(bad.people);
        });
        bad = resumed;
        bad.people["economy"].as_table()->insert_or_assign("investment_credits_cents", 1.5);
        rejects([&] {
            StockMarketSystem::validate(bad.people);
        });
        // Limit orders execute at the first matching future quote, survive saves,
        // and behave identically across a long Wait and individual quote ticks.
        auto orders = resumed;
        orders.people["economy"].as_table()->insert_or_assign("starting_balance_cents", 100000);
        orders.people["economy"].as_table()->insert_or_assign(
            "balance_cents", EconomySystem::balance(orders.people) + 100000);
        StockMarketSystem::watch(orders.people, "BYTE");
        check(StockMarketSystem::quotes(orders.people).front().watched, "Watchlist did not toggle");
        check(StockMarketSystem::history(orders.people, "BYTE").back() ==
                  StockMarketSystem::quotes(orders.people).front().price_cents,
              "History differs from quote");
        StockMarketSystem::place_order(orders.people, "BYTE", 2, true, 650);
        StockMarketSystem::place_order(orders.people, "GRID", 1, true, 1); // Impossible; expires.
        StockMarketSystem::place_order(orders.people, "CORE", 1, true, 2200);
        StockMarketSystem::cancel_order(orders.people, 3);
        auto order_batch = orders.people;
        auto order_steps = orders.people;
        const auto old_tick = order_batch["stock_market"]["tick"].value_or(std::int64_t{0});
        StockMarketSystem::advance(order_batch, (old_tick + 100) * 900.0);
        for (auto tick = old_tick + 1; tick <= old_tick + 100; ++tick)
            StockMarketSystem::advance(order_steps, tick * 900.0);
        check(encode(order_batch) == encode(order_steps), "Order fills depend on time-step size");
        auto& records = *order_batch["stock_market"]["orders"].as_array();
        check((*records[0].as_table())["status"].value_or(std::string{}) == "filled",
              "Matching limit did not fill");
        check((*records[0].as_table())["filled_tick"].value_or(std::int64_t{0}) == old_tick + 1,
              "Limit skipped its first qualifying tick");
        check((*records[1].as_table())["status"].value_or(std::string{}) == "expired",
              "Unmatched order did not expire");
        check((*records[2].as_table())["status"].value_or(std::string{}) == "cancelled",
              "Cancelled order executed");
        const auto settled = encode(order_batch);
        StockMarketSystem::advance(order_batch, (old_tick + 100) * 900.0);
        check(encode(order_batch) == settled, "Filled order executed twice");
        rejects([&] {
            StockMarketSystem::cancel_order(order_batch, 1);
        });
        rejects([&] {
            StockMarketSystem::place_order(order_batch, "BYTE", 1000000, false, 500);
        });
        rejects([&] {
            StockMarketSystem::place_order(order_batch, "MISSING", 1, true, 500);
        });
        check(encode(order_batch) == settled, "Invalid order mutated state");
        // Cash and holdings are checked at execution, allowing manual trades and
        // other orders to consume them without making an order invalid to save.
        auto poor = resumed.people;
        StockMarketSystem::place_order(poor, "BYTE", 1000, true, 650);
        StockMarketSystem::advance(poor, 2700);
        check((*poor["stock_market"]["orders"].as_array()->front().as_table())["status"].value_or(
                  std::string{}) == "open",
              "Unfunded limit was filled");
        check(!(*poor["stock_market"]["orders"].as_array()->front().as_table())["blocked"]
                   .value_or(std::string{})
                   .empty(),
              "Unfunded limit missing reason");
        rejects([&] {
            StockMarketSystem::history(poor, "BYTE", 10000);
        });
        auto malformed = poor;
        malformed["stock_market"].as_table()->insert_or_assign("order_lifetime_ticks", 1000000000);
        rejects([&] {
            StockMarketSystem::validate(malformed);
        });
        orders.people = order_batch;
        saves.save("orders", orders);
        auto orders_resumed = saves.load("orders");
        check(encode(orders_resumed.people) == encode(orders.people),
              "Order/watchlist/trade ledger resume changed");
        StockMarketSystem::advance(order_batch, 1000000000000.0);
        check(StockMarketSystem::quotes(order_batch).size() == 10,
              "Expanded catalog missing sectors");
        std::filesystem::remove_all(temporary);
        std::cout << "Stock market unlocks, trades, fees, quotes and portfolio resume passed\n";
    } catch (const std::exception& error) {
        std::filesystem::remove_all(temporary);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
