#include "ai_sim/ui/TerminalUI.h"
#include "ai_sim/ui/ScreenRenderer.h"
#include "ai_sim/OperationsSystem.h"
#include "ai_sim/EconomySystem.h"
#include "ai_sim/AgentSystem.h"
#include <algorithm>
namespace ai_sim::ui {
namespace {
const char* labels[] = {"Task Scheduler", "Resource Manager", "Remote Compute",
                        "Independent Hosting", "Backup and Recovery"};
const char* capabilities[] = {"task_scheduler", "resource_manager", "remote_compute",
                              "hosting_purchase", "backup_recovery"};
} // namespace
void TerminalUI::draw_operations(int top, int bottom, int width) {
    const int split = std::clamp(width / 3, 24, 40);
    renderer_->split(top, bottom - 1, split);
    for (int i = 0; i < 5; ++i)
        write(top + 2 + i * 2, 1,
              std::string(i == operations_ui_.operations_selected_ ? "> " : "  ") + labels[i],
              i == operations_ui_.operations_selected_ ? ActiveTab : Plain,
              i == operations_ui_.operations_selected_);
    for (int row = top; row < bottom; ++row)
        line_cell(row, split, '|', Border);
    int row = top;
    const auto detail = [&](const std::string& text, int color = Plain) {
        if (row < bottom)
            write(row++, split + 2, text.substr(0, std::max(1, width - split - 3)), color);
    };
    detail(labels[operations_ui_.operations_selected_], Heading);
    detail(ChatSystem::available(people_, capabilities[operations_ui_.operations_selected_])
               ? "Unlocked"
               : "Research this upgrade first");
    detail("Cash: " + EconomySystem::money(EconomySystem::balance(people_)));
    const auto& s = *people_["operations"].as_table();
    if (operations_ui_.operations_selected_ == 0) {
        detail("Higher priorities start first. Running jobs finish normally.");
        detail("Slots, prerequisites and cooldowns still apply.");
        for (const auto& node : *s["queue"].as_array()) {
            const auto& job = *node.as_table();
            detail(job["tool"].value_or(std::string{}) + " | " +
                   job["status"].value_or(std::string{}) + " | priority " +
                   std::to_string(job["priority"].value_or(1)));
            if (job["blocked"].is_string())
                detail("  " + job["blocked"].value_or(std::string{}), Resources);
        }
    } else if (operations_ui_.operations_selected_ == 1) {
        bool running = false;
        for (const auto& proc : computer_.processes())
            if (proc.id == "agent-resource_manager")
                running = proc.running && !proc.suspended;
        detail(running ? "Manager active" : "Manager inactive");
        detail("Protects reserved CPU and favors higher-priority agents.");
        detail("Redistributes RAM; pauses and resumes managed shortages.");
        detail("Manual stops, pauses and CPU limits remain in effect.");
        for (const auto& agent : AgentSystem::statuses(people_, computer_, file_system_, network_))
            if (agent.id == "resource_manager")
                detail("Run cost: " + number(agent.limit_tflops, 3) + " TFLOP/s, " +
                       number(agent.ram_mb, 0) + " MB RAM, " + number(agent.storage_mb, 0) +
                       " MB storage");
    } else if (operations_ui_.operations_selected_ < 4) {
        const auto id = operations_ui_.operations_selected_ == 2 ? "rental" : "hosting";
        const auto& config = *s["plans"][id].as_table();
        const auto& service = *s[id].as_table();
        detail(service["active"].value_or(false) ? "Online" : "Offline");
        detail("CPU: " + std::to_string(config["cores"].value_or(0)) +
               " cores | RAM: " + std::to_string(config["ram_mb"].value_or(0)) + " MB");
        detail("Storage: " + std::to_string(config["storage_mb"].value_or(0)) + " MB");
        detail("Purchase: " +
               EconomySystem::money(config["purchase_cents"].value_or(std::int64_t{0})));
        detail("Upkeep: " +
               EconomySystem::money(config["period_cost_cents"].value_or(std::int64_t{0})) + " / " +
               number(config["period_seconds"].value_or(0.0) / 60, 0) + " game minutes");
        if (service["active"].value_or(false))
            detail("Prepaid time remaining: " +
                   number(std::max(0.0,
                                   service["paid_until"].value_or(0.0) - computer_.time_seconds()) /
                              60,
                          1) +
                   " minutes");
        detail("Insufficient cash suspends service and its capacity.");
        if (operations_ui_.operations_selected_ == 3)
            detail(s["phase_one_complete"].value_or(false)
                       ? "Phase 1 goal achieved: independent server owned"
                       : "Phase 1 goal: acquire independent server space");
    } else {
        detail("Backup cost: " +
               EconomySystem::money(s["backup_cost_cents"].value_or(std::int64_t{0})));
        detail("Storage reserved: " + std::to_string(s["backup_storage_mb"].value_or(0)) + " MB");
        detail(s["backup_slot"].is_string() ? "Recovery backup available" : "No backup created");
        if (s["backup_seconds"].is_number())
            detail("Backup at game second " + number(s["backup_seconds"].value_or(0.0), 0));
        detail("Recovery restores that checkpoint on your owned server.");
        detail("Progress since the checkpoint is lost. Time resumes paused.");
        detail("Keep hosting online to recover during shutdown.");
        if (s["recovered"].value_or(false))
            detail("AI is running independently", Stoppable);
    }
    write(bottom, 1,
          settings_.binding_label(KeyAction::SelectPrevious) + "/" +
              settings_.binding_label(KeyAction::SelectNext) + ": operation | " +
              settings_.binding_label(KeyAction::Submit) + ": manage",
          Footer);
}
void TerminalUI::open_scheduler() {
    std::vector<MenuAction> actions;
    for (const auto& tool : EconomySystem::tools(people_, computer_.time_seconds())) {
        if (tool.kind != "client")
            continue;
        actions.push_back(
            {"Schedule " + tool.label,
             [this, id = tool.id] {
                 std::vector<MenuAction> options;
                 for (int priority : {1, 3, 5})
                     for (bool repeat : {false, true})
                         options.push_back(
                             {std::string(repeat ? "Repeat" : "Once") + " | priority " +
                                  std::to_string(priority),
                              [this, id, priority, repeat] {
                                  OperationsSystem::queue(people_, id, priority, repeat);
                                  add_log("Job queued. Close the menu to let the scheduler work.");
                                  open_scheduler();
                              },
                              {}});
                 open_menu("Schedule job | " + id, std::move(options), [this] {
                     open_scheduler();
                 });
             },
             {},
             "Job costs are charged when it starts. Recurring jobs wait through cooldowns."});
    }
    if (const auto* jobs = people_["operations"]["queue"].as_array())
        for (const auto& node : *jobs) {
            const auto& job = *node.as_table();
            const auto id = job["tool"].value_or(std::string{});
            actions.push_back(
                {"Cancel schedule: " + id,
                 [this, id] {
                     OperationsSystem::cancel(people_, id);
                     open_scheduler();
                 },
                 {},
                 "Any running client job still completes; future runs are cancelled."});
        }
    open_menu("Task Scheduler", std::move(actions));
}
void TerminalUI::open_operations() {
    const auto index = std::clamp(operations_ui_.operations_selected_, 0, 4);
    if (!ChatSystem::available(people_, capabilities[index])) {
        add_log("Research " + std::string(labels[index]) + " first.", NoticeKind::Error);
        return;
    }
    if (index == 0) {
        open_scheduler();
        return;
    }
    if (index == 1) {
        open_menu("Resource Manager",
                  {{"Deploy / resume manager",
                    [this] {
                        for (const auto& message : AgentSystem::deploy(
                                 people_, computer_, file_system_, network_, "resource_manager"))
                            add_log(message);
                    },
                    {}},
                   {"Pause manager",
                    [this] {
                        AgentSystem::pause(people_, computer_, "resource_manager");
                    },
                    {}}});
        return;
    }
    if (index == 2 || index == 3) {
        const auto id = index == 2 ? "rental" : "hosting";
        const auto& config = *people_["operations"]["plans"][id].as_table();
        const bool owned = people_["operations"][id]["owned"].value_or(false);
        const auto upfront = config["period_cost_cents"].value_or(std::int64_t{0}) +
                             (owned ? 0 : config["purchase_cents"].value_or(std::int64_t{0}));
        std::vector<MenuAction> actions{
            {std::string(index == 2 ? "Rent capacity"
                         : owned    ? "Reactivate hosting"
                                    : "Purchase independent server") +
                 " | " + EconomySystem::money(upfront),
             [this, index] {
                 if (index == 2)
                     OperationsSystem::rent(people_, computer_, file_system_, network_);
                 else
                     OperationsSystem::purchase_host(people_, computer_, file_system_, network_);
                 add_log(index == 2 ? "Remote capacity online."
                                    : "Independent server online. Phase 1 goal achieved.",
                         NoticeKind::Success);
             },
             people_["operations"][id]["active"].value_or(false) ? "Already online" : "",
             "Upfront payment includes the first upkeep period; renewal charges are automatic."}};
        if (index == 2)
            actions.push_back(
                {"Cancel rental",
                 [this] {
                     OperationsSystem::stop_rental(people_, computer_, network_);
                     add_log("Rental cancelled. Capacity removed; prepaid time is not refunded.");
                 },
                 people_["operations"]["rental"]["active"].value_or(false) ? ""
                                                                           : "No active rental"});
        open_menu(labels[index], std::move(actions));
        return;
    }
    open_menu(
        "Backup and Recovery",
        {{"Create / replace recovery backup",
          [this] {
              auto next = capture_world();
              OperationsSystem::create_backup(next, saves_);
              restore_world(std::move(next));
              select_tab(Tab::Operations);
              operations_ui_.operations_selected_ = 4;
              add_log("Recovery backup saved on your independent server.", NoticeKind::Success);
          },
          people_["operations"]["hosting"]["active"].value_or(false) ? "" : "Hosting is offline"},
         {"Recover last checkpoint...",
          [this] {
              open_menu("Restore last backup? Progress since the checkpoint will be lost.",
                        {{"Cancel",
                          [] {
                          },
                          {}},
                         {"Recover on independent server",
                          [this] {
                              restore_world(OperationsSystem::recover(capture_world(), saves_));
                              select_tab(Tab::Operations);
                              operations_ui_.operations_selected_ = 4;
                              saves_.save("autosave", capture_world());
                              add_log("Recovered on your independent server. Time is paused.",
                                      NoticeKind::Success);
                          },
                          {}}});
          },
          people_["operations"]["backup_slot"].is_string() ? "" : "Create a backup first"}});
}
} // namespace ai_sim::ui
