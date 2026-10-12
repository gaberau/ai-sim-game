// Module: Load and edit validated presentation settings, colors and configurable keybindings.
#pragma once

#include "ai_sim/ui/ProcessViewConfig.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

namespace ai_sim::ui {

inline constexpr std::array<std::string_view, 19> ColorRoleNames = {
    "text",          "protected",    "stoppable",    "player",       "heading",
    "clock",         "resources",    "active_tab",   "inactive_tab", "border",
    "log",           "prompt",       "footer",       "file_edit",    "file_read",
    "file_approval", "file_blocked", "file_unknown", "map_locked"};

enum class KeyAction {
    Quit,
    Submit,
    Backspace,
    Pause,
    NextTab,
    PreviousTab,
    ScrollUp,
    ScrollDown,
    SelectPrevious,
    SelectNext,
    ToggleExpanded,
    ScrollLeft,
    ScrollRight,
    EditorSave,
    EditorCancel,
    GameMenu,
    Back,
    ToggleFocus,
    EditorFocus,
    NavigateUp,
    NavigateDown,
    NavigateLeft,
    NavigateRight,
    FocusLeft,
    FocusRight,
    Help,
    ArchiveChat,
    ToggleArchivedChats,
    Count
};

inline constexpr std::array<std::string_view, static_cast<std::size_t>(KeyAction::Count)>
    KeyActionNames = {"quit",
                      "submit",
                      "backspace",
                      "pause",
                      "next_tab",
                      "previous_tab",
                      "scroll_up",
                      "scroll_down",
                      "select_previous",
                      "select_next",
                      "toggle_expanded",
                      "scroll_left",
                      "scroll_right",
                      "editor_save",
                      "editor_cancel",
                      "game_menu",
                      "back",
                      "toggle_focus",
                      "editor_focus",
                      "navigate_up",
                      "navigate_down",
                      "navigate_left",
                      "navigate_right",
                      "focus_left",
                      "focus_right",
                      "help",
                      "archive_chat",
                      "toggle_archived_chats"};

struct ColorSetting {
    short foreground = 7;
    short background = -1;
};

struct KeyBinding {
    std::vector<int> codes;
    std::vector<std::string> labels;
};

// Geometry uses terminal rows/columns; prose spacing is independent of game time.
struct UILayout {
    int workspace_top_gap = 1, workspace_bottom_gap = 1;
    int text_margin = 2, pane_padding = 3;
    int response_gap = 1, response_heading_gap = 1, response_height_percent = 50;
    int list_width_percent = 33, list_min_width = 26, list_max_width = 36;
    int operations_width_percent = 33, operations_min_width = 24, operations_max_width = 40;
    int minimum_columns = 80, minimum_rows = 24;
    int dialog_max_width = 92, dialog_max_height = 22;
    int news_width_percent = 40, email_editor_width_percent = 50;
};
inline constexpr std::array<std::string_view, 13> WorkspaceIds = {
    "messages", "processes", "network", "files", "news",          "ai",        "dossiers",
    "web",      "agents",    "phone",   "money", "modifications", "operations"};

struct UISettings {
    bool autosave_on_quit = true;
    bool full_redraw = true;
    bool software_cursor = true;
    std::filesystem::path save_directory = "saves";
    int editor_max_bytes = 1048576;
    int refresh_milliseconds = 100;
    int scroll_step = 5;
    int history_limit = 200;
    int notification_success_seconds = 8, notification_error_seconds = 15;
    int opening_text_width = 64;
    int people_panel_width = 34, people_wide_panel_width = 44;
    int network_panel_width = 30, map_label_width = 14;
    double uncertain_relationship_threshold = .8;

    std::array<ColorSetting, ColorRoleNames.size()> colors;
    std::array<KeyBinding, KeyActionNames.size()> keybindings;

    ProcessViewConfig process_view;
    UILayout layout;
    std::vector<std::string> page_order, hidden_pages;
    std::unordered_map<std::string, std::string> page_labels, labels;
    bool show_clock = true, show_resources = true, show_notifications = true, show_controls = true,
         show_tabs = true;
    // Presentation settings never grant gameplay capabilities or bypass locked pages.
    bool page_enabled(std::string_view id) const;
    std::string page_title(std::string_view id, std::string_view fallback) const;
    std::string label(std::string_view key, std::string_view fallback) const;
    int list_divider(int width, bool operations = false) const;
    // Menu keys use menu_ plus lowercase words joined with underscores.
    std::string menu_label(std::string_view text) const;

    // Check whether a configured action matches this ncurses key code.
    bool bound(KeyAction action, int key) const;
    // Return the human-readable configured shortcut labels for this action.
    std::string binding_label(KeyAction action) const;

    // Validate and write a settings change, returning the newly loaded configuration.
    static UISettings edit(const std::filesystem::path& path, bool keybindings,
                           const std::string& section, const std::string& key,
                           const std::string& literal);
    // Read authored TOML definitions and reject malformed values.
    static UISettings load(const std::filesystem::path& path);
};

} // namespace ai_sim::ui
