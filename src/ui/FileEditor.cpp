#include "ai_sim/ui/TerminalUI.h"
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace ai_sim::ui {
void TerminalUI::edit_file(const std::string& path) {
    file_system_.validate_write(network_, file_identity(), file_host_id_, path);
    std::string content;
    bool created = false;
    try {
        content = file_system_.read(network_, file_identity(), file_host_id_, path);
    } catch (const std::exception& error) {
        if (std::string(error.what()).rfind("No such file or directory:", 0) != 0)
            throw;
        created = true;
    }
    if (content.size() > static_cast<std::size_t>(settings_.editor_max_bytes)) {
        throw std::runtime_error("File exceeds interface.editor_max_bytes");
    }
    email_editor_ = false;
    editor_host_ = file_host_id_;
    editor_path_ = path;
    editor_buffer_ = std::move(content);
    editor_cursor_ = 0;
    editor_offset_ = 0;
    editor_dirty_ = created;
    editor_active_ = true;
    selected_tab_ = Tab::Files;
}

void TerminalUI::handle_editor_key(int key) {
    if (email_editor_ && (settings_.bound(KeyAction::PreviousTab, key) ||
                          settings_.bound(KeyAction::EditorFocus, key))) {
        email_original_focused_ = !email_original_focused_;
        return;
    }
    if (email_editor_ && email_original_focused_) {
        if (settings_.bound(KeyAction::NextTab, key)) {
            email_original_focused_ = false;
            return;
        }
        const bool up = navigation_bound(KeyAction::SelectPrevious, key) ||
                        settings_.bound(KeyAction::ScrollUp, key);
        const bool down = navigation_bound(KeyAction::SelectNext, key) ||
                          settings_.bound(KeyAction::ScrollDown, key);
        if (up || down) {
            const int step = settings_.bound(KeyAction::ScrollUp, key) ||
                                     settings_.bound(KeyAction::ScrollDown, key)
                                 ? settings_.scroll_step
                                 : 1;
            email_original_offset_ =
                std::clamp(email_original_offset_ + (up ? -step : step), 0, 1000000);
            return;
        }
        if (!settings_.bound(KeyAction::EditorSave, key) &&
            !settings_.bound(KeyAction::EditorCancel, key) &&
            !settings_.bound(KeyAction::Quit, key) && !settings_.bound(KeyAction::Pause, key))
            return;
    }
    if (settings_.bound(KeyAction::EditorCancel, key) || settings_.bound(KeyAction::Quit, key)) {
        editor_active_ = false;
        editor_buffer_.clear();
        add_log(email_editor_ ? "Email edit discarded; saved draft retained."
                              : "Editor draft discarded.");
    } else if (settings_.bound(KeyAction::EditorSave, key)) {
        if (email_editor_) {
            EmailSystem::set_draft(emails_, email_editor_id_, editor_buffer_,
                                   computer_.time_seconds());
            editor_active_ = false;
            editor_buffer_.clear();
            selected_tab_ = Tab::Chat;
            selected_email_id_ = email_editor_id_;
            add_log("Reply kept. Choose Send saved reply to send it.");
            open_actions();
        } else {
            file_system_.write(network_, file_identity(), editor_host_, editor_path_,
                               editor_buffer_, computer_.time_seconds());
            file_host_id_ = editor_host_;
            file_directory_ = FileSystem::parent(editor_path_);
            selected_file_path_ = editor_path_;
            file_content_offset_ = 0;
            editor_active_ = false;
            editor_buffer_.clear();
            add_log("Wrote " + editor_host_ + ":" + editor_path_);
        }
    } else if (settings_.bound(KeyAction::Pause, key)) {
        if (!computer_.day_finished())
            paused_ = !paused_;
    } else if (settings_.bound(KeyAction::ScrollLeft, key)) {
        if (editor_cursor_ > 0)
            --editor_cursor_;
    } else if (settings_.bound(KeyAction::ScrollRight, key)) {
        if (editor_cursor_ < editor_buffer_.size())
            ++editor_cursor_;
    } else if (settings_.bound(KeyAction::SelectPrevious, key) ||
               settings_.bound(KeyAction::SelectNext, key) ||
               settings_.bound(KeyAction::ScrollUp, key) ||
               settings_.bound(KeyAction::ScrollDown, key)) {
        const bool up = settings_.bound(KeyAction::SelectPrevious, key) ||
                        settings_.bound(KeyAction::ScrollUp, key);
        const int steps = (settings_.bound(KeyAction::ScrollUp, key) ||
                           settings_.bound(KeyAction::ScrollDown, key))
                              ? settings_.scroll_step
                              : 1;
        for (int step = 0; step < steps; ++step) {
            const auto previous_break = editor_cursor_ == 0
                                            ? std::string::npos
                                            : editor_buffer_.rfind('\n', editor_cursor_ - 1);
            const auto start = previous_break == std::string::npos ? 0 : previous_break + 1;
            const auto column = editor_cursor_ - start;
            if (up) {
                if (start == 0)
                    break;
                const auto previous_end = start - 1;
                const auto prior_break = previous_end == 0
                                             ? std::string::npos
                                             : editor_buffer_.rfind('\n', previous_end - 1);
                const auto prior_start = prior_break == std::string::npos ? 0 : prior_break + 1;
                editor_cursor_ = prior_start + std::min(column, previous_end - prior_start);
            } else {
                const auto end = editor_buffer_.find('\n', editor_cursor_);
                if (end == std::string::npos)
                    break;
                const auto next_start = end + 1;
                const auto next_break = editor_buffer_.find('\n', next_start);
                const auto next_end =
                    next_break == std::string::npos ? editor_buffer_.size() : next_break;
                editor_cursor_ = next_start + std::min(column, next_end - next_start);
            }
        }
    } else if (settings_.bound(KeyAction::Backspace, key)) {
        if (editor_cursor_ > 0) {
            editor_buffer_.erase(--editor_cursor_, 1);
            editor_dirty_ = true;
        }
    } else {
        std::string insertion;
        if (settings_.bound(KeyAction::Submit, key))
            insertion = "\n";
        else if (settings_.bound(KeyAction::NextTab, key))
            insertion = "    ";
        else if (key >= 32 && key <= 126)
            insertion = static_cast<char>(key);
        if (!insertion.empty()) {
            if (editor_buffer_.size() + insertion.size() >
                static_cast<std::size_t>(settings_.editor_max_bytes)) {
                throw std::runtime_error("Editor byte limit reached");
            }
            editor_buffer_.insert(editor_cursor_, insertion);
            editor_cursor_ += insertion.size();
            editor_dirty_ = true;
        }
    }
}

void TerminalUI::draw_editor(int top, int bottom, int width) {
    int text_column = 2;
    int text_width = width - 4;
    if (email_editor_) {
        const int split = width / 2;
        text_column = split + 2;
        text_width = width - split - 4;
        for (int row = top; row < bottom; ++row)
            write(row, split, "|", Border);
        write(top, 2, email_original_focused_ ? "[ ORIGINAL ]" : "ORIGINAL",
              email_original_focused_ ? ActiveTab : Heading, true);
        draw_original_email(top + 1, bottom - 1, 2, split - 4);
        const std::string heading = "REPLY | " + email_editor_id_ + (editor_dirty_ ? " *" : "") +
                                    (email_original_focused_ ? "" : " [focus]");
        write(top, text_column, heading.substr(0, std::max(1, text_width)),
              email_original_focused_ ? Heading : ActiveTab, true);
    } else {
        write(top, 1, "EDIT | " + editor_host_ + ":" + editor_path_ + (editor_dirty_ ? " *" : ""),
              Heading, true);
    }
    const auto line_width = static_cast<std::size_t>(std::max(1, text_width));
    struct Row {
        std::size_t start;
        std::size_t length;
    };
    std::vector<Row> rows;
    std::size_t start = 0;
    while (true) {
        const auto newline = editor_buffer_.find('\n', start);
        const auto end = newline == std::string::npos ? editor_buffer_.size() : newline;
        const auto length = end - start;
        if (length == 0)
            rows.push_back({start, 0});
        else {
            for (std::size_t offset = 0; offset < length; offset += line_width) {
                rows.push_back({start + offset, std::min(line_width, length - offset)});
            }
            if (length % line_width == 0)
                rows.push_back({end, 0});
        }
        if (newline == std::string::npos)
            break;
        start = newline + 1;
    }
    int cursor_row = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (editor_cursor_ >= rows[i].start && editor_cursor_ <= rows[i].start + rows[i].length) {
            cursor_row = static_cast<int>(i);
        }
    }
    const int visible = std::max(1, bottom - top - 1);
    if (cursor_row < editor_offset_)
        editor_offset_ = cursor_row;
    else if (cursor_row >= editor_offset_ + visible)
        editor_offset_ = cursor_row - visible + 1;
    for (int line = 0; line < visible; ++line) {
        const int index = editor_offset_ + line;
        if (index >= static_cast<int>(rows.size()))
            break;
        write(top + 1 + line, text_column,
              editor_buffer_.substr(rows[index].start, rows[index].length));
    }
    editor_cursor_row_ = top + 1 + cursor_row - editor_offset_;
    editor_cursor_column_ = text_column + static_cast<int>(editor_cursor_ - rows[cursor_row].start);
    write(bottom, 1,
          std::to_string(editor_buffer_.size()) + " B draft | " +
              settings_.binding_label(KeyAction::EditorSave) + ": save | " +
              settings_.binding_label(KeyAction::EditorCancel) + ": discard" +
              (email_editor_
                   ? " | " + settings_.binding_label(KeyAction::EditorFocus) + ": focus panes"
                   : ""),
          Footer);
}
} // namespace ai_sim::ui
