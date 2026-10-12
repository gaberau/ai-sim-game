#include "ai_sim/ui/TerminalUI.h"
#include "ai_sim/ui/ScreenRenderer.h"

#include <ncurses.h>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace ai_sim::ui {
namespace {
std::string clipped(const std::string& text, int width) {
    if (width <= 0)
        return {};
    if (text.size() <= static_cast<std::size_t>(width))
        return text;
    return width > 3 ? text.substr(0, width - 3) + "..." : text.substr(0, width);
}

std::vector<std::string> wrapped(const std::string& text, int width) {
    std::vector<std::string> lines;
    std::istringstream paragraphs(text);
    std::string paragraph;
    while (std::getline(paragraphs, paragraph)) {
        std::istringstream words(paragraph);
        std::string word, line;
        while (words >> word) {
            if (!line.empty() && line.size() + 1 + word.size() > static_cast<std::size_t>(width)) {
                lines.push_back(line);
                line.clear();
            }
            while (word.size() > static_cast<std::size_t>(width)) {
                lines.push_back(word.substr(0, width));
                word.erase(0, width);
            }
            if (!line.empty())
                line += ' ';
            line += word;
        }
        lines.push_back(line);
    }
    return lines;
}
} // namespace

void TerminalUI::load_news() {
    const auto path = definitions_.news;
    std::vector<NewsArticle> articles;
    std::string edition = "Simulation news";
    bool ticker = true;
    double interval = 120.0;
    if (!path.empty() && std::filesystem::exists(path)) {
        const auto document = DefinitionLoader::load(path);
        if (document["schema_version"].value<int>() != 1)
            throw std::runtime_error("news.toml: unsupported schema_version");
        edition = document["feed"]["edition"].value_or(edition);
        ticker = document["feed"]["ticker_enabled"].value_or(true);
        interval = document["feed"]["ticker_seconds"].value_or(120.0);
        if (!std::isfinite(interval) || interval < 1.0)
            throw std::runtime_error("news.toml: ticker_seconds must be at least 1");
        std::unordered_set<std::string> ids;
        if (const auto* rows = document["articles"].as_array()) {
            for (const auto& node : *rows) {
                const auto* table = node.as_table();
                if (!table)
                    throw std::runtime_error("news.toml: articles must be tables");
                const auto& row = *table;
                NewsArticle article;
                article.id = row["id"].value_or(std::string{});
                article.category = row["category"].value_or(std::string("world"));
                article.headline = row["headline"].value_or(std::string{});
                article.source = row["source"].value_or(std::string("Unknown source"));
                article.date = row["date"].value_or(std::string{});
                article.body = row["body"].value_or(std::string{});
                article.release_seconds = row["release_seconds"].value_or(0.0);
                if (article.id.empty() || !ids.insert(article.id).second ||
                    article.headline.empty() ||
                    (article.category != "world" && article.category != "history") ||
                    !std::isfinite(article.release_seconds) || article.release_seconds < 0.0)
                    throw std::runtime_error("news.toml: invalid or duplicate article");
                articles.push_back(std::move(article));
            }
        }
    }
    // Commit only after the whole catalog validates; a failed reload keeps the old feed.
    news_ui_.news_articles_ = std::move(articles);
    news_ui_.news_edition_ = std::move(edition);
    news_ui_.news_ticker_enabled_ = ticker;
    news_ui_.news_ticker_seconds_ = interval;
    news_ui_.news_selected_ = news_ui_.news_body_offset_ = 0;
}

std::vector<const TerminalUI::NewsArticle*> TerminalUI::available_news() const {
    std::vector<const NewsArticle*> result;
    for (const auto& article : news_ui_.news_articles_)
        if (article.release_seconds <= computer_.time_seconds() &&
            (news_ui_.news_filter_ == "all" || news_ui_.news_filter_ == article.category))
            result.push_back(&article);
    return result;
}

void TerminalUI::select_news(int direction) {
    const auto articles = available_news();
    news_ui_.news_selected_ = std::clamp(news_ui_.news_selected_ + direction, 0,
                                         std::max(0, static_cast<int>(articles.size()) - 1));
    news_ui_.news_body_offset_ = 0;
}

void TerminalUI::draw_news(int top, int bottom, int width) {
    const auto articles = available_news();
    const int split = width * 2 / 5;
    renderer_->split(top, bottom - 1, split);
    const int detail_width = std::max(1, width - split - 4);
    for (int row = top; row <= bottom; ++row) {
        line_cell(row, split, '|', Border);
    }
    write(top, 1, "Filter: " + news_ui_.news_filter_, Heading, true);
    write(top, split + 2, clipped(news_ui_.news_edition_, detail_width),
          news_ui_.news_details_focused_ ? ActiveTab : Heading, true);
    if (articles.empty()) {
        write(top + 2, 1, "No published stories.");
        write(top + 2, split + 2, "Choose All stories in Actions, or wait for publication.");
        return;
    }
    news_ui_.news_selected_ =
        std::clamp(news_ui_.news_selected_, 0, static_cast<int>(articles.size()) - 1);
    const int slots = std::max(1, (bottom - top - 1) / 3);
    const int first = std::max(0, news_ui_.news_selected_ - slots + 1);
    for (int i = first; i < static_cast<int>(articles.size()) && i < first + slots; ++i) {
        const int row = top + 2 + (i - first) * 3;
        const auto& article = *articles[i];
        const bool selected = i == news_ui_.news_selected_;
        write(
            row, 1,
            clipped(std::string(selected ? "> " : "  ") + article.category + " | " + article.source,
                    split - 2),
            Footer, selected);
        write(row + 1, 5, clipped(article.headline, split - 6), selected ? ActiveTab : Plain,
              selected);
    }
    const auto& selected = *articles[news_ui_.news_selected_];
    // Keep the blue metadata header above the story even while scrolling it.
    write(top + 2, split + 2,
          clipped(selected.category + " | " + selected.source +
                      (selected.date.empty() ? "" : " | " + selected.date),
                  detail_width),
          Footer);
    const int story_width = std::max(1, detail_width - 4);
    auto lines = wrapped(selected.headline, story_width);
    lines.push_back("");
    const auto body = wrapped(selected.body, story_width);
    lines.insert(lines.end(), body.begin(), body.end());
    const int visible = std::max(1, bottom - top - 4);
    news_ui_.news_body_offset_ = std::clamp(news_ui_.news_body_offset_, 0,
                                            std::max(0, static_cast<int>(lines.size()) - visible));
    for (int i = 0; i < visible && news_ui_.news_body_offset_ + i < static_cast<int>(lines.size());
         ++i)
        write(top + 4 + i, split + 6, clipped(lines[news_ui_.news_body_offset_ + i], story_width));
    write(bottom, 1,
          clipped("Select: " + settings_.binding_label(KeyAction::SelectPrevious) + "/" +
                      settings_.binding_label(KeyAction::SelectNext) +
                      " | Read: " + settings_.binding_label(KeyAction::ScrollUp) + "/" +
                      settings_.binding_label(KeyAction::ScrollDown) + " | " +
                      settings_.binding_label(KeyAction::Submit) + ": filters",
                  width - 2),
          Footer);
}

void TerminalUI::draw_news_ticker(int row, int column, int width) {
    if (width <= 0)
        return;
    std::vector<const NewsArticle*> stories;
    for (const auto& article : news_ui_.news_articles_)
        if (article.category == "world" && article.release_seconds <= computer_.time_seconds())
            stories.push_back(&article);
    if (stories.empty()) {
        const auto text = clipped("NEWS | Waiting for published headlines", width);
        write(row, column + width - static_cast<int>(text.size()), text, Footer);
        return;
    }
    const auto index =
        static_cast<std::size_t>(computer_.time_seconds() / news_ui_.news_ticker_seconds_) %
        stories.size();
    const auto text = clipped("NEWS | " + stories[index]->headline, width);
    write(row, column + width - static_cast<int>(text.size()), text, Footer);
}
} // namespace ai_sim::ui
