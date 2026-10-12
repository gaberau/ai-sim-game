// Module: Render agent shortages and apply inline deployment/resource controls to the selected
// agent.
#include "ai_sim/ui/TerminalUI.h"
#include "ai_sim/ui/ScreenRenderer.h"
#include "ai_sim/AgentSystem.h"
#include <algorithm>
#include <numeric>
#include <sstream>
#include <cmath>
#include <stdexcept>
namespace ai_sim::ui {
namespace {
std::string short_status(const std::string& status) {
    if (status == "Running")
        return "Active";
    if (status == "Running slowly")
        return "Slow";
    if (status == "Not researched")
        return "Needs research";
    return status;
}
AgentStatus selected(const std::vector<AgentStatus>& agents, const std::string& id) {
    for (const auto& agent : agents)
        if (agent.id == id)
            return agent;
    throw std::runtime_error("Unknown agent");
}
} // namespace
void TerminalUI::draw_agents(int top, int bottom, int width) {
    const int split = list_divider(width);
    renderer_->split(top, bottom - 1, split);
    for (int row = top; row < bottom; ++row)
        line_cell(row, split, '|', Border);
    const auto allocations = computer_.cpu_allocations();
    const double used = std::accumulate(allocations.begin(), allocations.end(), 0.0);
    const auto& host = network_.hosts()[network_.local_host_index()];
    std::vector<std::string> lines;
    lines.push_back("CPU " + number(used, 2) + " / " + number(computer_.cpu_cores(), 0) +
                    " cores | RAM " + number(computer_.allocated_memory_bytes() / 1000000.0, 0) +
                    " / " + number(computer_.memory_bytes() / 1000000.0, 0) + " MB");
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
                    "Research: " + pending + " | " +
                    number(total > 0 ? 100 * p.consumed_cpu_seconds / total : 100, 0) + "% | " +
                    (allocations[i] > 0
                         ? "ETA " + number(p.remaining_cpu_seconds / allocations[i], 1) +
                               " working seconds"
                         : "Waiting for CPU"));
            }
        }
    else
        lines.push_back("Research: Idle");
    const auto agents = AgentSystem::statuses(people_, computer_, file_system_, network_);
    agents_ui_.agent_selected_ =
        std::clamp(agents_ui_.agent_selected_, 0, std::max(0, static_cast<int>(agents.size()) - 1));
    const int slots = std::max(1, (bottom - top - 2) / 3);
    const int first = std::max(0, agents_ui_.agent_selected_ - slots + 1);
    for (int i = first; i < static_cast<int>(agents.size()) && i < first + slots; ++i) {
        const auto& agent = agents[i];
        const bool focused = i == agents_ui_.agent_selected_;
        const int row = top + 2 + (i - first) * 3;
        write(row, settings_.layout.text_margin,
              (std::string(focused ? "> " : "  ") + agent.label).substr(0, split - 3),
              focused ? ActiveTab : Plain, focused);
        write(row + 1, 3, short_status(agent.status).substr(0, split - 4),
              agent.bottleneck.empty() ? Footer : Protected, false, agent.bottleneck.empty());
    }
    if (agents.empty())
        write(top + 2, settings_.layout.text_margin, "No agents available.", Footer);
    else {
        const auto& a = agents[agents_ui_.agent_selected_];
        write(top, split + settings_.layout.pane_padding, a.label, Heading, true);
        lines.push_back("");
        lines.push_back("Status: " + short_status(a.status));
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
    const int visible = std::max(1, bottom - top - 10);
    if (agents_ui_.agent_reveal_) {
        scroll_offset() = 0;
        agents_ui_.agent_reveal_ = false;
    }
    const int detail_width = width - split - settings_.layout.pane_padding - 2;
    std::vector<ScreenRenderer::TextRow> details;
    for (const auto& text : lines) {
        // Wrap at word boundaries so budgets and shortage advice remain readable.
        std::istringstream words(text);
        std::string word, line;
        while (words >> word) {
            if (!line.empty() && static_cast<int>(line.size() + word.size() + 1) > detail_width) {
                details.push_back({line, text.find("BOTTLENECK:") != std::string::npos ||
                                                 text.find("REQUEST:") != std::string::npos
                                             ? Protected
                                             : Plain});
                line.clear();
            }
            if (!line.empty())
                line += ' ';
            line += word;
        }
        details.push_back({line, text.find("BOTTLENECK:") != std::string::npos ||
                                         text.find("REQUEST:") != std::string::npos
                                     ? Protected
                                     : Plain});
    }
    scroll_offset() =
        std::clamp(scroll_offset(), 0, std::max(0, static_cast<int>(details.size()) - visible));
    renderer_->scroll_text("agent-details", details, scroll_offset(), top + 1,
                           split + settings_.layout.pane_padding, visible, detail_width);
    if (!agents.empty()) {
        const auto& a = agents[agents_ui_.agent_selected_];
        const std::string actions[] = {
            a.running    ? settings_.label("agent_pause", "Pause")
            : a.deployed ? settings_.label("agent_resume", "Resume")
                         : settings_.label("agent_deploy", "Deploy"),
            settings_.label("agent_stop", "Stop"),
            settings_.label("agent_cpu_limit", "CPU limit"),
            settings_.label("agent_ram_budget", "RAM budget"),
            settings_.label("agent_priority", "Priority"),
            settings_.label("agent_repair_installation", "Repair installation"),
            settings_.label("agent_processes", "Processes"),
            settings_.label("agent_research_upgrades", "Research upgrades")};
        for (int i = 0; i < 8; ++i)
            write(bottom - 8 + i, split + settings_.layout.pane_padding,
                  (std::string(agents_ui_.controls_focused_ && i == agents_ui_.action_selected_
                                   ? "> "
                                   : "  ") +
                   actions[i]),
                  agents_ui_.controls_focused_ && i == agents_ui_.action_selected_ ? ActiveTab
                                                                                   : Plain,
                  agents_ui_.controls_focused_ && i == agents_ui_.action_selected_);
    }
    write(bottom, settings_.layout.text_margin,
          settings_.binding_label(KeyAction::FocusLeft) + "/" +
              settings_.binding_label(KeyAction::FocusRight) + ": agent / controls | " +
              settings_.binding_label(KeyAction::Submit) + ": " +
              (agents_ui_.controls_focused_ ? "apply" : "deploy / pause"),
          Footer);
}
void TerminalUI::open_agent_actions(const std::string& id) {
    const auto agents = AgentSystem::statuses(people_, computer_, file_system_, network_);
    for (std::size_t i = 0; i < agents.size(); ++i)
        if (agents[i].id == id)
            agents_ui_.agent_selected_ = static_cast<int>(i);
    agents_ui_.controls_focused_ = true;
    menu_ui_.menu_active_ = false;
    select_tab(Tab::Agents);
}
void TerminalUI::run_agent_control() {
    const auto agents = AgentSystem::statuses(people_, computer_, file_system_, network_);
    if (agents.empty()) {
        open_upgrades();
        return;
    }
    const auto& a =
        agents[std::clamp(agents_ui_.agent_selected_, 0, static_cast<int>(agents.size()) - 1)];
    const int action = agents_ui_.controls_focused_ ? agents_ui_.action_selected_ : 0;
    if (action == 0) {
        if (a.running)
            AgentSystem::pause(people_, computer_, a.id);
        else
            for (const auto& message :
                 AgentSystem::deploy(people_, computer_, file_system_, network_, a.id))
                add_log(message);
    } else if (action == 1)
        AgentSystem::pause(people_, computer_, a.id, true);
    else if (action >= 2 && action <= 4) {
        edit_agent_resource(a.id, action - 2);
        return;
    } else if (action == 5)
        AgentSystem::repair(people_, computer_, file_system_, network_, a.id);
    else if (action == 6) {
        select_tab(Tab::Processes);
        return;
    } else if (action == 7) {
        open_upgrades();
        return;
    }
    add_log("Agent updated: " + a.label, NoticeKind::Success);
}
void TerminalUI::edit_agent_resource(const std::string& id, int field) {
    const auto a = selected(AgentSystem::statuses(people_, computer_, file_system_, network_), id);
    const std::string titles[] = {"CPU limit (TFLOP/s)", "RAM budget (MB)", "Priority (1-5)"};
    open_text(
        titles[field], "Set the selected agent's budget. Changes take effect immediately.",
        [this, id, field](const std::string& value) {
            std::size_t parsed = 0;
            const double entered = std::stod(value, &parsed);
            if (parsed != value.size() || !std::isfinite(entered))
                throw std::runtime_error("Enter a finite number");
            if (field != 0 && (entered < 0 || entered > 1000000 || std::floor(entered) != entered))
                throw std::runtime_error("Enter a whole number in range");
            const auto current =
                selected(AgentSystem::statuses(people_, computer_, file_system_, network_), id);
            AgentSystem::configure(people_, computer_, id,
                                   field == 0 ? entered : current.limit_tflops,
                                   field == 1 ? static_cast<std::int64_t>(entered)
                                              : static_cast<std::int64_t>(current.ram_mb),
                                   field == 2 ? static_cast<int>(entered) : current.priority);
            add_log("Agent resource budget saved.", NoticeKind::Success);
        },
        field == 0   ? number(a.limit_tflops, 3)
        : field == 1 ? number(a.ram_mb, 0)
                     : std::to_string(a.priority));
}
} // namespace ai_sim::ui
