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
        check(StockMarketSystem::quotes(world.people).size() == 4, "Missing stocks");
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
        std::filesystem::remove_all(temporary);
        std::cout << "Stock market unlocks, trades, fees, quotes and portfolio resume passed\n";
    } catch (const std::exception& error) {
        std::filesystem::remove_all(temporary);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
