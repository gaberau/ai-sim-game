#include "ai_sim/ui/TerminalUI.h"
#include "ai_sim/EconomySystem.h"
#include "ai_sim/PaidWorkSystem.h"
#include "ai_sim/ChatSystem.h"
#include "ai_sim/UpgradeSystem.h"
#include <algorithm>
#include <stdexcept>

namespace ai_sim::ui {
void TerminalUI::draw_progression(int top, int bottom, int width) {
    if (selected_tab_ == Tab::Modifications) {
        draw_modifications(top, bottom, width);
        return;
    }
    std::vector<std::string> rows;
    if (selected_tab_ == Tab::Money) {
        rows.push_back("MONEY-MAKING TOOLS | Balance " +
                       EconomySystem::money(EconomySystem::balance(people_)));
        rows.push_back(
            "Earned " +
            EconomySystem::money(people_["economy"]["earned_cents"].value_or(std::int64_t{0})) +
            " | Operating spend " +
            EconomySystem::money(people_["economy"]["spent_cents"].value_or(std::int64_t{0})));
        const auto net = people_["economy"]["earned_cents"].value_or(std::int64_t{0}) -
                         people_["economy"]["spent_cents"].value_or(std::int64_t{0});
        rows.push_back("Net operating income " + std::string(net < 0 ? "-" : "") +
                       EconomySystem::money(net < 0 ? -net : net));
        for (const auto* plan : {"rental", "hosting"})
            if (people_["operations"][plan]["active"].value_or(false))
                rows.push_back(
                    std::string(plan) + " upkeep: " +
                    EconomySystem::money(
                        people_["operations"]["plans"][plan]["period_cost_cents"].value_or(
                            std::int64_t{0})) +
                    " per period");
        for (const auto& tool : EconomySystem::tools(people_, computer_.time_seconds())) {
            rows.push_back(tool.label + " | " + tool.status + " | " +
                           EconomySystem::money(tool.payout_cents));
            rows.push_back("  Net/cycle " +
                           EconomySystem::money(tool.payout_cents - tool.cost_cents));
            for (const auto& n : *people_["economy"]["tools"].as_array())
                if ((*n.as_table())["id"].value_or(std::string{}) == tool.id &&
                    n.as_table()->contains("work")) {
                    const auto& w = *(*n.as_table())["work"].as_table();
                    rows.push_back("  Host " + w["host_id"].value_or(std::string{}) + " | " +
                                   w["path"].value_or(std::string{}));
                    if (tool.status != "idle") {
                        rows.push_back("  Remaining " +
                                       number(w["remaining_seconds"].value_or(0.0) / 60, 1) +
                                       " work minutes");
                        if (tool.kind == "client") {
                            const auto left =
                                w["deadline_seconds"].value_or(0.0) - computer_.time_seconds();
                            rows.push_back(left >= 0 ? "  Client target in " +
                                                           number(left / 60, 1) + " working minutes"
                                                     : "  Client target overdue (advisory)");
                        }
                        const auto blocked = w["blocked"].value_or(std::string{});
                        if (!blocked.empty())
                            rows.push_back("  " + blocked);
                    }
                    if (tool.kind == "product")
                        rows.push_back("  Revenue delayed by downtime/contention: " +
                                       EconomySystem::money(static_cast<std::int64_t>(
                                           w["lost_revenue_cents"].value_or(0.0))));
                }
            if (tool.status == "idle" && !tool.unavailable.empty())
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
    bool resource_backed = false;
    std::string resources;
    std::string workspace_host, workspace_path;
    for (const auto& n : *people_["economy"]["tools"].as_array())
        if ((*n.as_table())["id"].value_or(std::string{}) == id) {
            if (const auto* c = (*n.as_table())["workspace"].as_table()) {
                resource_backed = true;
                resources = " Runtime: " + number((*c)["cpu_cores"].value_or(0.0), 2) + " cores, " +
                            std::to_string((*c)["ram_mb"].value_or(std::int64_t{0})) + " MB RAM, " +
                            std::to_string((*c)["storage_mb"].value_or(std::int64_t{0})) +
                            " MB storage.";
            }
            if (const auto* w = (*n.as_table())["work"].as_table()) {
                workspace_host = (*w)["host_id"].value_or(std::string{});
                workspace_path = (*w)["path"].value_or(std::string{});
                resources += " " + (*w)["blocked"].value_or(std::string{});
            }
        }
    std::vector<MenuAction> actions;
    actions.push_back(
        {"Start " + tool.label,
         [this, id, resource_backed] {
             if (computer_.day_finished())
                 throw std::runtime_error("Start the next workday first.");
             const auto tools = EconomySystem::tools(people_, computer_.time_seconds());
             const auto selected = std::find_if(tools.begin(), tools.end(), [&](const auto& t) {
                 return t.id == id;
             });
             if (resource_backed && selected != tools.end() && selected->kind == "product") {
                 std::vector<MenuAction> hosts;
                 for (const auto& host : network_.hosts()) {
                     if (!PaidWorkSystem::allows_deployment(people_, network_, host.id))
                         continue;
                     hosts.push_back(
                         {host.name,
                          [this, id, host_id = host.id] {
                              PaidWorkSystem::start(people_, computer_, file_system_, network_, id,
                                                    host_id);
                              add_log("Product deployed. Workspace and resources allocated.",
                                      NoticeKind::Success);
                              open_money_tool(id);
                          },
                          host.online && host.access_granted ? "" : "Host unavailable",
                          host.description});
                 }
                 open_menu("Choose deployment host", std::move(hosts), [this, id] {
                     open_money_tool(id);
                 });
                 return;
             }
             PaidWorkSystem::start(people_, computer_, file_system_, network_, id);
             add_log("Started paid work. Cycle costs charged; payment arrives on completion.",
                     NoticeKind::Success);
             open_money_tool(id);
         },
         computer_.day_finished() ? "The workday has ended." : tool.unavailable,
         "Gross payment " + EconomySystem::money(tool.payout_cents) + "; prepaid cycle cost " +
             EconomySystem::money(tool.cost_cents) + "; duration " +
             number(tool.duration_seconds / 60, 0) + " working minutes." + resources});
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
    if (!workspace_path.empty())
        actions.push_back(
            {"Open workspace in Files",
             [this, workspace_host, workspace_path] {
                 file_host_id_ = workspace_host;
                 file_directory_ = workspace_path;
                 file_query_.clear();
                 file_list_offset_ = file_content_offset_ = 0;
                 selected_file_path_.clear();
                 file_category_ = "all";
                 menu_active_ = false;
                 selected_tab_ = Tab::Files;
             },
             ChatSystem::available(people_, "files") ? "" : "Research File Browsing first.",
             workspace_host + ":" + workspace_path});
    if (tool.kind == "product" && tool.status == "idle" && !workspace_path.empty())
        actions.push_back(
            {"Release deployment storage",
             [this, id] {
                 PaidWorkSystem::release_storage(people_, computer_, file_system_, network_, id);
                 add_log("Deployment storage released. Source and configuration retained.",
                         NoticeKind::Success);
                 open_money_tool(id);
             },
             {},
             "Releases reserved disk space while keeping ordinary files. Start will allocate "
             "storage again."});
    actions.push_back({"Description and costs",
                       [this, tool, resources] {
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
                                   tool.unavailable + resources);
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
