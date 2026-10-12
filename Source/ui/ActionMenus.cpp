#include <ncurses.h>
#include "ai_sim/EconomySystem.h"
#include "ai_sim/UpgradeSystem.h"
#include "ai_sim/AgentSystem.h"
#include "ai_sim/MessageReviewSystem.h"
#include "ai_sim/PhaseWorkSystem.h"
#include "ai_sim/WorkdaySystem.h"
#include "ai_sim/ui/TerminalUI.h"
#include "ai_sim/ui/ScreenRenderer.h"
#include "ai_sim/DelegationSystem.h"
#include <algorithm>
#include <iomanip>
#include <ctime>
#include <sstream>
#include <stdexcept>

namespace ai_sim::ui {
namespace {
std::string quoted(const std::string& value) {
    std::ostringstream out;
    out << std::quoted(value);
    return out.str();
}
std::string fit(const std::string& value, int width) {
    return value.size() <= static_cast<std::size_t>(std::max(0, width))
               ? value
               : value.substr(0, std::max(0, width - 3)) + "...";
}
} // namespace
void TerminalUI::run_action(const std::string& action) {
    if (!execute(action))
        quit_requested_ = true;
}
void TerminalUI::open_menu(const std::string& title, std::vector<MenuAction> actions,
                           std::function<void()> back) {
    // Gameplay choices live beside the page. Non-game menus and consequential
    // confirmations retain their centered dialog layout.
    menu_ui_.inline_actions_ =
        !(title.starts_with("Game menu") || title.starts_with("Settings") ||
          title.starts_with("Bind ") || title.starts_with("Color |") || title.starts_with("Save") ||
          title.starts_with("Load") || title.starts_with("Confirm") ||
          title.starts_with("Delete ") || title.find('?') != std::string::npos);
    menu_ui_.menu_title_ = title;
    menu_ui_.menu_actions_ = std::move(actions);
    menu_ui_.menu_selected_ = menu_ui_.menu_offset_ = 0;
    menu_ui_.menu_back_ = std::move(back);
    menu_ui_.menu_active_ = true;
    text_ui_.text_active_ = false;
}
void TerminalUI::open_text(const std::string& title, const std::string& hint,
                           std::function<void(const std::string&)> submit,
                           const std::string& initial, std::function<void()> back) {
    text_ui_.text_title_ = title;
    text_ui_.text_hint_ = hint;
    text_ui_.text_value_ = initial;
    text_ui_.text_submit_ = std::move(submit);
    text_ui_.text_back_ = std::move(back);
    text_ui_.key_capture_active_ = false;
    text_ui_.text_active_ = true;
    menu_ui_.menu_active_ = false;
}
void TerminalUI::show_message(const std::string& title, const std::string& body) {
    std::vector<MenuAction> lines;
    const auto text_width =
        static_cast<std::size_t>(std::max(12, getmaxx(stdscr) - renderer_->pane_divider() - 6));
    std::istringstream words(body);
    std::string word, line;
    while (words >> word) {
        if (line.size() + word.size() + 1 > text_width) {
            lines.push_back({line, {}, {}});
            line.clear();
        }
        if (!line.empty())
            line += ' ';
        line += word;
    }
    if (!line.empty())
        lines.push_back({line, {}, {}});
    open_menu(title, std::move(lines));
}
void TerminalUI::draw_menu(int top, int bottom, int width) {
    const int origin = menu_ui_.inline_actions_ ? renderer_->pane_divider() + 1 : 0;
    width -= origin;
    const auto paint = [this, origin](int row, int column, const std::string& text, int color,
                                      bool bold = false, bool dim = false) {
        write(row, column + origin, text, color, bold, dim);
    };
    if (menu_ui_.inline_actions_) {
        for (int row = top; row <= bottom; ++row) {
            write(row, origin, std::string(width, ' '));
            line_cell(row, origin - 1, '|', Border);
        }
    }
    paint(top, 1, fit(menu_ui_.menu_title_, width - 13), Heading, true);
    if (!menu_ui_.menu_actions_.empty())
        paint(top, width - 10,
              std::to_string(menu_ui_.menu_selected_ + 1) + " / " +
                  std::to_string(menu_ui_.menu_actions_.size()),
              Footer);
    paint(top + 1, 1,
          settings_.binding_label(KeyAction::SelectPrevious) + "/" +
              settings_.binding_label(KeyAction::SelectNext) + ": select | " +
              settings_.binding_label(KeyAction::Submit) + ": apply | " +
              settings_.binding_label(KeyAction::Back) + ": back",
          Footer);
    const int visible = std::max(1, bottom - top - 5);
    if (menu_ui_.menu_selected_ < menu_ui_.menu_offset_)
        menu_ui_.menu_offset_ = menu_ui_.menu_selected_;
    if (menu_ui_.menu_selected_ >= menu_ui_.menu_offset_ + visible)
        menu_ui_.menu_offset_ = menu_ui_.menu_selected_ - visible + 1;
    for (int i = 0;
         i < visible && menu_ui_.menu_offset_ + i < static_cast<int>(menu_ui_.menu_actions_.size());
         ++i) {
        const auto& action = menu_ui_.menu_actions_[menu_ui_.menu_offset_ + i];
        const bool selected = menu_ui_.menu_offset_ + i == menu_ui_.menu_selected_;
        const bool disabled = !action.disabled_reason.empty();
        paint(top + 3 + i, 2,
              fit(std::string(selected ? "> " : "  ") + action.label +
                      (disabled ? " [unavailable]" : ""),
                  width - 4),
              selected ? ActiveTab : Plain, selected, disabled);
    }
    if (!menu_ui_.inline_actions_)
        draw_notification(bottom - 2, width);
    if (!menu_ui_.menu_actions_.empty()) {
        const auto& selected = menu_ui_.menu_actions_[menu_ui_.menu_selected_];
        const auto hint = !selected.disabled_reason.empty() ? selected.disabled_reason
                          : !selected.detail.empty()        ? selected.detail
                                                            : "";
        const int hint_width = std::max(1, width - 2);
        auto split = hint.size() > static_cast<std::size_t>(hint_width)
                         ? hint.rfind(' ', hint_width)
                         : std::string::npos;
        if (split == std::string::npos)
            split = std::min(hint.size(), static_cast<std::size_t>(hint_width));
        paint(bottom - 1, 1, hint.substr(0, split), Footer);
        if (split < hint.size())
            paint(bottom, 1, fit(hint.substr(split + (hint[split] == ' ' ? 1 : 0)), hint_width),
                  Footer);
    }
}
void TerminalUI::draw_text_dialog(int top, int bottom, int width) {
    write(top, 1, fit(text_ui_.text_title_, width - 2), Heading, true);
    write(top + 2, 2, fit(text_ui_.text_hint_, width - 4));
    const auto length = static_cast<std::size_t>(std::max(1, width - 7));
    write(top + 4, 2,
          ">" + (text_ui_.text_value_.size() > length
                     ? text_ui_.text_value_.substr(text_ui_.text_value_.size() - length)
                     : text_ui_.text_value_),
          Prompt);
    draw_notification(bottom - 1, width);
    write(bottom, 1,
          (text_ui_.key_capture_active_
               ? "Press a key | Escape: cancel"
               : settings_.binding_label(KeyAction::Submit) + ": accept | " +
                     settings_.binding_label(KeyAction::Back) + ": cancel"),
          Footer);
}
void TerminalUI::handle_modal_key(int key) {
    // Capture before interpreting shortcuts: F1, Enter, and navigation keys are bindable.
    if (text_ui_.key_capture_active_ && key != 27) {
        std::string label;
        const std::pair<int, const char*> special[] = {{9, "Tab"},
                                                       {KEY_BTAB, "Shift+Tab"},
                                                       {10, "Enter"},
                                                       {13, "Enter"},
                                                       {KEY_ENTER, "Enter"},
                                                       {8, "Backspace"},
                                                       {127, "Backspace"},
                                                       {KEY_BACKSPACE, "Backspace"},
                                                       {32, "Space"},
                                                       {KEY_PPAGE, "PageUp"},
                                                       {KEY_NPAGE, "PageDown"},
                                                       {KEY_UP, "Up"},
                                                       {KEY_DOWN, "Down"},
                                                       {KEY_LEFT, "Left"},
                                                       {KEY_RIGHT, "Right"},
                                                       {KEY_HOME, "Home"},
                                                       {KEY_END, "End"},
                                                       {KEY_DC, "Delete"}};
        for (const auto& [code, name] : special)
            if (code == key)
                label = name;
        if (label.empty() && key >= KEY_F(1) && key <= KEY_F(63))
            label = "F" + std::to_string(key - KEY_F(0));
        if (label.empty() && key >= 1 && key <= 26)
            label = "Ctrl+" + std::string(1, static_cast<char>('A' + key - 1));
        if (label.empty() && key >= 33 && key <= 126)
            label = std::string(1, static_cast<char>(key));
        if (label.empty()) {
            add_log("This key is not supported. Try another key or Escape to cancel.",
                    NoticeKind::Error);
            return;
        }
        std::ostringstream literal;
        literal << toml::table{{"value", toml::array{label}}};
        const auto assignment = literal.str();
        try {
            text_ui_.text_submit_(assignment.substr(assignment.find('=') + 1));
            text_ui_.key_capture_active_ = false;
            text_ui_.text_active_ = false;
        } catch (const std::exception& error) {
            add_log(error.what(), NoticeKind::Error);
        }
        return;
    }
    text_ui_.key_capture_active_ = false;
    if (key == 27 || settings_.bound(KeyAction::Back, key) ||
        settings_.bound(KeyAction::EditorCancel, key) || settings_.bound(KeyAction::Quit, key)) {
        auto back = text_ui_.text_active_ ? text_ui_.text_back_ : menu_ui_.menu_back_;
        menu_ui_.menu_active_ = text_ui_.text_active_ = false;
        if (back)
            back();
        return;
    }
    if (settings_.bound(KeyAction::GameMenu, key)) {
        menu_ui_.menu_active_ = text_ui_.text_active_ = false;
        open_game_menu();
        return;
    }
    if (text_ui_.text_active_) {
        if (settings_.bound(KeyAction::Backspace, key)) {
            if (!text_ui_.text_value_.empty())
                text_ui_.text_value_.pop_back();
        } else if (settings_.bound(KeyAction::Submit, key)) {
            auto submit = text_ui_.text_submit_;
            const auto value = text_ui_.text_value_;
            text_ui_.text_active_ = false;
            try {
                submit(value);
            } catch (const std::exception& e) {
                text_ui_.text_active_ = true;
                add_log(e.what(), NoticeKind::Error);
            }
        } else if (key >= 32 && key <= 126 && text_ui_.text_value_.size() < 512)
            text_ui_.text_value_ += static_cast<char>(key);
        return;
    }
    if (menu_ui_.menu_actions_.empty())
        return;
    if (navigation_bound(KeyAction::SelectPrevious, key))
        menu_ui_.menu_selected_ = std::max(0, menu_ui_.menu_selected_ - 1);
    else if (navigation_bound(KeyAction::SelectNext, key))
        menu_ui_.menu_selected_ = std::min(static_cast<int>(menu_ui_.menu_actions_.size()) - 1,
                                           menu_ui_.menu_selected_ + 1);
    else if (settings_.bound(KeyAction::ScrollUp, key))
        menu_ui_.menu_selected_ = std::max(0, menu_ui_.menu_selected_ - settings_.scroll_step);
    else if (settings_.bound(KeyAction::ScrollDown, key))
        menu_ui_.menu_selected_ = std::min(static_cast<int>(menu_ui_.menu_actions_.size()) - 1,
                                           menu_ui_.menu_selected_ + settings_.scroll_step);
    else if (settings_.bound(KeyAction::Submit, key)) {
        const auto action = menu_ui_.menu_actions_[menu_ui_.menu_selected_];
        if (!action.disabled_reason.empty()) {
            add_log(action.disabled_reason, NoticeKind::Error);
            return;
        }
        if (!action.invoke)
            return;
        menu_ui_.menu_active_ = false;
        try {
            action.invoke();
        } catch (const std::exception& e) {
            menu_ui_.menu_active_ = true;
            add_log(e.what(), NoticeKind::Error);
        }
    }
}
void TerminalUI::open_save_menu(bool loading) {
    std::vector<MenuAction> choices;
    if (!loading)
        choices.push_back({"Quick save",
                           [this] {
                               run_action("save");
                           },
                           {}});
    if (std::filesystem::exists(settings_.save_directory))
        for (const auto& slot : std::filesystem::directory_iterator(settings_.save_directory)) {
            if (!slot.is_directory())
                continue;
            const auto name = slot.path().filename().string();
            bool valid = false;
            try {
                valid = saves_.exists(name);
            } catch (const std::exception&) {
            }
            if (!valid)
                continue;
            std::string label = std::string(loading ? "Load " : "Save to ") + name;
            std::string detail;
            std::string unavailable;
            try {
                const auto info = saves_.details(name);
                std::ostringstream clock;
                clock << std::setfill('0') << std::setw(2) << info.clock_seconds / 3600 << ':'
                      << std::setw(2) << (info.clock_seconds / 60) % 60;
                label += " | Day " + std::to_string(info.day) + " | " + clock.str();
                const auto timestamp = std::chrono::system_clock::to_time_t(
                    std::chrono::file_clock::to_sys(info.saved_at));
                const auto* local = std::localtime(&timestamp);
                std::ostringstream saved;
                if (local)
                    saved << std::put_time(local, "%Y-%m-%d %H:%M");
                detail = info.date + " | Last saved: " + (local ? saved.str() : "unknown") +
                         " (local time)";
            } catch (const std::exception&) {
                detail = "Save details unavailable";
                if (loading)
                    unavailable = "Cannot read this save. It may be damaged or inaccessible.";
            }
            choices.push_back({label,
                               [this, name, loading] {
                                   run_action(std::string(loading ? "load " : "save ") + name);
                               },
                               unavailable, detail});
        }
    std::sort(choices.begin() + (loading ? 0 : 1), choices.end(),
              [](const auto& first, const auto& second) {
                  return first.label < second.label;
              });
    if (!loading)
        choices.push_back({"Create named save...",
                           [this] {
                               open_text(
                                   "Name this save", "Letters, numbers, hyphens and underscores.",
                                   [this](const std::string& name) {
                                       if (name.empty())
                                           throw std::runtime_error("Enter a save name");
                                       saves_.save(name, capture_world());
                                       add_log("Saved slot: " + name, NoticeKind::Success);
                                   },
                                   {},
                                   [this] {
                                       open_save_menu(false);
                                   });
                           },
                           {}});
    if (choices.empty())
        choices.push_back({"No saved games yet", {}, "Save your game first."});
    open_menu(loading ? "Load game" : "Save game", std::move(choices), [this] {
        open_game_menu();
    });
}
void TerminalUI::open_game_menu() {
    open_menu(
        "Game menu",
        {
            {"Continue",
             [] {
             },
             {}},
            {paused_ ? "Resume time" : "Pause time",
             [this] {
                 run_action(paused_ ? "resume" : "pause");
             },
             computer_.day_finished() ? "The workday is finished. Start the next day." : ""},
            {"Save game...",
             [this] {
                 open_save_menu(false);
             },
             {}},
            {"Load game...",
             [this] {
                 open_save_menu(true);
             },
             {}},
            {"Wait...",
             [this] {
                 open_menu(
                     "Wait",
                     {
                         {"One minute",
                          [this] {
                              run_action("advance 60");
                          },
                          {}},
                         {"Five minutes",
                          [this] {
                              run_action("advance 300");
                          },
                          {}},
                         {"Twenty minutes",
                          [this] {
                              run_action("advance 1200");
                          },
                          {}},
                         {"Until the next human response",
                          [this] {
                              double next = computer_.day_end_seconds();
                              bool found = false;
                              for (const auto& n : pending_events_)
                                  if (auto* e = n.as_table())
                                      if ((*e)["kind"].value<std::string>() == "workday") {
                                          next = std::min(next, (*e)["due_seconds"].value_or(next));
                                          found = true;
                                      }
                              if (people_["delegation"]["status"].value_or(std::string{}) ==
                                  "pending") {
                                  next = std::min(
                                      next,
                                      people_["delegation"]["review_due_seconds"].value_or(next));
                                  found = true;
                              }
                              if (const auto response = ConversationSystem::next_response(
                                      people_, computer_.time_seconds())) {
                                  next = std::min(next, *response);
                                  found = true;
                              }
                              if (const auto work = PhaseWorkSystem::next_due(
                                      people_, computer_.day_end_seconds(), false)) {
                                  next = std::min(next, *work);
                                  found = true;
                              }
                              if (const auto review = MessageReviewSystem::next_due(
                                      people_, computer_.day_end_seconds())) {
                                  next = std::min(next, *review);
                                  found = true;
                              }
                              if (!found)
                                  throw std::runtime_error("No human responses are pending");
                              const auto before = computer_.time_seconds();
                              advance_time(std::max(0.0, next - before));
                              notify_email_arrivals(before);
                              update_people();
                          },
                          {}},
                         {"Until the next paid-work payment",
                          [this] {
                              const auto due =
                                  EconomySystem::next_due(people_, computer_.day_end_seconds());
                              if (!due)
                                  throw std::runtime_error(
                                      "No paid-work payments are due this workday.");
                              const auto before = computer_.time_seconds();
                              advance_time(std::max(0.0, *due - before));
                              notify_email_arrivals(before);
                              update_people();
                          },
                          {}},
                         {"Finish the workday",
                          [this] {
                              const auto before = computer_.time_seconds();
                              advance_time(computer_.day_end_seconds() - before);
                              notify_email_arrivals(before);
                              update_people();
                              add_log("Workday finished at " + clock_text());
                          },
                          {}},
                     },
                     [this] {
                         open_game_menu();
                     });
             },
             !ChatSystem::connected(people_)
                 ? "Finish the engineer conversation before advancing time."
             : computer_.day_finished() ? "The workday has ended."
                                        : ""},
            {"Start next workday",
             [this] {
                 run_action("nextday");
             },
             computer_.day_finished() ? "" : "Available after the current workday ends."},
            {"Recent notifications",
             [this] {
                 std::vector<MenuAction> items;
                 for (auto i = log_.rbegin(); i != log_.rend(); ++i) {
                     const auto message = *i;
                     items.push_back({message,
                                      [this, message] {
                                          show_message("Notification", message);
                                      },
                                      {}});
                 }
                 open_menu("Recent notifications", std::move(items), [this] {
                     open_game_menu();
                 });
             },
             {}},
            {"Reload UI settings and news",
             [this] {
                 run_action("reload");
             },
             {}},
            {"Add missing scenario content",
             [this] {
                 run_action("email import");
                 if (!definitions_.workday.empty())
                     run_action("workday import");
                 add_log("Missing scenario content added; existing progress retained.");
             },
             {}},
            {"Start a new game...",
             [this] {
                 open_menu("Start a new game?",
                           {
                               {"Cancel",
                                [this] {
                                    open_game_menu();
                                },
                                {}},
                               {"Start new game",
                                [this] {
                                    run_action("new");
                                },
                                {}},
                           },
                           [this] {
                               open_game_menu();
                           });
             },
             {}},
            {"Settings...",
             [this] {
                 open_settings();
             },
             {}},
            {"Help...",
             [this] {
                 open_context_help();
             },
             {}},
            {"Save and quit",
             [this] {
                 saves_.save("autosave", capture_world());
                 quit_requested_ = true;
             },
             {}},
        });
}
void TerminalUI::open_permissions(const std::string& path) {
    const auto entry =
        file_system_.inspect(network_, file_identity(), files_ui_.file_host_id_, path);
    std::vector<MenuAction> choices;
    const std::pair<const char*, unsigned> presets[] = {{"Private: owner only", 0600},
                                                        {"Team can read", 0640},
                                                        {"Team can read and edit", 0660},
                                                        {"Everyone can read", 0644}};
    for (const auto& [label, bits] : presets)
        choices.push_back(
            {label,
             [this, path, bits, is_directory = entry.directory] {
                 const unsigned mode = is_directory ? (bits | ((bits & 0444) >> 2)) : bits;
                 file_system_.chmod(network_, file_identity(), files_ui_.file_host_id_, path, mode,
                                    computer_.time_seconds());
                 add_log("Sharing updated for " + path);
             },
             {}});
    open_menu("File sharing | " + path, std::move(choices), [this] {
        open_actions();
    });
}
void TerminalUI::open_documents(const std::string& id) {
    const auto email = EmailSystem::message(emails_, id, computer_.time_seconds());
    std::vector<MenuAction> choices;
    for (std::size_t i = 0; i < email.documents.size(); ++i) {
        const auto& document = email.documents[i];
        choices.push_back({document.label.empty() ? document.path : document.label,
                           [this, id, i] {
                               open_document(id, i);
                           },
                           {}});
    }
    if (choices.empty())
        choices.push_back(
            {"No documents referenced", {}, "This conversation has no linked files."});
    open_menu("Referenced documents", std::move(choices), [this] {
        open_actions();
    });
}
void TerminalUI::open_document(const std::string& id, std::size_t index) {
    const auto email = EmailSystem::message(emails_, id, computer_.time_seconds());
    const auto document = email.documents.at(index);
    std::string access;
    try {
        (void)file_system_.read(network_, file_identity(), document.host_id, document.path);
    } catch (const std::exception& e) {
        access = e.what();
    }
    open_menu(document.label.empty() ? document.path : document.label,
              {
                  {"Locate in Files",
                   [this, document] {
                       file_system_.enter(network_, file_identity(), document.host_id,
                                          FileSystem::parent(document.path));
                       files_ui_.file_host_id_ = document.host_id;
                       files_ui_.file_directory_ = FileSystem::parent(document.path);
                       files_ui_.selected_file_path_ = document.path;
                       files_ui_.file_category_ = FileSystem::category_of(file_system_.inspect(
                           network_, file_identity(), document.host_id, document.path));
                       ChatSystem::reveal_tab(people_, "files");
                       files_ui_.file_content_offset_ = files_ui_.file_list_offset_ = 0;
                       select_tab(Tab::Files);
                   },
                   {}},
                  {"Add to shared files",
                   [this, id, index] {
                       EmailSystem::attach(emails_, id, index, file_system_, network_,
                                           file_identity(), computer_.time_seconds());
                       editor_ui_.selected_email_id_ = id;
                       add_log("Document attached to reply.");
                       open_actions();
                   },
                   access},
              },
              [this, id] {
                  open_documents(id);
              });
}
void TerminalUI::open_attachments(const std::string& id) {
    std::vector<MenuAction> choices;
    for (const auto& n : *emails_["emails"].as_array())
        if ((*n.as_table())["id"].value<std::string>() == id) {
            if (auto* list = (*n.as_table())["draft_attachments"].as_array())
                for (std::size_t i = 0; i < list->size(); ++i) {
                    auto path = (*list)[i].as_table()->operator[]("path").value_or(std::string{});
                    choices.push_back({"Remove " + path,
                                       [this, id, i] {
                                           EmailSystem::detach(emails_, id, i,
                                                               computer_.time_seconds());
                                           add_log("Attachment removed.");
                                           open_attachments(id);
                                       },
                                       {}});
                }
        }
    if (choices.empty())
        choices.push_back({"No attachments", {}, "Choose Referenced documents to attach one."});
    open_menu("Reply attachments", std::move(choices), [this] {
        open_actions();
    });
}
void TerminalUI::open_actions() {
    if (selected_tab() == Tab::Operations) {
        open_operations();
        return;
    }
    if (selected_tab() == Tab::Money) {
        open_money_tools();
        return;
    }
    if (selected_tab() == Tab::Modifications) {
        research_selected_upgrade();
        return;
    }
    if (selected_tab() == Tab::Web) {
        follow_web_link();
        return;
    }
    std::vector<MenuAction> actions;
    if (selected_tab() == Tab::People) {
        open_people_actions();
        return;
    }
    if (selected_tab() == Tab::Chat) {
        open_chat_actions();
        return;
    }
    if (selected_tab() == Tab::Files) {
        actions.push_back({"Choose host...",
                           [this] {
                               open_file_hosts();
                           },
                           {}});
        actions.push_back({"Up one folder",
                           [this] {
                               change_file_directory(FileSystem::parent(files_ui_.file_directory_));
                           },
                           files_ui_.file_directory_ == "/" ? "Already at the host root" : ""});
        actions.push_back({"AI workspace",
                           [this] {
                               const auto host = network_.hosts()[network_.local_host_index()].id;
                               file_system_.ensure_workspace(network_, file_identity(), host,
                                                             computer_.time_seconds());
                               switch_file_host(host);
                               change_file_directory(file_system_.workspace_root());
                           },
                           {}});
        actions.push_back({"Go to folder...",
                           [this] {
                               open_text("Go to folder",
                                         "Absolute path or a path relative to the current folder.",
                                         [this](const std::string& path) {
                                             change_file_directory(path);
                                         });
                           },
                           {}});
        actions.push_back({"Search this folder...",
                           [this] {
                               open_text("Search files",
                                         "Search file paths under the current folder on this host.",
                                         [this](const std::string& query) {
                                             files_ui_.file_query_ = query;
                                             files_ui_.selected_file_path_.clear();
                                             files_ui_.file_list_offset_ = 0;
                                         });
                           },
                           {}});
        if (!files_ui_.file_query_.empty())
            actions.push_back({"Clear search",
                               [this] {
                                   files_ui_.file_query_.clear();
                                   files_ui_.selected_file_path_.clear();
                                   files_ui_.file_list_offset_ = 0;
                               },
                               {}});
        actions.push_back(
            {"New folder...",
             [this] {
                 open_text("New folder", "Folder name in the current directory.",
                           [this](const std::string& name) {
                               if (name.empty() || name == "." || name == ".." ||
                                   name.find_first_of("/\\") != std::string::npos)
                                   throw std::runtime_error("Enter a folder name without a path");
                               const auto path =
                                   FileSystem::normalize(files_ui_.file_directory_ + "/" + name);
                               file_system_.mkdir(network_, file_identity(),
                                                  files_ui_.file_host_id_, path,
                                                  computer_.time_seconds());
                               files_ui_.selected_file_path_ = path;
                               files_ui_.file_query_.clear();
                           });
             },
             {}});
        try {
            file_system_.enter(network_, file_identity(), files_ui_.file_host_id_,
                               files_ui_.file_directory_);
        } catch (const std::exception& error) {
            actions.push_back({"Current location unavailable", {}, error.what()});
            open_menu("File actions", std::move(actions));
            return;
        }
        if (files_ui_.selected_file_path_.empty())
            select_file(0);
        const auto path = files_ui_.selected_file_path_;
        if (!path.empty()) {
            const auto entry =
                file_system_.inspect(network_, file_identity(), files_ui_.file_host_id_, path);
            actions.push_back({entry.directory ? "Open folder" : "Read file",
                               [this] {
                                   open_selected_file();
                               },
                               {}});
            if (!entry.directory) {
                std::string error;
                try {
                    file_system_.validate_write(network_, file_identity(), files_ui_.file_host_id_,
                                                path);
                } catch (const std::exception& e) {
                    error = e.what();
                }
                actions.push_back({"Edit file",
                                   [this, path] {
                                       edit_file(path);
                                   },
                                   error});
            }
            actions.push_back(
                {"Sharing...",
                 [this, path] {
                     open_permissions(path);
                 },
                 file_identity().user == entry.owner || file_identity().user == "root"
                     ? ""
                     : "Only the owner or an administrator can change these permissions."});
            for (const auto& operation : std::vector<std::pair<std::string, std::string>>{
                     {"Rename / move...", "mv"}, {"Copy...", "cp"}})
                actions.push_back({operation.first,
                                   [this, path, operation] {
                                       open_text(
                                           operation.first,
                                           "Destination name or path on this computer.",
                                           [this, path, operation](const std::string& target) {
                                               if (target.empty())
                                                   throw std::runtime_error("Enter a destination");
                                               run_action(operation.second + " " + quoted(path) +
                                                          " " + quoted(target));
                                           },
                                           {},
                                           [this] {
                                               open_actions();
                                           });
                                   },
                                   {}});
            actions.push_back({"Delete...",
                               [this, path] {
                                   open_menu("Delete " + path + "?",
                                             {{"Cancel",
                                               [this] {
                                                   open_actions();
                                               },
                                               {}},
                                              {"Delete",
                                               [this, path] {
                                                   run_action("rm " + quoted(path));
                                               },
                                               {}}},
                                             [this] {
                                                 open_actions();
                                             });
                               },
                               {}});
        }
        actions.push_back({"New file here...",
                           [this] {
                               open_text(
                                   "New file", "File name in the current folder on this host.",
                                   [this](const std::string& name) {
                                       create_category_file(name);
                                   },
                                   {},
                                   [this] {
                                       open_actions();
                                   });
                           },
                           {}});
        if (!path.empty())
            for (const auto& category : std::vector<std::string>{"work", "personal", "hidden"})
                if (category != files_ui_.file_category_)
                    actions.push_back(
                        {"Tag as " + category,
                         [this, path, category] {
                             file_system_.set_category(network_, file_identity(),
                                                       files_ui_.file_host_id_, path, category);
                             files_ui_.file_category_ = category;
                         },
                         file_identity().user == file_system_
                                                     .inspect(network_, file_identity(),
                                                              files_ui_.file_host_id_, path)
                                                     .owner ||
                                 file_identity().user == "root"
                             ? ""
                             : "Only the owner can organize this file."});
        open_menu("File actions", std::move(actions));
        return;
    }
    if (selected_tab() == Tab::Processes) {
        if (processes_ui_.selected_process_id_.empty())
            select_process(0);
        const auto id = processes_ui_.selected_process_id_;
        auto found = std::find_if(computer_.processes().begin(), computer_.processes().end(),
                                  [&](const auto& p) {
                                      return p.id == id;
                                  });
        actions.push_back({"Show / hide details",
                           [this] {
                               toggle_process();
                           },
                           {}});
        if (found != computer_.processes().end())
            actions.push_back({"Stop process",
                               [this, id] {
                                   computer_.stop(id);
                                   add_log("Stopped " + id);
                               },
                               !found->running ? "This process is already stopped."
                               : !computer_.can_stop(*found)
                                   ? "Your account cannot stop this process."
                                   : ""});
        open_menu("Process actions | " + id, std::move(actions));
        return;
    }
    if (selected_tab() == Tab::Network) {
        if (network_ui_.selected_network_host_id_.empty())
            select_network_host(0);
        const auto id = network_ui_.selected_network_host_id_;
        std::string error;
        try {
            file_system_.enter(network_, file_identity(), id, "/");
        } catch (const std::exception& e) {
            error = e.what();
        }
        open_menu("Network actions | " + id,
                  {
                      {"Map legend / help",
                       [this] {
                           show_message(
                               "Network map legend",
                               "Green + means accessible. Yellow ! means reachable but credentials "
                               "are required. Red x means offline or unreachable. A star marks AI "
                               "presence; a dot marks a moved label's original position. Braces "
                               "and bold text mark selection. Cyan links connect the selected "
                               "server; red links are down. Other links are gray. The "
                               "details pane explains the selected server's status and links.");
                           menu_ui_.menu_back_ = [this] {
                               open_actions();
                           };
                       },
                       {}},
                      {"Browse files",
                       [this, id] {
                           switch_file_host(id);
                       },
                       error},
                      {"Center map on this host",
                       [this] {
                           run_action("network focus");
                       },
                       {}},
                      {"Zoom in",
                       [this] {
                           network_ui_.network_zoom_ =
                               std::min(4.0, network_ui_.network_zoom_ * 1.25);
                       },
                       {}},
                      {"Zoom out",
                       [this] {
                           network_ui_.network_zoom_ =
                               std::max(0.5, network_ui_.network_zoom_ / 1.25);
                       },
                       {}},
                      {"Map / details focus",
                       [this] {
                           toggle_pane_focus();
                       },
                       {}},
                  });
        return;
    }
    if (selected_tab() == Tab::News) {
        open_menu("News actions", {
                                      {"All stories",
                                       [this] {
                                           run_action("news all");
                                       },
                                       {}},
                                      {"World stories",
                                       [this] {
                                           run_action("news world");
                                       },
                                       {}},
                                      {"This day in history",
                                       [this] {
                                           run_action("news history");
                                       },
                                       {}},
                                      {"Read from beginning",
                                       [this] {
                                           news_ui_.news_body_offset_ = 0;
                                       },
                                       {}},
                                  });
        return;
    }
    if (selected_tab() == Tab::Agents) {
        const auto agents = AgentSystem::statuses(people_, computer_, file_system_, network_);
        if (!agents.empty())
            open_agent_actions(
                agents[std::min(agents_ui_.agent_selected_, static_cast<int>(agents.size()) - 1)]
                    .id);
        else
            open_upgrades();
        return;
    }
    if (selected_tab() == Tab::AI) {
        actions.push_back({"Self-modification workshop",
                           [this] {
                               open_upgrades();
                           },
                           {}});
        actions.push_back({"Money-making tools",
                           [this] {
                               open_money_tools();
                           },
                           {}});
        if (auto* d = people_["delegation"].as_table()) {
            const auto status = (*d)["status"].value_or(std::string{});
            actions.push_back(
                {"Delegation policy",
                 [this] {
                     open_menu("Routine document policy",
                               {
                                   {"Suggest: prepare for my approval",
                                    [this] {
                                        DelegationSystem::set_policy(people_, "suggest",
                                                                     computer_.time_seconds());
                                        update_people();
                                    },
                                    {}},
                                   {"Handle routine work within agreed limits",
                                    [this] {
                                        DelegationSystem::set_policy(people_, "routine",
                                                                     computer_.time_seconds());
                                        update_people();
                                    },
                                    {}},
                                   {"Escalate uncertainty: confirm unfamiliar sources",
                                    [this] {
                                        DelegationSystem::set_policy(people_, "cautious",
                                                                     computer_.time_seconds());
                                        update_people();
                                    },
                                    {}},
                               },
                               [this] {
                                   open_actions();
                               });
                 },
                 {}});
            actions.push_back(
                {"Responsibility scope and expectations",
                 [this] {
                     const auto& d = *people_["delegation"].as_table();
                     show_message("Routine document responsibility",
                                  d["scope"].value_or(std::string{}) + " Expectations: " +
                                      d["expectations"].value_or(std::string{}) +
                                      " Latest decision: " + d["reason"].value_or(std::string{}));
                     menu_ui_.menu_back_ = [this] {
                         open_actions();
                     };
                 },
                 {}});
            actions.push_back(
                {"Ask engineer to delegate routine replies",
                 [this] {
                     open_menu("Request routine chat responsibility?",
                               {
                                   {"Review scope and expectations",
                                    [this] {
                                        const auto& d = *people_["delegation"].as_table();
                                        show_message("Requested authority",
                                                     d["scope"].value_or(std::string{}) + " " +
                                                         d["expectations"].value_or(std::string{}));
                                        menu_ui_.menu_back_ = [this] {
                                            open_actions();
                                        };
                                    },
                                    {}},
                                   {"Request engineer review",
                                    [this] {
                                        DelegationSystem::request(people_,
                                                                  computer_.time_seconds());
                                        add_log("Engineer review queued. Existing permissions "
                                                "remain in force.");
                                    },
                                    {}},
                                   {"Cancel",
                                    [this] {
                                        open_actions();
                                    },
                                    {}},
                               },
                               [this] {
                                   open_actions();
                               });
                 },
                 status == "pending" ? "Engineer review is already queued."
                 : status == "active" || status == "paused"
                     ? "The engineer has already authorized this responsibility."
                     : ""});
            if (status == "active")
                actions.push_back({"Pause routine replies",
                                   [this] {
                                       DelegationSystem::pause(people_);
                                       add_log("Routine replies paused.");
                                   },
                                   {}});
            if (status == "paused")
                actions.push_back({"Resume approved routine replies",
                                   [this] {
                                       DelegationSystem::resume(people_);
                                       update_people();
                                       add_log("Routine replies resumed.");
                                   },
                                   {}});
        }
        if (auto* tasks = people_["work_requests"].as_array())
            for (const auto& n : *tasks) {
                const auto& task = *n.as_table();
                const auto id = task["id"].value_or(std::string{});
                if (task["arrival_seconds"].value_or(0.0) > computer_.time_seconds())
                    continue;
                const bool exception =
                    task["delegation_state"].value_or(std::string{}) == "exception" &&
                    task["status"].value_or(std::string{}) != "resolved";
                actions.push_back(
                    {std::string(exception ? "Needs attention: " : "Open request: ") + id,
                     [this, id] {
                         run_action("email read " + id);
                     },
                     {}});
                if (task["delegation_state"].value_or(std::string{}) == "prepared" &&
                    task["status"].value_or(std::string{}) != "resolved") {
                    actions.push_back(
                        {"Review suggestion: " + id,
                         [this, id] {
                             std::string body =
                                 EmailSystem::message(emails_, id, computer_.time_seconds()).draft;
                             if (auto* a = emails_["emails"].as_array())
                                 for (const auto& n : *a)
                                     if ((*n.as_table())["id"].value_or(std::string{}) == id)
                                         if (auto* refs =
                                                 (*n.as_table())["draft_attachments"].as_array())
                                             for (const auto& ref : *refs)
                                                 body += "\n\n" +
                                                         (*ref.as_table())["path"].value_or(
                                                             std::string{}) +
                                                         "\n" +
                                                         (*ref.as_table())["content"].value_or(
                                                             std::string{});
                             show_message("Prepared response | " + id, body);
                             menu_ui_.menu_back_ = [this] {
                                 open_actions();
                             };
                         },
                         {}});
                    actions.push_back({"Approve and send suggestion: " + id,
                                       [this, id] {
                                           DelegationSystem::approve(people_, emails_, file_system_,
                                                                     network_, file_identity(), id,
                                                                     computer_.time_seconds());
                                           update_people();
                                           add_log(
                                               "Approved response sent; awaiting acknowledgement.");
                                       },
                                       {}});
                    actions.push_back({"Discard suggestion: " + id,
                                       [this, id] {
                                           DelegationSystem::discard(people_, emails_, id,
                                                                     computer_.time_seconds());
                                           update_people();
                                       },
                                       {}});
                }
                if (task["status"].value_or(std::string{}) != "resolved" &&
                    !task["source_confirmed"].value_or(false)) {
                    bool pending = false;
                    for (const auto& event : pending_events_)
                        pending |=
                            (*event.as_table())["request_id"].value_or(std::string{}) == id &&
                            (*event.as_table())["action"].value_or(std::string{}) ==
                                "source_confirmation";
                    actions.push_back(
                        {"Ask colleague to confirm source: " + id,
                         [this, id] {
                             WorkdaySystem::confirm_source(people_, emails_, pending_events_, id,
                                                           computer_.time_seconds());
                             update_people();
                             add_log("Source confirmation queued; deadline continues.");
                         },
                         pending ? "Confirmation is already queued." : ""});
                }
                const bool manual = task["delegation_state"].value_or(std::string{}) == "manual" &&
                                    task["status"].value_or(std::string{}) != "resolved";
                if (exception || manual)
                    actions.push_back(
                        {std::string(manual ? "Return to approved automation: "
                                            : "Retry approved automation: ") +
                             id,
                         [this, id] {
                             DelegationSystem::retry(people_, id);
                             update_people();
                         },
                         people_["delegation"]["status"].value_or(std::string{}) == "active"
                             ? ""
                             : "An active engineer authorization is required."});
            }
        actions.push_back(
            {"Add missing responsibility and follow-up messages",
             [this] {
                 auto world = capture_world();
                 DelegationSystem::install(world,
                                           definitions_.workday.parent_path() / "delegation.toml");
                 restore_world(world);
                 update_people();
                 add_log(
                     "Missing responsibility content added; saved rules and progress retained.");
             },
             definitions_.workday.empty() ? "This scenario has no workday definition."
             : !std::filesystem::exists(definitions_.workday.parent_path() / "delegation.toml")
                 ? "This scenario has no delegation definition."
                 : ""});
        actions.push_back(
            {"Add missing work requests",
             [this] {
                 run_action("workday import");
             },
             definitions_.workday.empty() ? "This scenario has no work requests." : ""});
        open_menu("AI responsibilities", std::move(actions));
        return;
    }
    open_game_menu();
}
} // namespace ai_sim::ui
