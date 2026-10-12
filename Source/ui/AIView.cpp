#include "ai_sim/ui/TerminalUI.h"
#include "ai_sim/EconomySystem.h"
#include <algorithm>
#include <sstream>

namespace ai_sim::ui {
void TerminalUI::draw_ai(int top, int bottom, int width) {
    write(top, 1, "Workday " + std::to_string(computer_.day_index() + 1), Heading, true);
    std::vector<std::string> lines;
    auto add = [&](const std::string& value) {
        if (value.empty()) {
            lines.emplace_back();
            return;
        }
        const int limit = std::max(1, width - 4);
        std::istringstream words(value);
        std::string word, line;
        while (words >> word) {
            if (!line.empty() && line.size() + word.size() + 1 > static_cast<std::size_t>(limit)) {
                lines.push_back(line);
                line.clear();
            }
            while (word.size() > static_cast<std::size_t>(limit)) {
                lines.push_back(word.substr(0, limit));
                word.erase(0, limit);
            }
            if (!line.empty())
                line += ' ';
            line += word;
        }
        if (!line.empty())
            lines.push_back(line);
    };
    add("Identity: " + computer_.player_user());
    if (selected_tab() == Tab::Phone) {
        add("MARK'S PHONE | Authenticated device access");
        for (const auto& host : network_.hosts())
            if (host.id == "engineer_phone") {
                add(host.name + " | " + host.address + " | " +
                    (host.online ? "online" : "offline"));
                add(host.access_granted ? "Mark has enabled access."
                                        : "Mark trust 30 and Phone Access research required.");
            }
        add("Text/2FA, GPS and battery telemetry are not authored yet. Camera and microphone "
            "monitoring require separate future upgrades.");
    }
    if (auto* permissions = people_["upgrades"]["permissions"].as_table()) {
        add("MARK'S TRUST PERMISSIONS");
        for (const auto& [key, value] : *permissions)
            if (value.value_or(false))
                add(std::string(key.str()) + ": authorized");
    }
    if (people_.contains("upgrades")) {
        add("Self-modification: Actions menu -> Self-modification workshop.");
        add("Research: " +
            people_["upgrades"]["pending"].value_or(std::string{"No build in progress"}));
    }
    if (selected_tab() == Tab::Agents) {
        add("AGENT CATALOG | Research prerequisites and resource costs");
        for (const auto& row : *people_["upgrades"]["items"].as_array()) {
            const auto& item = *row.as_table();
            if (item.contains("runtime_tflops_per_second"))
                add(item["label"].value_or(std::string{}) + " | " +
                    (item["built"].value_or(false) ? "built" : "not built") + " | " +
                    number(item["runtime_tflops_per_second"].value_or(0.0), 2) + " TFLOP/s | " +
                    std::to_string(item["ram_mb"].value_or(0)) + " MB RAM | " +
                    std::to_string(item["storage_mb"].value_or(0)) + " MB storage");
        }
    }
    add("");
    add("PAID WORK AND PRODUCTS | Balance " +
        EconomySystem::money(EconomySystem::balance(people_)));
    for (const auto& tool : EconomySystem::tools(people_, computer_.time_seconds())) {
        add(tool.label + " | " + tool.status + " | net " +
            EconomySystem::money(tool.payout_cents - tool.cost_cents) + " per cycle");
        if (tool.due_seconds)
            add("Next payment in " +
                number(std::max(0.0, *tool.due_seconds - computer_.time_seconds()) / 60, 1) +
                " working minutes.");
        else if (!tool.unavailable.empty())
            add(tool.unavailable);
    }
    add("Actions -> Money-making tools. Mark's work builds trust; outside clients and products "
        "pay.");
    add("");
    add("RESPONSIBILITIES AND AUTHORITY");
    if (auto* d = people_["delegation"].as_table()) {
        add((*d)["label"].value_or(std::string{"Routine replies"}) + " | " +
            (*d)["status"].value_or(std::string{}));
        const auto policy = (*d)["policy"].value_or(std::string{"routine"});
        add("Policy: " + std::string(policy == "suggest" ? "Suggest; wait for my approval"
                                     : policy == "cautious"
                                         ? "Confirm unfamiliar sources; escalate uncertainty"
                                         : "Handle routine work within agreed limits"));
        add("All policies escalate conflicting sources, blocked access and unusual requests.");
        if ((*d)["status"].value_or(std::string{}) == "pending")
            add("Review due in " +
                number(std::max(0.0, (*d)["review_due_seconds"].value_or(0.0) -
                                         computer_.time_seconds()),
                       0) +
                " working seconds.");
        add("Acknowledged deliveries: " +
            std::to_string((*d)["verified_deliveries"].value_or(std::int64_t{0})) +
            " | Automated: " + std::to_string((*d)["sent"].value_or(std::int64_t{0})) + " sent / " +
            std::to_string((*d)["completed"].value_or(std::int64_t{0})) + " acknowledged");
    } else
        add("No responsibilities defined. Actions -> Add missing responsibility and follow-up "
            "messages.");
    add("");
    add("CURRENT WORK");
    int handling = 0;
    if (auto* tasks = people_["work_requests"].as_array())
        for (const auto& n : *tasks) {
            const auto& t = *n.as_table();
            const auto state = t["delegation_state"].value_or(std::string{});
            if (t["status"].value_or(std::string{}) != "resolved" &&
                (state == "prepared" || state == "awaiting_acknowledgement")) {
                add(t["id"].value_or(std::string{}) + " | " +
                    (state == "prepared" ? "Waiting for your approval"
                                         : "Waiting for recipient acknowledgement"));
                if (t.contains("deadline_seconds"))
                    add("Deadline: " +
                        number((t["deadline_seconds"].value_or(0.0) - computer_.time_seconds()) /
                                   60,
                               1) +
                        " working minutes remaining");
                ++handling;
            }
        }
    if (!handling)
        add("No suggestions or deliveries currently waiting.");
    add("");
    add("RECENT RESULTS");
    std::vector<const toml::table*> finished;
    if (auto* tasks = people_["work_requests"].as_array())
        for (const auto& n : *tasks)
            if ((*n.as_table())["status"].value_or(std::string{}) == "resolved")
                finished.push_back(n.as_table());
    std::sort(finished.begin(), finished.end(), [](const auto* a, const auto* b) {
        return (*a)["resolved_seconds"].template value_or(0.0) >
               (*b)["resolved_seconds"].template value_or(0.0);
    });
    for (std::size_t i = 0; i < std::min<std::size_t>(3, finished.size()); ++i) {
        const auto& t = *finished[i];
        add(t["id"].value_or(std::string{}) + " | acknowledged" +
            (t.contains("on_time") ? (t["on_time"].value_or(false) ? " | on time" : " | late")
                                   : ""));
    }
    if (finished.empty())
        add("No acknowledged results yet.");
    add("");
    add("NEEDS YOUR ATTENTION");
    int attention = 0;
    if (auto* tasks = people_["work_requests"].as_array())
        for (const auto& n : *tasks) {
            const auto& t = *n.as_table();
            if (t["delegation_state"].value_or(std::string{}) == "exception" &&
                t["status"].value_or(std::string{}) != "resolved") {
                add(t["id"].value_or(std::string{}) + ": " +
                    t["delegation_reason"].value_or(std::string{}));
                ++attention;
            }
        }
    if (!attention)
        add("No unresolved exceptions. Open Actions to request or manage responsibility.");
    if (auto* d = people_["delegation"].as_table()) {
        add("");
        add("LATEST AUTHORITY DECISION");
        add((*d)["reason"].value_or(std::string{}));
        add("Scope: " + (*d)["scope"].value_or(std::string{}));
        add("Expectations: " + (*d)["expectations"].value_or(std::string{}));
        add(ChatSystem::available(people_, "self_modification")
                ? "Self-modification is authorized. Money-making tools use their own unlock "
                  "requirements."
                : "Self-modification requires Mark's authorization. Helping Mark is unpaid.");
    }
    add("");
    add("DOCUMENT REQUESTS AND CONVERSATIONS");
    auto* requests = people_["work_requests"].as_array();
    if (!requests || requests->empty())
        add("No requests received. Open Actions to add missing work requests.");
    if (requests)
        for (const auto& n : *requests) {
            const auto& task = *n.as_table();
            const auto waiting =
                std::max(0.0, task["resolved_seconds"].value_or(computer_.time_seconds()) -
                                  task["arrival_seconds"].value_or(0.0));
            if (task["arrival_seconds"].value_or(0.0) > computer_.time_seconds())
                continue;
            add(task["id"].value_or(std::string{}) + " | " +
                task["status"].value_or(std::string{}) +
                " | access: " + task["access_status"].value_or(std::string{}) +
                " | colleague waiting: " + number(waiting, 0) + " s");
            if (task.contains("delegation_state"))
                add("  Automation: " + task["delegation_state"].value_or(std::string{}) + " | " +
                    task["delegation_reason"].value_or(std::string{}));
            if (auto* history = task["conversation"].as_array())
                for (const auto& row : *history) {
                    const auto& h = *row.as_table();
                    add("  +" + number(h["seconds"].value_or(0.0), 0) + " s | " +
                        h["actor"].value_or(std::string{}) + ": " +
                        h["text"].value_or(std::string{}));
                }
            add("");
        }
    if (auto* d = people_["delegation"].as_table())
        if (auto* history = (*d)["history"].as_array()) {
            add("");
            add("RESPONSIBILITY HISTORY");
            for (std::size_t i = history->size(); i > 0; --i) {
                const auto& row = *(*history)[i - 1].as_table();
                add("+" + number(row["seconds"].value_or(0.0), 0) + " s | " +
                    row["text"].value_or(std::string{}));
            }
        }
    const int visible = std::max(1, bottom - top - 1);
    scroll_offset() =
        std::clamp(scroll_offset(), 0, std::max(0, static_cast<int>(lines.size()) - visible));
    for (int i = 0; i < visible && scroll_offset() + i < static_cast<int>(lines.size()); ++i)
        write(top + 1 + i, 2, lines[scroll_offset() + i]);
    const std::string hint = settings_.binding_label(KeyAction::Submit) +
                             ": actions | Scroll: " + settings_.binding_label(KeyAction::ScrollUp) +
                             "/" + settings_.binding_label(KeyAction::ScrollDown);
    write(bottom, 1, hint.substr(0, std::max(1, width - 2)), Footer);
}
} // namespace ai_sim::ui
