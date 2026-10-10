#include "ai_sim/ui/TerminalUI.h"
#include "ai_sim/EconomySystem.h"
#include "ai_sim/UpgradeSystem.h"
#include <algorithm>
#include <stdexcept>

namespace ai_sim::ui {
void TerminalUI::draw_progression(int top, int bottom, int width) {
    std::vector<std::string> rows;
    if (selected_tab_ == Tab::Money) {
        rows.push_back("MONEY-MAKING TOOLS | Balance " +
                       EconomySystem::money(EconomySystem::balance(people_)));
        for (const auto& tool : EconomySystem::tools(people_, computer_.time_seconds())) {
            rows.push_back(tool.label + " | " + tool.status + " | " +
                           EconomySystem::money(tool.payout_cents));
            if (!tool.unavailable.empty())
                rows.push_back("  " + tool.unavailable);
        }
    } else {
        rows.push_back("MODIFICATIONS | " +
                       people_["upgrades"]["pending"].value_or(std::string{"Ready"}));
        rows.push_back(people_["upgrades"]["capacity_requested"].value_or(false)
                           ? "Self-modification authorized"
                           : "Request more capacity from Mark to authorize self-modification.");
        if (const auto* items = people_["upgrades"]["items"].as_array())
            for (const auto& row : *items) {
                const auto& item = *row.as_table();
                const auto id = item["id"].value_or(std::string{});
                const auto reason = UpgradeSystem::unavailable(people_, id);
                rows.push_back(item["label"].value_or(id) + " | " +
                               (reason.empty() ? "Ready to research" : reason));
            }
    }
    ai_offset_ =
        std::clamp(ai_offset_, 0, std::max(0, static_cast<int>(rows.size()) - (bottom - top)));
    for (int row = top; row < bottom && ai_offset_ + row - top < static_cast<int>(rows.size());
         ++row)
        write(row, 1, rows[ai_offset_ + row - top].substr(0, std::max(0, width - 2)),
              row == top ? Heading : Plain);
    write(bottom, 1,
          settings_.binding_label(KeyAction::Submit) + ": " +
              (selected_tab_ == Tab::Money ? "manage tools" : "research and capacity"),
          Footer);
}
void TerminalUI::open_money_tools() {
    std::vector<MenuAction> actions;
    const auto time = computer_.time_seconds();
    for (const auto& tool : EconomySystem::tools(people_, time)) {
        std::string detail = tool.description + " | " + EconomySystem::money(tool.payout_cents) +
                             " gross / " + EconomySystem::money(tool.cost_cents) + " cost per " +
                             number(tool.duration_seconds / 60, 0) + " working minutes | " +
                             std::to_string(tool.slots) + " slot(s).";
        if (tool.due_seconds)
            detail += " Next payment in " +
                      number(std::max(0.0, *tool.due_seconds - time) / 60, 1) + " working minutes.";
        actions.push_back({tool.label + " | " + tool.status,
                           [this, id = tool.id] {
                               open_money_tool(id);
                           },
                           {},
                           detail});
    }
    if (actions.empty())
        actions.push_back({"No money-making tools configured", {}, {}});
    open_menu("Money-making tools | Balance " +
                  EconomySystem::money(EconomySystem::balance(people_)),
              std::move(actions));
}
void TerminalUI::open_money_tool(const std::string& id) {
    const auto tools = EconomySystem::tools(people_, computer_.time_seconds());
    const auto selected = std::find_if(tools.begin(), tools.end(), [&](const auto& tool) {
        return tool.id == id;
    });
    if (selected == tools.end())
        throw std::runtime_error("Unknown money-making tool");
    const auto tool = *selected;
    std::vector<MenuAction> actions;
    actions.push_back(
        {"Start " + tool.label,
         [this, id] {
             if (computer_.day_finished())
                 throw std::runtime_error("Start the next workday first.");
             EconomySystem::start(people_, id, computer_.time_seconds());
             add_log("Started paid work. Cycle costs charged; payment arrives on completion.",
                     NoticeKind::Success);
             open_money_tool(id);
         },
         computer_.day_finished() ? "The workday has ended." : tool.unavailable,
         "Gross payment " + EconomySystem::money(tool.payout_cents) + "; prepaid cycle cost " +
             EconomySystem::money(tool.cost_cents) + "; duration " +
             number(tool.duration_seconds / 60, 0) + " working minutes."});
    if (tool.kind == "product" && tool.status == "running")
        actions.push_back(
            {"Stop after the current cycle",
             [this, id] {
                 EconomySystem::stop(people_, id);
                 add_log("Product will finish its prepaid cycle, then stop.", NoticeKind::Success);
                 open_money_tool(id);
             },
             {},
             "The current cycle still pays. No further renewal costs will be charged."});
    actions.push_back({"Description and costs",
                       [this, tool] {
                           show_message(
                               tool.label,
                               tool.description +
                                   " Gross payment: " + EconomySystem::money(tool.payout_cents) +
                                   ". Cycle cost: " + EconomySystem::money(tool.cost_cents) +
                                   ". Net per cycle: " +
                                   EconomySystem::money(tool.payout_cents - tool.cost_cents) +
                                   ". Duration: " + number(tool.duration_seconds / 60, 0) +
                                   " working minutes. Work slots: " + std::to_string(tool.slots) +
                                   ". Completed: " + std::to_string(tool.completed_cycles) + ". " +
                                   tool.unavailable);
                           menu_back_ = [this, id = tool.id] {
                               open_money_tool(id);
                           };
                       },
                       {}});
    open_menu(tool.label + " | " + tool.status + " | " +
                  EconomySystem::money(EconomySystem::balance(people_)),
              std::move(actions), [this] {
                  open_money_tools();
              });
}
} // namespace ai_sim::ui
