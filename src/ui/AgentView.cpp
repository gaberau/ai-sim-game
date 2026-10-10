#include "ai_sim/ui/TerminalUI.h"
#include "ai_sim/AgentSystem.h"
#include <algorithm>
#include <numeric>
#include <sstream>
#include <cmath>
#include <stdexcept>
namespace ai_sim::ui {
namespace {
AgentStatus selected(const std::vector<AgentStatus>& agents, const std::string& id) {
    for (const auto& agent : agents)
        if (agent.id == id)
            return agent;
    throw std::runtime_error("Unknown agent");
}
} // namespace
void TerminalUI::draw_agents(int top, int bottom, int width) {
    write(top, 1, "AGENTS | Workday " + std::to_string(computer_.day_index() + 1), Heading, true);
    const auto allocations = computer_.cpu_allocations();
    const double used = std::accumulate(allocations.begin(), allocations.end(), 0.0);
    const auto& host = network_.hosts()[network_.local_host_index()];
    std::vector<std::string> lines;
    lines.push_back("LOCAL DEPLOYMENT | CPU " + number(used, 2) + " / " +
                    number(computer_.cpu_cores(), 0) + " cores | RAM " +
                    number(computer_.allocated_memory_bytes() / 1000000.0, 0) + " / " +
                    number(computer_.memory_bytes() / 1000000.0, 0) + " MB");
    lines.push_back("Storage " + number(file_system_.storage_used(host.id) / 1000000.0, 0) + " / " +
                    number(host.storage_capacity_bytes / 1000000.0, 0) +
                    " MB | Workloads share CPU by priority.");
    const auto pending = people_["upgrades"]["pending"].value_or(std::string{});
    if (!pending.empty())
        for (std::size_t i = 0; i < computer_.processes().size(); ++i) {
            const auto& p = computer_.processes()[i];
            if (p.id == "research-" + pending) {
                const auto total = p.consumed_cpu_seconds + p.remaining_cpu_seconds;
                lines.push_back(
                    "RESEARCH | " + pending + " | " +
                    number(total > 0 ? 100 * p.consumed_cpu_seconds / total : 100, 0) + "% | " +
                    (allocations[i] > 0
                         ? "ETA " + number(p.remaining_cpu_seconds / allocations[i], 1) +
                               " working seconds"
                         : "Waiting for CPU"));
            }
        }
    else
        lines.push_back("RESEARCH | No build running. Enter -> Research upgrades.");
    const auto agents = AgentSystem::statuses(people_, computer_, file_system_, network_);
    agent_selected_ =
        std::clamp(agent_selected_, 0, std::max(0, static_cast<int>(agents.size()) - 1));
    std::vector<int> headings;
    for (std::size_t i = 0; i < agents.size(); ++i) {
        const auto& a = agents[i];
        lines.push_back("");
        headings.push_back(static_cast<int>(lines.size()));
        lines.push_back(std::string(static_cast<int>(i) == agent_selected_ ? "> " : "  ") +
                        a.label + " | " + a.status);
        lines.push_back("  CPU demand " + number(a.requested_tflops, 3) + " / limit " +
                        number(a.limit_tflops, 3) + " / allocated " +
                        number(a.allocated_tflops, 3) + " TFLOP/s | priority " +
                        std::to_string(a.priority));
        lines.push_back("  RAM used " + number(a.ram_used_mb, 0) + " / budget " +
                        number(a.ram_mb, 0) + " MB | minimum " + number(a.minimum_ram_mb, 0) +
                        " / recommended " + number(a.recommended_ram_mb, 0) + " MB | storage " +
                        number(a.storage_used_mb, 0) + " / " + number(a.storage_mb, 0) +
                        " MB installed");
        lines.push_back("  Task: " + a.task);
        if (!a.bottleneck.empty())
            lines.push_back("  BOTTLENECK: " + a.bottleneck);
        if (const auto* r = people_["upgrades"]["resource_requests"][a.id].as_table();
            r && (*r)["active"].value_or(false))
            lines.push_back("  REQUEST: " + (*r)["advice"].value_or(std::string{}));
        for (const auto& p : computer_.processes())
            if (p.id == "agent-" + a.id) {
                const double last = people_["upgrades"]["agent_work"][a.id].value_or(0.0),
                             cadence =
                                 people_["upgrades"]["resources"]["action_cpu_seconds"].value_or(
                                     1.0);
                lines.push_back(
                    "  Next work cycle: " +
                    number(std::clamp((p.consumed_cpu_seconds - last) / cadence * 100, 0.0, 100.0),
                           0) +
                    "% | CPU work completed " + number(p.consumed_cpu_seconds, 1) +
                    " core-seconds");
            }
    }
    const int visible = std::max(1, bottom - top - 1);
    if (agent_reveal_ && !headings.empty()) {
        const auto row = headings[agent_selected_];
        if (row < ai_offset_ || row >= ai_offset_ + visible - 5)
            ai_offset_ = row;
        agent_reveal_ = false;
    }
    ai_offset_ = std::clamp(ai_offset_, 0, std::max(0, static_cast<int>(lines.size()) - visible));
    for (int i = 0; i < visible && ai_offset_ + i < static_cast<int>(lines.size()); ++i)
        write(top + 1 + i, 1, lines[ai_offset_ + i].substr(0, std::max(1, width - 2)),
              lines[ai_offset_ + i].starts_with("> ") ? ActiveTab : Plain);
    write(bottom, 1, "Up/Down: select agent | PageUp/PageDown: scroll | Enter: agent controls",
          Footer);
}
void TerminalUI::open_agent_actions(const std::string& id) {
    const auto a = selected(AgentSystem::statuses(people_, computer_, file_system_, network_), id);
    open_menu(
        "Agent | " + a.label + " | " + a.status,
        {{a.deployed ? "Resume agent" : "Deploy agent",
          [this, id, a] {
              open_menu("Confirm deployment | " + a.label,
                        {{"Deploy / resume on this computer",
                          [this, id] {
                              for (const auto& message : AgentSystem::deploy(
                                       people_, computer_, file_system_, network_, id))
                                  add_log(message);
                              add_log("Deployment requested; shortage requests appear in the agent "
                                      "dashboard.");
                              open_agent_actions(id);
                          },
                          {},
                          "CPU demand shares resources with research and other processes. "
                          "Shortages leave the agent waiting. " +
                              a.bottleneck},
                         {"Cancel",
                          [this, id] {
                              open_agent_actions(id);
                          },
                          {}}},
                        [this, id] {
                            open_agent_actions(id);
                        });
          },
          a.running ? "Agent is already running"
          : a.built ? ""
                    : "Research this agent first",
          "Minimum RAM " + number(a.minimum_ram_mb, 0) + " MB; budget " + number(a.ram_mb, 0) +
              " MB; storage " + number(a.storage_mb, 0) + " MB. " + a.bottleneck},
         {"Pause agent",
          [this, id] {
              AgentSystem::pause(people_, computer_, id);
              open_agent_actions(id);
          },
          a.built ? "" : "Not researched", "Releases CPU and RAM; keeps installation storage."},
         {"Stop agent",
          [this, id] {
              AgentSystem::pause(people_, computer_, id, true);
              open_agent_actions(id);
          },
          a.built ? "" : "Not researched"},
         {"Resource budgets and priority",
          [this, id] {
              open_agent_resources(id);
          },
          {}},
         {"Repair installation",
          [this, id] {
              AgentSystem::repair(people_, computer_, file_system_, network_, id);
              add_log("Agent installation repaired.", NoticeKind::Success);
              open_agent_actions(id);
          },
          a.deployed ? "" : "Deploy the agent first",
          "Restores the required storage allocation without discarding task history."},
         {"Resource request",
          [this, id] {
              const auto* r = people_["upgrades"]["resource_requests"][id].as_table();
              show_message("Resource request",
                           r ? (*r)["reason"].value_or(std::string{"No request"}) + ". " +
                                   (*r)["advice"].value_or(std::string{})
                             : "No outstanding resource request.");
              menu_back_ = [this, id] {
                  open_agent_actions(id);
              };
          },
          {}},
         {"Open Processes",
          [this] {
              selected_tab_ = Tab::Processes;
          },
          tab_available(Tab::Processes) ? "" : "Research Task Manager first"},
         {"Research upgrades",
          [this] {
              open_upgrades();
          },
          {}}});
}
void TerminalUI::open_agent_resources(const std::string& id) {
    const auto a = selected(AgentSystem::statuses(people_, computer_, file_system_, network_), id);
    std::vector<MenuAction> actions;
    for (const auto& field : {std::string{"CPU limit (TFLOP/s)"}, std::string{"RAM budget (MB)"},
                              std::string{"Priority (1-5)"}})
        actions.push_back(
            {field,
             [this, id, a, field] {
                 const auto initial = field.starts_with("CPU")   ? number(a.limit_tflops, 3)
                                      : field.starts_with("RAM") ? number(a.ram_mb, 0)
                                                                 : std::to_string(a.priority);
                 open_text(
                     field,
                     "CPU 0 waits; RAM below minimum waits; higher priority gets a larger CPU "
                     "share.",
                     [this, id, a, field](const std::string& value) {
                         std::size_t parsed = 0;
                         const double entered = std::stod(value, &parsed);
                         if (parsed != value.size() || !std::isfinite(entered))
                             throw std::runtime_error("Enter a finite number");
                         if (!field.starts_with("CPU") &&
                             (entered < 0 || entered > 1000000 || std::floor(entered) != entered))
                             throw std::runtime_error("Enter a whole number in range");
                         AgentSystem::configure(
                             people_, computer_, id,
                             field.starts_with("CPU") ? entered : a.limit_tflops,
                             field.starts_with("RAM") ? static_cast<std::int64_t>(entered)
                                                      : static_cast<std::int64_t>(a.ram_mb),
                             field.starts_with("Priority") ? static_cast<int>(entered)
                                                           : a.priority);
                         add_log("Agent resource budget saved.", NoticeKind::Success);
                         open_agent_resources(id);
                     },
                     initial,
                     [this, id] {
                         open_agent_resources(id);
                     });
             },
             {},
             field.starts_with("RAM")
                 ? "Minimum " + number(a.minimum_ram_mb, 0) + " MB; recommended " +
                       number(a.recommended_ram_mb, 0) + " MB."
                 : "Limits and priorities persist in saves."});
    open_menu("Resources | " + a.label, std::move(actions), [this, id] {
        open_agent_actions(id);
    });
}
} // namespace ai_sim::ui
