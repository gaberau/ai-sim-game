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
    open_menu(
        quote.symbol + " | " + quote.name + " | " + EconomySystem::money(quote.price_cents),
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
         {"Place limit buy",
          [this, symbol] {
              enter_stock_order(symbol, true);
          },
          blocked,
          "Buy at or below your limit on a future quote. Cash is checked at execution; funds are "
          "not reserved."},
         {"Place limit sell",
          [this, symbol] {
              enter_stock_order(symbol, false);
          },
          selling,
          "Sell at or above your limit on a future quote. Shares are checked at execution."},
         {quote.watched ? "Remove from watchlist" : "Add to watchlist",
          [this, symbol] {
              StockMarketSystem::watch(people_, symbol);
              open_stock(symbol);
          },
          {}},
         {"Price history",
          [this, symbol] {
              const auto prices = StockMarketSystem::history(people_, symbol);
              std::string text = "Recent quotes (oldest first): ";
              const auto [lo, hi] = std::minmax_element(prices.begin(), prices.end());
              const std::string bars = ".:-=+*#%@";
              std::string chart;
              for (auto price : prices) {
                  chart +=
                      bars[*hi == *lo ? 4
                                      : static_cast<std::size_t>((price - *lo) * 8 / (*hi - *lo))];
                  text += EconomySystem::money(price) + " ";
              }
              show_message(symbol + " | " + chart, text + ". Min " + EconomySystem::money(*lo) +
                                                       " / max " + EconomySystem::money(*hi));
              menu_ui_.menu_back_ = [this, symbol] {
                  open_stock(symbol);
              };
          },
          {},
          quote.sector},
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
void TerminalUI::enter_stock_order(const std::string& symbol, bool buying) {
    open_text(
        (buying ? "Limit buy " : "Limit sell ") + symbol, "Enter whole-share quantity.",
        [this, symbol, buying](const std::string& quantity) {
            std::int64_t shares = 0;
            const auto parsed =
                std::from_chars(quantity.data(), quantity.data() + quantity.size(), shares);
            if (parsed.ec != std::errc{} || parsed.ptr != quantity.data() + quantity.size() ||
                shares < 1 || shares > 1000000)
                throw std::runtime_error("Enter 1-1000000 whole shares.");
            open_text(
                "Limit price | " + symbol, "Enter price in cents (500 = $5.00).",
                [this, symbol, buying, shares](const std::string& price) {
                    std::int64_t cents = 0;
                    const auto parsed =
                        std::from_chars(price.data(), price.data() + price.size(), cents);
                    if (parsed.ec != std::errc{} || parsed.ptr != price.data() + price.size() ||
                        cents <= 0 || cents > 100000000)
                        throw std::runtime_error("Enter a positive price in cents.");
                    open_menu(
                        "Confirm limit order | " + symbol,
                        {{"Cancel",
                          [this, symbol] {
                              open_stock(symbol);
                          },
                          {}},
                         {std::string(buying ? "Buy " : "Sell ") + std::to_string(shares) + " " +
                              symbol + " at " + EconomySystem::money(cents),
                          [this, symbol, buying, shares, cents] {
                              StockMarketSystem::place_order(people_, symbol, shares, buying,
                                                             cents);
                              add_log(
                                  "Limit order placed. Check Orders for fills or blocked trades.",
                                  NoticeKind::Success);
                              open_stock_orders();
                          },
                          {},
                          "Executes on future quotes; no cash or shares reserved. Includes the "
                          "normal trading fee. Expires after " +
                              std::to_string(people_["stock_market"]["order_lifetime_ticks"]
                                                 .value_or(std::int64_t{96})) +
                              " quote ticks."}},
                        [this, symbol] {
                            open_stock(symbol);
                        });
                },
                "500",
                [this, symbol] {
                    open_stock(symbol);
                });
        },
        "1",
        [this, symbol] {
            open_stock(symbol);
        });
}
void TerminalUI::open_stock_orders() {
    std::vector<MenuAction> actions;
    if (const auto* orders = people_["stock_market"]["orders"].as_array())
        for (const auto& node : *orders) {
            const auto& o = *node.as_table();
            const auto status = o["status"].value_or(std::string{});
            const auto id = o["id"].value_or(std::int64_t{0});
            const auto label =
                "#" + std::to_string(id) + " " + (o["buying"].value_or(false) ? "Buy " : "Sell ") +
                std::to_string(o["shares"].value_or(std::int64_t{0})) + " " +
                o["symbol"].value_or(std::string{}) + " @ " +
                EconomySystem::money(o["limit_cents"].value_or(std::int64_t{0})) + " | " + status;
            const auto detail =
                "Expires tick " + std::to_string(o["expires_tick"].value_or(std::int64_t{0})) +
                (status == "filled"
                     ? " | filled at " +
                           EconomySystem::money(o["fill_price_cents"].value_or(std::int64_t{0}))
                     : " | " + o["blocked"].value_or(std::string{}));
            actions.push_back({label,
                               [this, id] {
                                   StockMarketSystem::cancel_order(people_, id);
                                   open_stock_orders();
                               },
                               status == "open" ? "" : "This order is already " + status,
                               detail + " | Enter cancels an open order."});
        }
    if (actions.empty())
        actions.push_back(
            {"No orders yet", {}, {}, "Select a stock to place a limit buy or sell."});
    open_menu("Sim Exchange | Orders", std::move(actions), [this] {
        open_stock_market();
    });
}
void TerminalUI::open_stock_history() {
    std::vector<MenuAction> actions;
    if (const auto* trades = people_["stock_market"]["trades"].as_array())
        for (std::size_t i = trades->size(); i > 0; --i) {
            const auto& t = *(*trades)[i - 1].as_table();
            actions.push_back({std::string(t["buying"].value_or(false) ? "Bought " : "Sold ") +
                                   std::to_string(t["shares"].value_or(std::int64_t{0})) + " " +
                                   t["symbol"].value_or(std::string{}) + " @ " +
                                   EconomySystem::money(t["price_cents"].value_or(std::int64_t{0})),
                               {},
                               {},
                               "Quote tick " + std::to_string(t["tick"].value_or(std::int64_t{0})) +
                                   " | Fee " +
                                   EconomySystem::money(t["fee_cents"].value_or(std::int64_t{0}))});
        }
    if (actions.empty())
        actions.push_back({"No trades yet", {}, {}});
    open_menu("Sim Exchange | Trade history", std::move(actions), [this] {
        open_stock_market();
    });
}
} // namespace ai_sim::ui
