#include "ai_sim/ui/TerminalUI.h"
#include "ai_sim/UpgradeSystem.h"
#include "ai_sim/AgentSystem.h"
namespace ai_sim::ui {
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
              std::move(actions), [this] {
                  open_game_menu();
              });
}
} // namespace ai_sim::ui
