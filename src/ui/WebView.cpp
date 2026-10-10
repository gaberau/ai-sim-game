#include "ai_sim/ui/TerminalUI.h"
#include "ai_sim/StockMarketSystem.h"
#include "ai_sim/EconomySystem.h"
#include <algorithm>
namespace ai_sim::ui {
namespace {
std::string gain(std::int64_t cents) {
    return (cents < 0 ? "-" : "+") + EconomySystem::money(cents < 0 ? -cents : cents);
}
} // namespace
std::string TerminalUI::market_url() const {
    return people_["stock_market"]["website_url"].value_or(std::string{"https://exchange.sim"});
}
void TerminalUI::open_stock_market() {
    browser_address_ = market_url();
    web_selected_ = web_offset_ = 0;
    selected_tab_ = Tab::Web;
    menu_active_ = text_active_ = false;
}
void TerminalUI::open_web_address() {
    open_text(
        "Website address", "Enter an in-game website URL, or about:home for bookmarks.",
        [this](const std::string& address) {
            browser_address_ = address;
            web_selected_ = web_offset_ = 0;
        },
        browser_address_);
}
void TerminalUI::draw_web(int top, int bottom, int width) {
    write(top, 1, "WEB BROWSER | " + browser_address_, Heading, true);
    const auto site_title =
        people_["stock_market"]["website_title"].value_or(std::string{"Sim Exchange"});
    struct Row {
        std::string text;
        int color = Plain;
    };
    std::vector<Row> rows;
    if (browser_address_ == market_url() && people_["stock_market"].is_table()) {
        const auto title =
            people_["stock_market"]["website_title"].value_or(std::string{"Sim Exchange"});
        write(top + 1, 2,
              title + " | Cash " + EconomySystem::money(EconomySystem::balance(people_)), Heading);
        const auto quotes = StockMarketSystem::quotes(people_);
        for (const auto& quote : quotes) {
            const auto delta = quote.price_cents - quote.previous_cents;
            rows.push_back({(quote.watched ? "* " : "") + quote.symbol + " | " +
                                EconomySystem::money(quote.price_cents) + " | " + gain(delta) +
                                " | owned " + std::to_string(quote.shares) + " | " + quote.name +
                                " | " + quote.sector,
                            delta < 0   ? Protected
                            : delta > 0 ? Stoppable
                                        : Plain});
        }
        rows.push_back({"Portfolio / account summary"});
        rows.push_back({"Orders / cancel pending trades"});
        rows.push_back({"Trade history"});
        rows.push_back({"Watchlist"});
        rows.push_back({"Go to another address"});
        rows.push_back({"Browser home"});
    } else if (browser_address_ == infrastructure_url() && people_["infrastructure"].is_table()) {
        write(top + 1, 2,
              people_["infrastructure"]["website_title"].value_or(
                  std::string{"City Infrastructure Directory"}),
              Heading);
        for (const auto& node : *people_["infrastructure"]["sectors"].as_array()) {
            const auto& sector = *node.as_table();
            rows.push_back({sector["name"].value_or(std::string{}) + " | " +
                            sector["description"].value_or(std::string{})});
        }
        rows.push_back({"Go to another address"});
        rows.push_back({"Browser home"});
    } else {
        write(top + 1, 2,
              browser_address_ == "about:home"
                  ? "BOOKMARKS"
                  : "Website not found. Choose a bookmark or another address.",
              Heading);
        rows.push_back({site_title + " | " + market_url()});
        rows.push_back({"City Infrastructure Directory | " + infrastructure_url()});
        rows.push_back({"Go to address"});
        rows.push_back({"Browser home"});
    }
    web_selected_ = std::clamp(web_selected_, 0, static_cast<int>(rows.size()) - 1);
    const auto visible = std::max(1, bottom - top - 3);
    if (web_selected_ < web_offset_)
        web_offset_ = web_selected_;
    if (web_selected_ >= web_offset_ + visible)
        web_offset_ = web_selected_ - visible + 1;
    for (int row = 0; row < visible && web_offset_ + row < static_cast<int>(rows.size()); ++row) {
        const auto index = web_offset_ + row;
        const auto& entry = rows[index];
        write(top + 3 + row, 2,
              (index == web_selected_ ? "> " : "  ") + entry.text.substr(0, std::max(0, width - 4)),
              index == web_selected_ ? ActiveTab : entry.color, index == web_selected_);
    }
    write(bottom, 1,
          settings_.binding_label(KeyAction::Submit) + ": follow link | " +
              settings_.binding_label(KeyAction::SelectPrevious) + "/" +
              settings_.binding_label(KeyAction::SelectNext) + ": links",
          Footer);
}
void TerminalUI::follow_web_link() {
    if (browser_address_ == market_url() && people_["stock_market"].is_table()) {
        const auto quotes = StockMarketSystem::quotes(people_);
        if (web_selected_ < static_cast<int>(quotes.size())) {
            open_stock(quotes[web_selected_].symbol);
        } else if (web_selected_ == static_cast<int>(quotes.size())) {
            std::int64_t value = 0, basis = 0;
            for (const auto& quote : quotes) {
                value += quote.shares * quote.price_cents;
                basis += quote.basis_cents;
            }
            show_message(
                "Sim Exchange | Portfolio",
                "Cash: " + EconomySystem::money(EconomySystem::balance(people_)) + ". Holdings: " +
                    EconomySystem::money(value) + ". Cost basis: " + EconomySystem::money(basis) +
                    ". Unrealized: " + gain(value - basis) +
                    ". Realized after fees: " + gain(StockMarketSystem::realized(people_)) +
                    ". Fee per trade: " + EconomySystem::money(StockMarketSystem::fee(people_)) +
                    ". Prices update in working time. Buying needs cash; selling needs owned "
                    "shares.");
            menu_back_ = [this] {
                open_stock_market();
            };
        } else if (web_selected_ == static_cast<int>(quotes.size()) + 1) {
            open_stock_orders();
        } else if (web_selected_ == static_cast<int>(quotes.size()) + 2) {
            open_stock_history();
        } else if (web_selected_ == static_cast<int>(quotes.size()) + 3) {
            std::vector<MenuAction> actions;
            for (const auto& quote : quotes)
                if (quote.watched)
                    actions.push_back(
                        {quote.symbol + " | " + EconomySystem::money(quote.price_cents),
                         [this, symbol = quote.symbol] {
                             open_stock(symbol);
                         },
                         {},
                         quote.sector});
            if (actions.empty())
                actions.push_back({"Your watchlist is empty",
                                   {},
                                   {},
                                   "Select a stock and add it to your watchlist."});
            open_menu("Sim Exchange | Watchlist", std::move(actions), [this] {
                open_stock_market();
            });
        } else if (web_selected_ == static_cast<int>(quotes.size()) + 4) {
            open_web_address();
        } else {
            browser_address_ = "about:home";
            web_selected_ = web_offset_ = 0;
        }
    } else if (browser_address_ == infrastructure_url() && people_["infrastructure"].is_table()) {
        const auto& sectors = *people_["infrastructure"]["sectors"].as_array();
        if (web_selected_ < static_cast<int>(sectors.size()))
            open_infrastructure_sector(
                (*sectors[web_selected_].as_table())["id"].value_or(std::string{}));
        else if (web_selected_ == static_cast<int>(sectors.size()))
            open_web_address();
        else {
            browser_address_ = "about:home";
            web_selected_ = web_offset_ = 0;
        }
    } else if (web_selected_ == 0) {
        open_stock_market();
    } else if (web_selected_ == 1) {
        open_infrastructure();
    } else if (web_selected_ == 2) {
        open_web_address();
    } else {
        browser_address_ = "about:home";
        web_selected_ = web_offset_ = 0;
    }
}
} // namespace ai_sim::ui
