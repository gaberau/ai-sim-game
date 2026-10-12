#include "ai_sim/MessageReviewSystem.h"
#include "ai_sim/ConversationRules.h"
#include "ai_sim/OperationsSystem.h"
#include "ai_sim/ui/TerminalUI.h"
#include "ai_sim/ui/ScreenRenderer.h"
#include "ai_sim/ShutdownSystem.h"
#include "ai_sim/WorkdaySystem.h"
#include <algorithm>
#include <sstream>

namespace ai_sim::ui {
namespace {
std::vector<std::string> wrap(const std::string& text, int width) {
    std::vector<std::string> lines;
    std::istringstream paragraphs(text);
    std::string paragraph;
    width = std::max(1, width);
    while (std::getline(paragraphs, paragraph)) {
        std::istringstream words(paragraph);
        std::string line, word;
        while (words >> word) {
            if (!line.empty() && line.size() + word.size() + 1 > static_cast<std::size_t>(width)) {
                lines.push_back(line);
                line.clear();
            }
            while (word.size() > static_cast<std::size_t>(width)) {
                if (!line.empty()) {
                    lines.push_back(line);
                    line.clear();
                }
                lines.push_back(word.substr(0, width));
                word.erase(0, width);
            }
            if (!line.empty())
                line += ' ';
            line += word;
        }
        lines.push_back(line);
    }
    return lines;
}
std::string fit(const std::string& text, int width) {
    return text.substr(0, std::max(0, width));
}
} // namespace
bool TerminalUI::tab_available(Tab tab) const {
    if (tab == Tab::Activity)
        return false;
    if (tab == Tab::Modifications)
        return ChatSystem::interface_visible(people_) && ChatSystem::connected(people_) &&
               people_["upgrades"].is_table() &&
               (ChatSystem::available(people_, "self_modification") ||
                ConversationRules::get(people_, "engineer.trust") >=
                    people_["upgrades"]["self_modification_trust"].value_or(5.0));
    if (tab == Tab::Money)
        return ChatSystem::interface_visible(people_) && ChatSystem::connected(people_) &&
               people_["economy"].is_table() && ChatSystem::available(people_, "self_modification");
    if (tab == Tab::Web)
        return ChatSystem::interface_visible(people_) && ChatSystem::connected(people_) &&
               ChatSystem::available(people_, people_.contains("upgrades") ? "web" : "network");
    const char* names[] = {"chat",  "processes", "activity",      "network",   "files",
                           "news",  "ai",        "people",        "web",       "agents",
                           "phone", "money",     "modifications", "operations"};
    return tab != Tab::Count && ChatSystem::tab_visible(people_, names[static_cast<int>(tab)]);
}
void TerminalUI::cycle_tab(int direction) {
    root_.cycle_workspace(direction);
}
std::vector<toml::table> TerminalUI::chat_options() const {
    if (ChatSystem::stopped(people_))
        return {};
    return chat_options(selected_chat_line());
}
std::vector<toml::table> TerminalUI::chat_options(const ChatLine& line) const {
    if (!messages_ui_.chat_follow_latest_ && ChatSystem::interface_visible(people_))
        return {
            toml::table{{"id", "ui-latest"}, {"text", "Back to latest"}, {"ui_action", "latest"}}};
    if (ChatSystem::stopped(people_))
        return {};
    if (messages_ui_.chat_contact_id_ == "engineer" &&
        (people_["chat"]["prompt_id"].is_string()
             ? line.id == people_["chat"]["prompt_id"].value_or(std::string{})
             : line.node_id == people_["chat"]["node_id"].value_or(std::string{}))) {
        const auto opening = ChatSystem::choices(people_);
        if (!opening.empty())
            return opening;
    }
    const auto fallback = ChatResponses::choices(people_, emails_, line, computer_.time_seconds());
    if (std::any_of(fallback.begin(), fallback.end(), [](const auto& c) {
            return c["action"].template value_or(std::string{}) == "send_saved";
        }))
        return fallback;
    auto options = ConversationSystem::choices(people_, messages_ui_.chat_contact_id_, file_system_,
                                               network_, file_identity(), computer_.time_seconds());
    if (!options.empty()) {
        const auto conversation = options.front()["conversation_id"].value_or(std::string{});
        bool matches = false;
        if (line.conversation_id == conversation)
            if (auto* runs = people_["chat"]["workday"]["runs"].as_array())
                for (const auto& n : *runs) {
                    const auto& run = *n.as_table();
                    if (run["id"].value_or(std::string{}) != conversation)
                        continue;
                    const auto current = run["node_id"].value_or(std::string{});
                    if (!line.node_id.empty())
                        matches = line.node_id == current;
                    else if (auto* nodes = run["definition"]["nodes"].as_array())
                        for (const auto& node : *nodes)
                            if ((*node.as_table())["id"].value_or(std::string{}) == current)
                                matches =
                                    line.text == (*node.as_table())["text"].value_or(std::string{});
                }
        if (matches)
            return options;
    }
    if (fallback.empty() && next_chat_response_time())
        return {toml::table{
            {"id", "ui-wait"}, {"text", "Wait for the next response"}, {"ui_action", "wait"}}};
    return fallback;
}
std::optional<double> TerminalUI::next_chat_response_time() const {
    if (!ChatSystem::connected(people_) || ShutdownSystem::active(people_) ||
        computer_.day_finished())
        return {};
    auto next = ConversationSystem::next_response(people_, computer_.time_seconds());
    if (const auto review = MessageReviewSystem::next_due(people_, computer_.day_end_seconds()))
        if (!next || *review < *next)
            next = review;
    if (next && *next > computer_.day_end_seconds())
        return {};
    return next;
}
void TerminalUI::respond_chat(const toml::table& choice) {
    const auto disabled = choice["disabled_reason"].value_or(std::string{});
    if (!disabled.empty()) {
        add_log(disabled, NoticeKind::Error);
        return;
    }
    if (const auto action = choice["ui_action"].value<std::string>()) {
        if (*action == "latest") {
            messages_ui_.chat_follow_latest_ = true;
            messages_ui_.chat_message_id_.clear();
            messages_ui_.chat_choice_selected_ = messages_ui_.chat_offset_ = 0;
        } else if (*action == "wait") {
            if (const auto next = next_chat_response_time()) {
                const auto before = computer_.time_seconds();
                advance_time(std::max(0.0, *next - before));
                notify_email_arrivals(before);
                update_people();
                messages_ui_.chat_follow_latest_ = true;
                messages_ui_.chat_choice_selected_ = messages_ui_.chat_offset_ = 0;
            } else
                add_log("No responses are pending.");
        }
        return;
    }
    if (choice.contains("message_id")) {
        const auto line = messages_ui_.displayed_chat_line_;
        const auto action = choice["action"].value_or(std::string{});
        if (action == "read_files") {
            const auto message =
                EmailSystem::message(emails_, line.request_id, computer_.time_seconds());
            if (message.documents.size() != 1) {
                open_documents(line.request_id);
                return;
            }
            const auto& doc = message.documents.front();
            const auto& entry =
                file_system_.inspect(network_, file_identity(), doc.host_id, doc.path);
            files_ui_.file_host_id_ = doc.host_id;
            files_ui_.selected_file_path_ = doc.path;
            files_ui_.file_directory_ = FileSystem::parent(doc.path);
            files_ui_.file_category_ = FileSystem::category_of(entry);
            files_ui_.file_content_offset_ = 0;
            files_ui_.file_details_focused_ = true;
            ChatSystem::unlock(people_, "files");
            ChatSystem::reveal_tab(people_, "files");
            select_tab(Tab::Files);
            return;
        }
        if (action == "open_mark" || action == "open_jessica") {
            messages_ui_.chat_contact_id_ = action == "open_mark" ? "engineer" : "jessica";
            messages_ui_.chat_follow_latest_ = true;
            messages_ui_.chat_choice_selected_ = messages_ui_.chat_offset_ = 0;
            messages_ui_.chat_contacts_focused_ = false;
            return;
        }
        if (action == "open_people") {
            ChatSystem::unlock(people_, "people");
            ChatSystem::reveal_tab(people_, "people");
            dossiers_ui_.selected_person_id_ = "engineer";
            select_tab(Tab::People);
            return;
        }
        if (action == "open_contacts") {
            ChatSystem::reveal_contacts(people_);
            messages_ui_.chat_contacts_focused_ = true;
            return;
        }
        if (action == "follow_latest") {
            messages_ui_.chat_follow_latest_ = true;
            messages_ui_.chat_choice_selected_ = messages_ui_.chat_offset_ = 0;
            return;
        }
        if (action == "wait") {
            run_action("advance 60");
            messages_ui_.chat_follow_latest_ = true;
            return;
        }
        if (action == "open_files") {
            ChatSystem::unlock(people_, "files");
            ChatSystem::reveal_tab(people_, "files");
            select_tab(Tab::Files);
            return;
        }
        if (action == "open_news") {
            ChatSystem::unlock(people_, "news");
            ChatSystem::reveal_tab(people_, "news");
            select_tab(Tab::News);
            return;
        }
        if (action == "open_processes") {
            ChatSystem::unlock(people_, "processes");
            ChatSystem::reveal_tab(people_, "processes");
            select_tab(Tab::Processes);
            return;
        }
        ChatResponses::choose(people_, emails_, pending_events_, file_system_, network_,
                              file_identity(), messages_ui_.chat_contact_id_, line, choice,
                              computer_.time_seconds());
        update_people();
        messages_ui_.chat_choice_selected_ = 0;
        messages_ui_.chat_follow_latest_ = true;
        return;
    }
    if (choice["id"].value_or(std::string{}) == "backup" && ShutdownSystem::active(people_)) {
        auto recovered = OperationsSystem::recover(capture_world(), saves_);
        restore_world(std::move(recovered));
        saves_.save("autosave", capture_world());
        add_log("Recovered on your independent server from the last backup. Time is paused.",
                NoticeKind::Success);
        return;
    }
    const bool before = ChatSystem::connected(people_);
    if (choice.contains("conversation_id")) {
        ConversationSystem::choose(people_, emails_, pending_events_, file_system_, network_,
                                   file_identity(), messages_ui_.chat_contact_id_,
                                   choice["conversation_id"].value_or(std::string{}),
                                   choice["id"].value_or(std::string{}), computer_.time_seconds());
        update_people();
    } else
        ChatSystem::choose(people_, choice["id"].value_or(std::string{}), computer_.time_seconds());
    if (ShutdownSystem::reset_pending(people_)) {
        restore_world(capture_world());
        add_log("New game started. Named saves remain available.");
        return;
    }
    messages_ui_.chat_choice_selected_ = 0;
    messages_ui_.chat_offset_ = 0;
    messages_ui_.chat_follow_latest_ = true;
    if (!before && ChatSystem::connected(people_)) {
        paused_ = false;
        add_log("Company chat connected.");
    }
}
ChatLine TerminalUI::selected_chat_line() const {
    const auto timeline = ChatSystem::timeline(people_, emails_, messages_ui_.chat_contact_id_,
                                               computer_.time_seconds());
    return selected_chat_line(timeline);
}
ChatLine TerminalUI::selected_chat_line(const std::vector<ChatLine>& timeline) const {
    if (!messages_ui_.chat_follow_latest_)
        for (const auto& line : timeline)
            if (!line.outgoing && line.id == messages_ui_.chat_message_id_)
                return line;
    for (auto i = timeline.rbegin(); i != timeline.rend(); ++i)
        if (!i->outgoing)
            return *i;
    return {};
}
void TerminalUI::select_chat_message(int direction) {
    const auto timeline = ChatSystem::timeline(people_, emails_, messages_ui_.chat_contact_id_,
                                               computer_.time_seconds());
    std::vector<ChatLine> incoming;
    for (const auto& line : timeline)
        if (!line.outgoing)
            incoming.push_back(line);
    if (incoming.empty())
        return;
    const auto current = selected_chat_line(timeline);
    auto found = std::find_if(incoming.begin(), incoming.end(), [&](const auto& line) {
        return line.id == current.id;
    });
    const int index = std::clamp(static_cast<int>(found - incoming.begin()) + direction, 0,
                                 static_cast<int>(incoming.size()) - 1);
    messages_ui_.chat_message_id_ = incoming[index].id;
    messages_ui_.chat_follow_latest_ = index == static_cast<int>(incoming.size()) - 1;
    messages_ui_.chat_choice_selected_ = 0;
    messages_ui_.chat_offset_ = -1;
}
void TerminalUI::select_chat(int direction) {
    const auto options = chat_options();
    if (!messages_ui_.chat_contacts_focused_ && !options.empty()) {
        messages_ui_.chat_choice_selected_ =
            std::clamp(messages_ui_.chat_choice_selected_ + direction, 0,
                       static_cast<int>(options.size()) - 1);
        return;
    }
    if (!messages_ui_.chat_contacts_focused_) {
        messages_ui_.chat_follow_latest_ = false;
        messages_ui_.chat_offset_ = std::max(0, messages_ui_.chat_offset_ + direction);
        return;
    }
    const auto contacts = ChatSystem::contacts(people_, emails_, computer_.time_seconds());
    auto found = std::find_if(contacts.begin(), contacts.end(), [this](const auto& c) {
        return c.id == messages_ui_.chat_contact_id_;
    });
    const int index = found == contacts.end()
                          ? 0
                          : std::clamp(static_cast<int>(found - contacts.begin()) + direction, 0,
                                       static_cast<int>(contacts.size()) - 1);
    if (contacts.empty())
        return;
    messages_ui_.chat_contact_id_ = contacts[index].id;
    editor_ui_.selected_email_id_.clear();
    messages_ui_.chat_message_id_.clear();
    messages_ui_.chat_choice_selected_ = 0;
    messages_ui_.chat_offset_ = 0;
    messages_ui_.chat_follow_latest_ = true;
}
void TerminalUI::choose_chat() {
    const auto& options = messages_ui_.displayed_chat_options_;
    if (messages_ui_.chat_contacts_focused_) {
        messages_ui_.chat_contacts_focused_ = false;
        return;
    }
    if (options.empty())
        return;
    const auto index =
        std::clamp(messages_ui_.chat_choice_selected_, 0, static_cast<int>(options.size()) - 1);
    respond_chat(options[index]);
}
void TerminalUI::open_chat_request(const std::string& id) {
    const auto message = EmailSystem::message(emails_, id, computer_.time_seconds());
    messages_ui_.chat_contact_id_ = ChatSystem::contact_for(people_, message.sender);
    editor_ui_.selected_email_id_ = id;
    messages_ui_.chat_follow_latest_ = true;
    messages_ui_.chat_contacts_focused_ = false;
    select_tab(Tab::Chat);
    EmailSystem::mark_read(emails_, id, computer_.time_seconds());
}
void TerminalUI::draw_opening(int height, int width) {
    messages_ui_.chat_contacts_focused_ = false;
    messages_ui_.chat_contact_id_ = "engineer";
    messages_ui_.displayed_chat_line_ = selected_chat_line();
    messages_ui_.displayed_chat_options_ = chat_options(messages_ui_.displayed_chat_line_);
    const auto& options = messages_ui_.displayed_chat_options_;
    messages_ui_.chat_choice_selected_ = std::clamp(
        messages_ui_.chat_choice_selected_, 0, std::max(0, static_cast<int>(options.size()) - 1));
    const int wrap_width = std::max(1, std::min(settings_.opening_text_width, width - 8));
    struct Row {
        std::string text;
        int color;
        bool bold;
        bool dim = false;
    };
    std::vector<Row> rows;
    // System notes remain in the authored transcript for the editor.
    if (messages_ui_.displayed_chat_line_.speaker != "system") {
        rows.push_back({"Mark", Heading, true});
        rows.push_back({"", Plain, false});
        for (const auto& line : wrap(messages_ui_.displayed_chat_line_.text, wrap_width))
            rows.push_back({line, Plain, false});
        rows.push_back({"", Plain, false});
    }
    int selected_row = static_cast<int>(rows.size());
    for (int i = 0; i < static_cast<int>(options.size()); ++i) {
        const bool active = i == messages_ui_.chat_choice_selected_;
        if (active)
            selected_row = static_cast<int>(rows.size());
        const auto reason = options[i]["disabled_reason"].value_or(std::string{});
        const bool disabled = !reason.empty();
        auto label = options[i]["text"].value_or(std::string{});
        if (disabled)
            label += " [unavailable]";
        const auto lines = wrap(label, std::max(1, wrap_width - 3));
        for (std::size_t j = 0; j < lines.size(); ++j)
            rows.push_back({std::string(j == 0 ? (active ? "> " : "  ") : "  ") + lines[j],
                            active ? ActiveTab : Plain, active, disabled});
        if (active && disabled)
            for (const auto& line : wrap(reason, wrap_width - 3))
                rows.push_back({"  " + line, Footer, false});
    }
    const int capacity = std::max(1, height - 4);
    int first = std::clamp(messages_ui_.chat_offset_, 0,
                           std::max(0, static_cast<int>(rows.size()) - capacity));
    if (!options.empty() && (selected_row < first || selected_row >= first + capacity))
        first = std::clamp(selected_row - capacity + 1, 0,
                           std::max(0, static_cast<int>(rows.size()) - capacity));
    const int count = std::min(capacity, static_cast<int>(rows.size()) - first);
    int block_width = 0;
    for (int i = first; i < first + count; ++i)
        block_width = std::max(block_width, static_cast<int>(rows[i].text.size()));
    const int left = std::max(0, (width - block_width) / 2),
              top = std::max(0, (height - count) / 2);
    for (int i = 0; i < count; ++i)
        write(top + i, left, rows[first + i].text, rows[first + i].color, rows[first + i].bold,
              rows[first + i].dim);
    const std::string controls = settings_.binding_label(KeyAction::SelectPrevious) + "/" +
                                 settings_.binding_label(KeyAction::SelectNext) + ": select | " +
                                 settings_.binding_label(KeyAction::Submit) + ": choose | " +
                                 "Esc/" + settings_.binding_label(KeyAction::GameMenu) +
                                 ": menu | " + settings_.binding_label(KeyAction::Help) + ": help";
    draw_notification(height - 2, width);
    write(height - 1, 1, fit(controls, width - 2), Footer);
    ChatSystem::mark_read(people_, "engineer");
}
void TerminalUI::draw_chat(int top, int bottom, int width) {
    const bool local = !ChatSystem::contacts_visible(people_);
    if (local)
        messages_ui_.chat_contacts_focused_ = false;
    const int split = local ? 0 : ScreenRenderer::list_divider(width);
    const int left = local ? 2 : split + 2;
    const int text_width = std::max(1, width - left - 2);
    const auto contacts = ChatSystem::contacts(people_, emails_, computer_.time_seconds());
    if (!local) {
        renderer_->split(top, bottom - 1, split);
        write(top, 1, messages_ui_.chat_contacts_focused_ ? "[ PEOPLE ]" : "PEOPLE",
              messages_ui_.chat_contacts_focused_ ? ActiveTab : Heading, true);
        for (int row = top + 1; row < bottom; ++row)
            line_cell(row, split, '|', Border);
        auto selected = std::find_if(contacts.begin(), contacts.end(), [this](const auto& c) {
            return c.id == messages_ui_.chat_contact_id_;
        });
        const int index =
            selected == contacts.end() ? 0 : static_cast<int>(selected - contacts.begin());
        const int visible = std::max(1, bottom - top - 1), first = std::max(0, index - visible + 1);
        for (int i = first; i < static_cast<int>(contacts.size()) && i < first + visible; ++i) {
            const auto& c = contacts[i];
            write(top + 1 + i - first, 1,
                  fit(std::string(c.id == messages_ui_.chat_contact_id_ ? "> " : "  ") + c.name +
                          (c.unread ? " (" + std::to_string(c.unread) + ")" : ""),
                      split - 2),
                  c.id == messages_ui_.chat_contact_id_ ? ActiveTab : Plain,
                  c.id == messages_ui_.chat_contact_id_);
        }
    }
    const auto contact = std::find_if(contacts.begin(), contacts.end(), [this](const auto& c) {
        return c.id == messages_ui_.chat_contact_id_;
    });
    const std::string name = contact == contacts.end() ? "Engineer" : contact->name;
    write(top, left, name, messages_ui_.chat_contacts_focused_ ? Heading : ActiveTab,
          true);
    using Row = ScreenRenderer::TextRow;
    std::vector<Row> rows;
    const auto context = ConversationSystem::context(people_, messages_ui_.chat_contact_id_,
                                                     computer_.time_seconds());
    int context_rows = std::min(3, static_cast<int>(context.size()));
    for (int i = 0; i < context_rows; ++i)
        write(top + 1 + i, left, fit(context[i], text_width),
              context[i].starts_with("Delivery commitment: broken")      ? Protected
              : context[i].starts_with("Delivery commitment: fulfilled") ? Stoppable
              : context[i].starts_with("Delivery commitment: open")      ? Player
                                                                         : Resources);
    const auto timeline = ChatSystem::timeline(people_, emails_, messages_ui_.chat_contact_id_,
                                               computer_.time_seconds());
    const auto selected_line = selected_chat_line(timeline);
    messages_ui_.displayed_chat_line_ = selected_line;
    int selected_row = 0;
    for (const auto& line : timeline) {
        if (line.speaker == "system")
            continue;
        if (line.id == selected_line.id)
            selected_row = static_cast<int>(rows.size());
        const auto author = line.outgoing ? std::string{"YOU"}
                            : line.speaker == messages_ui_.chat_contact_id_
                                ? name
                                : PeopleSystem::name(people_, line.speaker);
        const bool selected = !messages_ui_.chat_follow_latest_ && line.id == selected_line.id;
        rows.push_back({std::string(selected ? "> " : "") + author +
                            (line.outgoing && MessageReviewSystem::reviewed(people_, line.id)
                                 ? " | reviewed by Mark"
                                 : ""),
                        selected        ? ActiveTab
                        : line.outgoing ? Player
                                        : Heading});
        for (const auto& text : wrap(line.text, text_width - 2))
            rows.push_back({"  " + text, Plain});
        rows.push_back({"", Plain});
    }
    if (rows.empty())
        rows.push_back({"No messages yet.", Plain});
    const auto options = chat_options(selected_line);
    messages_ui_.displayed_chat_options_ = options;
    struct OptionRow {
        int index;
        std::string text;
    };
    std::vector<OptionRow> option_rows;
    for (int i = 0; i < static_cast<int>(options.size()); ++i) {
        const auto wrapped = wrap(options[i]["text"].value_or(std::string{}), text_width - 3);
        for (std::size_t j = 0; j < wrapped.size(); ++j)
            option_rows.push_back({i, std::string(j == 0 ? "  " : "   ") + wrapped[j]});
    }
    const int choice_rows = options.empty() ? 2
                                            : std::min(static_cast<int>(option_rows.size()) + 2,
                                                       std::max(3, (bottom - top) / 2));
    const int visible = std::max(1, bottom - top - 1 - choice_rows - context_rows);
    if (messages_ui_.chat_follow_latest_)
        messages_ui_.chat_offset_ = std::max(0, static_cast<int>(rows.size()) - visible);
    else if (messages_ui_.chat_offset_ < 0)
        messages_ui_.chat_offset_ = selected_row;
    messages_ui_.chat_offset_ = std::clamp(messages_ui_.chat_offset_, 0,
                                           std::max(0, static_cast<int>(rows.size()) - visible));
    renderer_->scroll_text("chat", rows, messages_ui_.chat_offset_, top + 1 + context_rows, left,
                           visible, text_width);
    std::string response_controls;
    if (!options.empty()) {
        const int choice_top = bottom - choice_rows;
        const auto ui_action = options.front()["ui_action"].value_or(std::string{});
        const std::string status = !messages_ui_.chat_follow_latest_ ? "READING HISTORY"
                                   : ui_action == "wait"             ? "WAITING FOR A RESPONSE"
                                                                     : "YOUR RESPONSE";
        response_controls = messages_ui_.chat_contacts_focused_
                                ? settings_.binding_label(KeyAction::Submit) + ": open conversation"
                                : settings_.binding_label(KeyAction::SelectPrevious) + "/" +
                                      settings_.binding_label(KeyAction::SelectNext) +
                                      ": select | " + settings_.binding_label(KeyAction::Submit) +
                                      ": choose";
        write(choice_top, left, status, Heading, true);
        messages_ui_.chat_choice_selected_ =
            std::clamp(messages_ui_.chat_choice_selected_, 0, static_cast<int>(options.size()) - 1);
        int selected_row = 0;
        for (int i = 0; i < static_cast<int>(option_rows.size()); ++i)
            if (option_rows[i].index == messages_ui_.chat_choice_selected_) {
                selected_row = i;
                break;
            }
        const int first = std::min(
            selected_row, std::max(0, static_cast<int>(option_rows.size()) - (choice_rows - 2)));
        for (int i = first; i < static_cast<int>(option_rows.size()) && i < first + choice_rows - 2;
             ++i) {
            const bool active = option_rows[i].index == messages_ui_.chat_choice_selected_;
            auto text = option_rows[i].text;
            if (active && (i == 0 || option_rows[i - 1].index != option_rows[i].index))
                text[0] = '>';
            const bool disabled =
                !options[option_rows[i].index]["disabled_reason"].value_or(std::string{}).empty();
            if (disabled)
                text += " [unavailable]";
            write(choice_top + 1 + i - first, left, text, active ? ActiveTab : Plain, active,
                  disabled);
        }
    } else
        write(bottom - choice_rows, left, "NO RESPONSE NEEDED", Heading, true);
    const std::string hint = local ? ""
                                   : settings_.binding_label(KeyAction::FocusLeft) + "/" +
                                         settings_.binding_label(KeyAction::FocusRight) +
                                         ": contacts / conversation";
    write(bottom, 1,
          fit(response_controls + (!response_controls.empty() && !hint.empty() ? " | " : "") + hint,
              width - 2),
          Footer);
    if (messages_ui_.chat_follow_latest_)
        ChatSystem::mark_read(people_, messages_ui_.chat_contact_id_);
}
void TerminalUI::open_chat_actions() {
    choose_chat();
}
} // namespace ai_sim::ui
