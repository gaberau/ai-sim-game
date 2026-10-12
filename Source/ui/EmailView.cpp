#include "ai_sim/ui/TerminalUI.h"
#include "ai_sim/ui/ScreenRenderer.h"
#include "ai_sim/DelegationSystem.h"
#include "ai_sim/WorkdaySystem.h"
#include <ncurses.h>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace ai_sim::ui {
namespace {
std::vector<std::string> wrap(const std::string& text, int width) {
    std::vector<std::string> lines;
    std::istringstream paragraphs(text);
    std::string paragraph;
    while (std::getline(paragraphs, paragraph)) {
        std::istringstream words(paragraph);
        std::string word, line;
        while (words >> word) {
            if (!line.empty() && line.size() + word.size() + 1 > static_cast<std::size_t>(width)) {
                lines.push_back(line);
                line.clear();
            }
            while (word.size() > static_cast<std::size_t>(width)) {
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
void no_extra(std::istringstream& input) {
    std::string extra;
    if (input >> extra)
        throw std::runtime_error("Unexpected email argument: " + extra);
}
} // namespace

void TerminalUI::notify_email_arrivals(double previous_time) {
    for (const auto& email : EmailSystem::messages(emails_, computer_.time_seconds()))
        if (email.arrival_seconds > previous_time)
            add_log("Incoming chat: " + email.subject + " [" + email.id + "]");
}

std::vector<Email> TerminalUI::filtered_emails() const {
    auto messages = EmailSystem::messages(emails_, computer_.time_seconds());
    std::erase_if(messages, [&](const auto& message) {
        return ChatSystem::contact_for(people_, message.sender) != messages_ui_.chat_contact_id_;
    });
    return messages;
}
void TerminalUI::cycle_email_folder(int direction) {
    editor_ui_.email_folder_ =
        static_cast<EmailFolder>((static_cast<int>(editor_ui_.email_folder_) + direction + 4) % 4);
    editor_ui_.selected_email_id_.clear();
    editor_ui_.email_body_offset_ = 0;
}

Email TerminalUI::selected_email() {
    const auto messages = filtered_emails();
    if (messages.empty())
        throw std::runtime_error("No messages in this conversation.");
    auto found = std::find_if(messages.begin(), messages.end(), [&](const auto& email) {
        return email.id == editor_ui_.selected_email_id_;
    });
    if (found == messages.end()) {
        found = std::prev(messages.end());
        for (auto it = messages.begin(); it != messages.end(); ++it) {
            for (const auto& n : *emails_["emails"].as_array())
                if ((*n.as_table())["id"].value<std::string>() == it->id &&
                    (*n.as_table())["workflow"].is_table()) {
                    bool resolved = false;
                    if (auto* tasks = people_["work_requests"].as_array())
                        for (const auto& t : *tasks)
                            if ((*t.as_table())["id"].value<std::string>() == it->id)
                                resolved =
                                    (*t.as_table())["status"].value_or(std::string{}) == "resolved";
                    if (!resolved) {
                        found = it;
                        break;
                    }
                }
        }
        editor_ui_.selected_email_id_ = found->id;
        editor_ui_.email_body_offset_ = 0;
    }
    return *found;
}

void TerminalUI::select_email(int direction) {
    const auto messages = filtered_emails();
    if (messages.empty())
        return;
    const auto found = std::find_if(messages.begin(), messages.end(), [&](const auto& email) {
        return email.id == editor_ui_.selected_email_id_;
    });
    const int index = found == messages.end()
                          ? 0
                          : std::clamp(static_cast<int>(found - messages.begin()) + direction, 0,
                                       static_cast<int>(messages.size()) - 1);
    editor_ui_.selected_email_id_ = messages[index].id;
    editor_ui_.email_body_offset_ = 0;
    if (editor_ui_.email_folder_ != EmailFolder::Unread && !messages[index].archived)
        EmailSystem::mark_read(emails_, editor_ui_.selected_email_id_, computer_.time_seconds());
}

void TerminalUI::edit_email(const std::string& id) {
    const auto email = EmailSystem::message(emails_, id, computer_.time_seconds());
    if (email.draft.size() > static_cast<std::size_t>(settings_.editor_max_bytes))
        throw std::runtime_error("Saved draft exceeds the UI editor byte limit");
    editor_ui_.selected_email_id_ = id;
    const auto visible = filtered_emails();
    EmailSystem::mark_read(emails_, id, computer_.time_seconds());
    if (editor_ui_.email_folder_ == EmailFolder::Unread ||
        std::none_of(visible.begin(), visible.end(), [&](const auto& candidate) {
            return candidate.id == id;
        }))
        editor_ui_.email_folder_ = email.archived ? EmailFolder::Archived : EmailFolder::Inbox;
    editor_ui_.email_original_offset_ = 0;
    editor_ui_.email_original_focused_ = false;
    if (auto* tasks = people_["work_requests"].as_array())
        for (auto& n : *tasks) {
            auto& task = *n.as_table();
            if (task["id"].value_or(std::string{}) == id &&
                task["status"].value_or(std::string{}) != "resolved" &&
                task["delegation_state"].value_or(std::string{}) != "awaiting_acknowledgement") {
                task.insert_or_assign("delegation_state", "manual");
                task.insert_or_assign(
                    "delegation_reason",
                    "Player chose to compose this reply; automation leaves it to the player.");
            }
        }
    editor_ui_.email_editor_ = true;
    editor_ui_.email_editor_id_ = id;
    editor_ui_.editor_buffer_ = email.draft;
    editor_ui_.editor_cursor_ = 0;
    editor_ui_.editor_offset_ = 0;
    editor_ui_.editor_dirty_ = false;
    editor_ui_.editor_active_ = true;
    select_tab(Tab::Chat);
}

void TerminalUI::email_command(std::istringstream& input) {
    std::string action;
    if (!(input >> action)) {
        select_tab(Tab::Chat);
        return;
    }
    if (action == "inbox" || action == "starred" || action == "unread" || action == "archived") {
        no_extra(input);
        editor_ui_.email_folder_ = action == "inbox"     ? EmailFolder::Inbox
                                   : action == "starred" ? EmailFolder::Starred
                                   : action == "unread"  ? EmailFolder::Unread
                                                         : EmailFolder::Archived;
        editor_ui_.selected_email_id_.clear();
        editor_ui_.email_body_offset_ = 0;
        select_tab(Tab::Chat);
        return;
    }
    if (action == "star" || action == "unstar" || action == "archive" || action == "unarchive" ||
        action == "mark-unread") {
        std::string id;
        if (!(input >> id))
            id = selected_email().id;
        no_extra(input);
        const std::string flag = action == "star" || action == "unstar" ? "starred"
                                 : action == "mark-unread"              ? "read"
                                                                        : "archived";
        const bool value = action == "star" || action == "archive";
        const auto target = EmailSystem::message(emails_, id, computer_.time_seconds());
        EmailSystem::set_flag(emails_, id, flag, value, computer_.time_seconds());
        // Mark-unread switches to the Unread preview so rendering cannot undo it.
        if (action == "mark-unread")
            editor_ui_.email_folder_ =
                target.archived ? EmailFolder::Archived : EmailFolder::Unread;
        messages_ui_.chat_contact_id_ = ChatSystem::contact_for(people_, target.sender);
        messages_ui_.chat_follow_latest_ = true;
        messages_ui_.chat_choice_selected_ = messages_ui_.chat_offset_ = 0;
        messages_ui_.chat_contacts_focused_ = false;
        editor_ui_.selected_email_id_ = id;
        editor_ui_.email_body_offset_ = 0;
        select_tab(Tab::Chat);
        add_log("Chat " + id + ": " + action);
        return;
    }
    if (action == "request-access" || action == "blocked") {
        std::string id;
        if (!(input >> id))
            id = selected_email().id;
        no_extra(input);
        const auto message = EmailSystem::message(emails_, id, computer_.time_seconds());
        if (action == "request-access") {
            WorkdaySystem::request_access(people_, emails_, pending_events_, file_system_, network_,
                                          file_identity(), id, computer_.time_seconds());
            add_log(
                "Access request queued for the engineer; open the AI tab for the response queue.");
        } else {
            if (!message.draft.empty() || message.attachment_count)
                throw std::runtime_error(
                    "Send or clear the existing draft before sending a blocked update.");
            if (message.documents.empty())
                throw std::runtime_error("No referenced document to report blocked");
            bool denied = false;
            try {
                (void)file_system_.read(network_, file_identity(), message.documents[0].host_id,
                                        message.documents[0].path);
            } catch (const std::exception& error) {
                if (std::string(error.what()).find("Permission denied") != std::string::npos)
                    denied = true;
                else
                    throw;
            }
            if (!denied)
                throw std::runtime_error("The document is readable; attach it instead");
            auto candidate = emails_;
            EmailSystem::set_draft(
                candidate, id,
                "I located the document, but my account cannot read it. I am blocked by its "
                "permissions. Please contact the document owner if you need it urgently.",
                computer_.time_seconds());
            EmailSystem::send(candidate, id, file_system_, network_, file_identity(),
                              computer_.time_seconds());
            emails_ = std::move(candidate);
            WorkdaySystem::observe_reply(people_, emails_, pending_events_, id,
                                         computer_.time_seconds());
            add_log("Sent a permission-blocked update; the colleague still needs the document.");
        }
        select_tab(Tab::Chat);
        return;
    }
    if (action == "import") {
        no_extra(input);
        const auto source = DefinitionLoader::load(definitions_.emails);
        const auto count = EmailSystem::import_messages(emails_, source);
        add_log("Imported " + std::to_string(count) +
                " scenario messages; existing messages retained.");
        select_tab(Tab::Chat);
        return;
    }
    if (action == "read" || action == "reply" || action == "send") {
        std::string id;
        if (!(input >> id))
            id = selected_email().id;
        no_extra(input);
        const auto target = EmailSystem::message(emails_, id, computer_.time_seconds());
        messages_ui_.chat_contact_id_ = ChatSystem::contact_for(people_, target.sender);
        messages_ui_.chat_follow_latest_ = true;
        messages_ui_.chat_choice_selected_ = messages_ui_.chat_offset_ = 0;
        messages_ui_.chat_contacts_focused_ = false;
        const auto visible = filtered_emails();
        if (action != "reply" &&
            std::none_of(visible.begin(), visible.end(), [&](const auto& email) {
                return email.id == id;
            }))
            editor_ui_.email_folder_ = target.archived ? EmailFolder::Archived : EmailFolder::Inbox;
        if (action == "send") {
            bool prepared = false;
            if (auto* tasks = people_["work_requests"].as_array())
                for (const auto& n : *tasks)
                    prepared |=
                        (*n.as_table())["id"].value_or(std::string{}) == id &&
                        (*n.as_table())["delegation_state"].value_or(std::string{}) == "prepared";
            if (prepared)
                DelegationSystem::approve(people_, emails_, file_system_, network_, file_identity(),
                                          id, computer_.time_seconds());
            else
                EmailSystem::send(emails_, id, file_system_, network_, file_identity(),
                                  computer_.time_seconds());
            WorkdaySystem::observe_reply(people_, emails_, pending_events_, id,
                                         computer_.time_seconds());
            add_log("Sent simulated reply to " + id + ".");
        } else if (action == "reply")
            edit_email(id);
        else
            EmailSystem::mark_read(emails_, id, computer_.time_seconds());
        editor_ui_.selected_email_id_ = id;
        editor_ui_.email_body_offset_ = 0;
        select_tab(Tab::Chat);
        return;
    }
    if (action == "clear") {
        no_extra(input);
        EmailSystem::clear_draft(emails_, selected_email().id, computer_.time_seconds());
        add_log("Reply draft and attachments cleared; sent replies retained.");
        select_tab(Tab::Chat);
        return;
    }
    if (action == "source" || action == "attach" || action == "detach") {
        int number = 0;
        if (!(input >> number) || number < 1)
            throw std::runtime_error("Usage: email " + action + " <number>");
        no_extra(input);
        const auto email = selected_email();
        const auto index = static_cast<std::size_t>(number - 1);
        if (action == "attach") {
            EmailSystem::attach(emails_, email.id, index, file_system_, network_, file_identity(),
                                computer_.time_seconds());
            add_log("Attached source " + std::to_string(number) + " to reply draft.");
            select_tab(Tab::Chat);
        } else if (action == "detach") {
            EmailSystem::detach(emails_, email.id, index, computer_.time_seconds());
            add_log("Detached attachment " + std::to_string(number) + ".");
            select_tab(Tab::Chat);
        } else {
            if (index >= email.documents.size())
                throw std::runtime_error("Invalid source document number");
            const auto& source = email.documents[index];
            // Read first: inaccessible references must not change the file view.
            (void)file_system_.read(network_, file_identity(), source.host_id, source.path);
            file_system_.enter(network_, file_identity(), source.host_id,
                               FileSystem::parent(source.path));
            files_ui_.file_host_id_ = source.host_id;
            files_ui_.file_directory_ = FileSystem::parent(source.path);
            files_ui_.selected_file_path_ = source.path;
            files_ui_.file_list_offset_ = files_ui_.file_content_offset_ = 0;
            select_tab(Tab::Files);
            add_log("Opened referenced document " + source.host_id + ":" + source.path);
        }
        return;
    }
    throw std::runtime_error(
        "email inbox/starred/unread/archived | star/unstar/archive/unarchive/mark-unread [id] | "
        "read/reply/send [id] | source/attach/detach <n> | clear | import");
}

void TerminalUI::draw_original_email(int top, int bottom, int column, int width) {
    const auto email =
        EmailSystem::message(emails_, editor_ui_.email_editor_id_, computer_.time_seconds());
    std::vector<std::string> lines;
    auto add = [&](const std::string& text) {
        auto rows = wrap(text, std::max(1, width));
        lines.insert(lines.end(), rows.begin(), rows.end());
    };
    add("From: " + email.sender);
    add("Subject: " + email.subject);
    lines.emplace_back();
    add(email.body);
    lines.emplace_back();
    add("Referenced documents:");
    for (const auto& document : email.documents)
        add(document.host_id + ":" + document.path);
    const int visible = std::max(1, bottom - top + 1);
    editor_ui_.email_original_offset_ =
        std::clamp(editor_ui_.email_original_offset_, 0,
                   std::max(0, static_cast<int>(lines.size()) - visible));
    std::vector<ScreenRenderer::TextRow> original;
    original.reserve(lines.size());
    for (const auto& line : lines)
        original.push_back({line});
    renderer_->scroll_text("original-email", original, editor_ui_.email_original_offset_, top,
                           column, visible, width);
}

} // namespace ai_sim::ui
