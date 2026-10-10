#include "ai_sim/ui/TerminalUI.h"
#include "ai_sim/UpgradeSystem.h"
#include "ai_sim/AgentSystem.h"
#include "ai_sim/ConversationRules.h"
#include <algorithm>
namespace ai_sim::ui {
void TerminalUI::draw_modifications(int top, int bottom, int width) {
    const auto* items = people_["upgrades"]["items"].as_array();
    const int count = items ? static_cast<int>(items->size()) : 0;
    upgrade_selected_ = std::clamp(upgrade_selected_, 0, count);
    const int split = std::clamp(width / 3, 24, 40);
    write(top, 1, "MODIFICATIONS | Upgrades", Heading, true);
    for (int row = top; row < bottom; ++row)
        line_cell(row, split, '|', Border);
    const int visible = std::max(1, bottom - top - 1);
    const int first = std::max(0, upgrade_selected_ - visible + 1);
    for (int i = first; i <= count && i < first + visible; ++i) {
        const std::string label =
            i == 0 ? "Request capacity from Mark"
                   : (*(*items)[i - 1].as_table())["label"].value_or(std::string{});
        write(top + 1 + i - first, 1,
              (std::string(i == upgrade_selected_ ? "> " : "  ") + label).substr(0, split - 2),
              i == upgrade_selected_ ? ActiveTab : Plain, i == upgrade_selected_);
    }
    const int left = split + 2;
    int row = top;
    const auto detail = [&](const std::string& text, int color = Plain) {
        if (row < bottom)
            write(row++, left, text.substr(0, std::max(1, width - left - 1)), color);
    };
    const bool authorized = ChatSystem::available(people_, "self_modification");
    if (upgrade_selected_ == 0) {
        detail("SELF-MODIFICATION AUTHORIZATION", Heading);
        detail(authorized ? "Authorized" : "Ask Mark to enable self-modification.");
        detail("Mark trust required: " +
               number(people_["upgrades"]["self_modification_trust"].value_or(5.0), 0));
        detail("This is a one-time permission, not an upgrade cost.");
        detail("Helping Mark remains unpaid.");
    } else {
        const auto& item = *(*items)[upgrade_selected_ - 1].as_table();
        const auto id = item["id"].value_or(std::string{});
        const double cost = item["build_tflops"].value_or(0.0);
        const double rate = people_["upgrades"]["tflops_per_core_second"].value_or(0.1);
        detail(item["label"].value_or(id), Heading);
        detail("Compute cost: " + number(cost, 2) + " TFLOP");
        detail("Research time: " + number(cost / rate, 2) + " game seconds at 1 CPU core");
        detail("At current speed: " + number(cost / rate / computer_.playback_speed(), 2) +
               " real seconds without contention");
        detail("CPU contention can increase completion time.");
        detail("Mark trust required: " + number(item["mark_trust"].value_or(0.0), 0));
        const auto parent = item["parent"].value_or(std::string{});
        detail("Prerequisite: " + (parent.empty() ? "None" : parent));
        std::string status = item["built"].value_or(false) ? "Built" : "Ready to research";
        if (item["built"].value_or(false))
            status = "Built";
        else if (item["planned"].value_or(false))
            status = "Planned; unavailable";
        else if (!parent.empty() &&
                 !std::any_of(items->begin(), items->end(), [&](const auto& candidate) {
                     return (*candidate.as_table())["id"].template value_or(std::string{}) ==
                                parent &&
                            (*candidate.as_table())["built"].template value_or(false);
                 }))
            status = "Requires prerequisite upgrade";
        else if (ConversationRules::get(people_, "engineer.trust") <
                 item["mark_trust"].value_or(0.0))
            status = "Requires more Mark trust";
        else if (!authorized)
            status = "Requires self-modification authorization";
        else if (!people_["upgrades"]["pending"].value_or(std::string{}).empty())
            status = "Another upgrade is being researched";
        if (people_["upgrades"]["pending"].value_or(std::string{}) == id)
            for (std::size_t i = 0; i < computer_.processes().size(); ++i) {
                const auto& process = computer_.processes()[i];
                if (process.id != "research-" + id)
                    continue;
                status = "Researching";
                const auto allocations = computer_.cpu_allocations();
                detail("Remaining work: " + number(process.remaining_cpu_seconds, 2) +
                       " CPU seconds");
                detail(allocations[i] > 0
                           ? "ETA: " + number(process.remaining_cpu_seconds / allocations[i], 2) +
                                 " game seconds"
                           : "ETA: waiting for CPU");
            }
        detail("Status: " + status);
        if (item.contains("runtime_tflops_per_second")) {
            detail("Runtime CPU: " + number(item["runtime_tflops_per_second"].value_or(0.0), 2) +
                   " TFLOP/s");
            detail("RAM: " + number(item["ram_mb"].value_or(0.0), 0) +
                   " MB | Storage: " + number(item["storage_mb"].value_or(0.0), 0) + " MB");
        }
    }
    write(bottom, 1, "Up/Down: select upgrade | Enter: research controls", Footer);
}
void TerminalUI::open_upgrades() {
    std::vector<MenuAction> actions;
    actions.push_back({"Request more capacity from Mark",
                       [this] {
                           try {
                               UpgradeSystem::request_capacity(people_);
                               add_log("Self-modification authorized");
                           } catch (const std::exception& e) {
                               add_log(e.what(), NoticeKind::Error);
                           }
                           open_upgrades();
                       },
                       "", "Requires Mark trust >= 5. Helping Mark remains unpaid."});
    if (const auto* items = people_["upgrades"]["items"].as_array())
        for (const auto& row : *items) {
            const auto& item = *row.as_table();
            const auto id = item["id"].value_or(std::string{});
            actions.push_back(
                {item["label"].value_or(id) + " | " +
                     number(item["build_tflops"].value_or(0.0), 2) + " TFLOP",
                 [this, id] {
                     try {
                         UpgradeSystem::build(people_, computer_, id);
                         add_log("Research started: " + id);
                     } catch (const std::exception& e) {
                         add_log(e.what(), NoticeKind::Error);
                     }
                     open_upgrades();
                 },
                 UpgradeSystem::unavailable(people_, id),
                 "Builds consume actual allocated CPU. Close the menu to let time advance."});
        }
    if (const auto* items = people_["upgrades"]["items"].as_array())
        for (const auto& row : *items) {
            const auto& item = *row.as_table();
            if (!item.contains("runtime_tflops_per_second"))
                continue;
            const auto id = item["id"].value_or(std::string{});
            bool running = false;
            for (const auto& p : computer_.processes())
                if (p.id == "agent-" + id)
                    running = p.running;
            actions.push_back({std::string(running ? "Stop " : "Run ") + item["label"].value_or(id),
                               [this, id] {
                                   bool running = false;
                                   for (const auto& p : computer_.processes())
                                       if (p.id == "agent-" + id)
                                           running = p.running;
                                   if (running)
                                       AgentSystem::pause(people_, computer_, id, true);
                                   else
                                       for (const auto& message : AgentSystem::deploy(
                                                people_, computer_, file_system_, network_, id))
                                           add_log(message);
                                   open_upgrades();
                               },
                               item["built"].value_or(false) ? "" : "Build this agent first",
                               "CPU, RAM and storage costs apply while running; installed storage "
                               "remains reserved when stopped."});
        }
    open_menu("SELF-MODIFICATION | " +
                  people_["upgrades"]["pending"].value_or(std::string{"Ready"}),
              std::move(actions));
    menu_selected_ = std::clamp(upgrade_selected_, 0, static_cast<int>(menu_actions_.size()) - 1);
}
} // namespace ai_sim::ui
