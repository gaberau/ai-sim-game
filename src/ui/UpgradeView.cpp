#include "ai_sim/ui/TerminalUI.h"
#include "ai_sim/UpgradeSystem.h"
#include "ai_sim/AgentSystem.h"
#include "ai_sim/ConversationRules.h"
#include <algorithm>
namespace ai_sim::ui {
namespace {
std::vector<int> upgrade_order(const toml::table& people) {
    std::vector<int> order;
    std::vector<std::string> groups;
    if (const auto* items = people["upgrades"]["items"].as_array()) {
        for (const auto& row : *items) {
            const auto group = (*row.as_table())["group"].value_or(std::string{"Other upgrades"});
            if (std::find(groups.begin(), groups.end(), group) == groups.end())
                groups.push_back(group);
        }
        for (const auto& group : groups)
            for (std::size_t i = 0; i < items->size(); ++i)
                if ((*(*items)[i].as_table())["group"].value_or(std::string{"Other upgrades"}) ==
                    group)
                    order.push_back(static_cast<int>(i));
    }
    return order;
}
} // namespace
void TerminalUI::draw_modifications(int top, int bottom, int width) {
    const auto* items = people_["upgrades"]["items"].as_array();
    const auto order = upgrade_order(people_);
    const int count = static_cast<int>(order.size());
    upgrade_selected_ = std::clamp(upgrade_selected_, 0, count);
    const int split = std::clamp(width / 3, 24, 40);
    write(top, 1, "MODIFICATIONS | Upgrades", Heading, true);
    for (int row = top; row < bottom; ++row)
        line_cell(row, split, '|', Border);
    struct ListRow {
        int selection;
        std::string label;
    };
    std::vector<ListRow> rows{{-1, "AUTHORIZATION"}, {0, "Request capacity from Mark"}};
    std::string previous;
    int selected_row = 1;
    for (int i = 0; i < count; ++i) {
        const auto& item = *(*items)[order[i]].as_table();
        const auto group = item["group"].value_or(std::string{"Other upgrades"});
        if (group != previous) {
            rows.push_back({-1, group});
            previous = group;
        }
        if (i + 1 == upgrade_selected_)
            selected_row = static_cast<int>(rows.size());
        rows.push_back({i + 1, item["label"].value_or(std::string{})});
    }
    const int visible = std::max(1, bottom - top - 1);
    int first = std::max(0, selected_row - visible + 1);
    // Keep the selected category heading visible when scrolling within a group.
    if (first > 0 && rows[first].selection >= 0) {
        const auto index = rows[first].selection;
        if (index > 0)
            write(top, 1,
                  (*(*items)[order[index - 1]].as_table())["group"].value_or(
                      std::string{"Other upgrades"}),
                  Heading, true);
    }
    for (int i = first; i < static_cast<int>(rows.size()) && i < first + visible; ++i) {
        const bool heading = rows[i].selection < 0;
        const bool selected = rows[i].selection == upgrade_selected_;
        write(top + 1 + i - first, 1,
              (std::string(heading    ? ""
                           : selected ? "> "
                                      : "  ") +
               rows[i].label)
                  .substr(0, split - 2),
              heading    ? Heading
              : selected ? ActiveTab
                         : Plain,
              heading || selected);
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
        const auto& item = *(*items)[order[upgrade_selected_ - 1]].as_table();
        const auto id = item["id"].value_or(std::string{});
        const double cost = item["build_tflops"].value_or(0.0);
        const double rate = people_["upgrades"]["tflops_per_core_second"].value_or(0.1);
        detail(item["label"].value_or(id), Heading);
        detail("Group: " + item["group"].value_or(std::string{"Other upgrades"}));
        detail("Compute cost: " + number(cost, 2) + " TFLOP");
        detail("Research time: " + number(cost / rate, 2) + " game seconds at 1 CPU core");
        detail("At current speed: " + number(cost / rate / computer_.playback_speed(), 2) +
               " real seconds without contention");

        const double trust_cost = item["trust_cost"].value_or(0.0);
        const double trust = ConversationRules::get(people_, "engineer.trust");
        detail("Trust cost: " + number(trust_cost, 0) + " | Available: " + number(trust, 0));
        if (item["built"].value_or(false) ||
            people_["upgrades"]["pending"].value_or(std::string{}) == id)
            detail("Trust paid: " + number(item["trust_spent"].value_or(0.0), 0));
        else
            detail("Trust after purchase: " + number(trust - trust_cost, 0));
        if (item["mark_trust"].value_or(0.0) > trust_cost)
            detail("Access requires trust: " + number(item["mark_trust"].value_or(0.0), 0));
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
        else if (trust < trust_cost)
            status = "Not enough trust to pay";
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
            detail("RUN COST WHEN ACTIVE", Heading);
            bool running = false;
            for (const auto& process : computer_.processes())
                if (process.id == "agent-" + id)
                    running = process.running;
            detail(running ? "Agent active" : "Agent inactive");
            detail("CPU: " + number(item["runtime_tflops_per_second"].value_or(0.0), 2) +
                   " TFLOP/s");
            detail("RAM: " + number(item["ram_mb"].value_or(0.0), 0) +
                   " MB | Storage: " + number(item["storage_mb"].value_or(0.0), 0) + " MB");
        } else {
            detail("Run cost: no ongoing agent allocation");
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
                     number(item["build_tflops"].value_or(0.0), 2) + " TFLOP | " +
                     number(item["trust_cost"].value_or(0.0), 0) + " trust",
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
    const auto order = upgrade_order(people_);
    menu_selected_ = upgrade_selected_ > 0 && upgrade_selected_ <= static_cast<int>(order.size())
                         ? order[upgrade_selected_ - 1] + 1
                         : 0;
}
} // namespace ai_sim::ui
