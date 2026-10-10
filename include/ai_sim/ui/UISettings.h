#pragma once

#include "ai_sim/ui/ProcessViewConfig.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

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
                      "help"};

struct ColorSetting {
    short foreground = 7;
    short background = -1;
};

struct KeyBinding {
    std::vector<int> codes;
    std::vector<std::string> labels;
};

struct UISettings {
    bool autosave_on_quit = true;
    bool full_redraw = true;
    bool software_cursor = true;
    std::filesystem::path save_directory = "saves";
    int editor_max_bytes = 1048576;
    int refresh_milliseconds = 100;
    int scroll_step = 5;
    int history_limit = 200;
    int activity_bar_width = 8;
    int notification_success_seconds = 8, notification_error_seconds = 15;
    int opening_text_width = 64;
    int people_panel_width = 34, people_wide_panel_width = 44;
    int network_panel_width = 30, map_label_width = 14;
    double uncertain_relationship_threshold = .8;

    std::array<ColorSetting, ColorRoleNames.size()> colors;
    std::array<KeyBinding, KeyActionNames.size()> keybindings;

    ProcessViewConfig process_view;

    bool bound(KeyAction action, int key) const;
    std::string binding_label(KeyAction action) const;

    static UISettings edit(const std::filesystem::path& path, bool keybindings,
                           const std::string& section, const std::string& key,
                           const std::string& literal);
    static UISettings load(const std::filesystem::path& path);
};

} // namespace ai_sim::ui
