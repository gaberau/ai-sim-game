#include "ai_sim/ui/TerminalUI.h"
#include "ai_sim/EconomySystem.h"
#include <algorithm>
#include <stdexcept>

namespace ai_sim::ui {
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
              std::move(actions), [this] {
                  open_game_menu();
              });
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
