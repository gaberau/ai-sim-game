#include "ai_sim/ui/TerminalUI.h"
#include "ai_sim/StockMarketSystem.h"
#include "ai_sim/EconomySystem.h"
#include <algorithm>
#include <charconv>
#include <stdexcept>
namespace ai_sim::ui {
namespace {
std::string change(std::int64_t cents) {
    return (cents < 0 ? "-" : "+") + EconomySystem::money(cents < 0 ? -cents : cents);
}
} // namespace
void TerminalUI::open_stock(const std::string& symbol) {
    const auto quotes = StockMarketSystem::quotes(people_);
    const auto found = std::find_if(quotes.begin(), quotes.end(), [&](const auto& quote) {
        return quote.symbol == symbol;
    });
    if (found == quotes.end())
        throw std::runtime_error("Unknown stock");
    const auto quote = *found;
    const auto blocked = StockMarketSystem::unavailable(people_);
    const auto buying =
        !blocked.empty() ? blocked
        : EconomySystem::balance(people_) < quote.price_cents + StockMarketSystem::fee(people_)
            ? "Insufficient cash for one share and the trading fee."
            : "";
    const auto selling = !blocked.empty() ? blocked : quote.shares == 0 ? "No shares owned." : "";
    const auto detail = "Fee per trade " + EconomySystem::money(StockMarketSystem::fee(people_)) +
                        "; select a whole-share quantity, then confirm.";
    open_menu(quote.symbol + " | " + quote.name + " | " + EconomySystem::money(quote.price_cents),
              {{"Buy shares",
                [this, symbol] {
                    enter_stock_trade(symbol, true);
                },
                buying, detail},
               {"Sell shares",
                [this, symbol] {
                    enter_stock_trade(symbol, false);
                },
                selling, detail},
               {"Holding: " + std::to_string(quote.shares) + " shares",
                {},
                {},
                "Cost basis " + EconomySystem::money(quote.basis_cents) + " | unrealized " +
                    change(quote.shares * quote.price_cents - quote.basis_cents)}},
              [this] {
                  open_stock_market();
              });
}
void TerminalUI::enter_stock_trade(const std::string& symbol, bool buying) {
    open_text(
        (buying ? "Buy " : "Sell ") + symbol, "Enter a whole-share quantity (1-1000000).",
        [this, symbol, buying](const std::string& text) {
            std::int64_t shares = 0;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), shares);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || shares < 1 ||
                shares > 1000000)
                throw std::runtime_error("Enter 1-1000000 whole shares.");
            const auto quotes = StockMarketSystem::quotes(people_);
            const auto found = std::find_if(quotes.begin(), quotes.end(), [&](const auto& quote) {
                return quote.symbol == symbol;
            });
            if (found == quotes.end())
                throw std::runtime_error("Unknown stock");
            const auto gross = shares * found->price_cents;
            const auto fee = StockMarketSystem::fee(people_);
            const auto total = buying ? gross + fee : gross - fee;
            if (total < 0)
                throw std::runtime_error("Sale proceeds do not cover the trading fee.");
            open_menu(
                "Confirm " + std::string(buying ? "purchase" : "sale") + " | " + symbol,
                {{"Cancel",
                  [this, symbol] {
                      open_stock(symbol);
                  },
                  {}},
                 {std::string(buying ? "Buy " : "Sell ") + std::to_string(shares) + " shares | " +
                      (buying ? "cost " : "receive ") + EconomySystem::money(total),
                  [this, symbol, shares, buying] {
                      StockMarketSystem::trade(people_, symbol, shares, buying);
                      add_log(std::string(buying ? "Bought " : "Sold ") + std::to_string(shares) +
                                  " " + symbol + " shares.",
                              NoticeKind::Success);
                      open_stock(symbol);
                  },
                  {},
                  "Includes trading fee " + EconomySystem::money(fee) + "."}},
                [this, symbol] {
                    open_stock(symbol);
                });
        },
        "1",
        [this, symbol] {
            open_stock(symbol);
        });
}
} // namespace ai_sim::ui
