#include "ai_sim/ui/UISettings.h"
#include "ai_sim/TomlIO.h"
#include <ncurses.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace ai_sim;
using namespace ai_sim::ui;
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
int main(int argc, char** argv) {
    auto temp = std::filesystem::temp_directory_path() /
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    try {
        check(argc == 2, "Root required");
        const auto root = std::filesystem::path(argv[1]);
        auto settings = UISettings::load(root / "config/settings.toml");
        check(settings.bound(KeyAction::SelectPrevious, KEY_UP) &&
                  settings.bound(KeyAction::SelectNext, KEY_DOWN),
              "Arrows not enabled");
        check(settings.bound(KeyAction::FocusLeft, 'q') &&
                  settings.bound(KeyAction::FocusRight, 'e'),
              "Pane keys wrong");
        check(!settings.bound(KeyAction::NavigateUp, 'w') &&
                  !settings.bound(KeyAction::ToggleFocus, 'q'),
              "Stock WASD/toggle survived");
        check(settings.bound(KeyAction::ArchiveChat, 'a') &&
                  settings.bound(KeyAction::ToggleArchivedChats, 'v'),
              "Chat archive bindings missing");
        std::filesystem::create_directories(temp);
        std::filesystem::copy_file(root / "config/settings.toml", temp / "settings.toml");
        auto keys = toml::parse_file((root / "config/keybindings.toml").string());
        auto* bindings = keys["keybindings"].as_table();
        bindings->erase("focus_left");
        bindings->erase("focus_right");
        bindings->insert_or_assign("toggle_focus", toml::array{"q", "Q"});
        bindings->insert_or_assign("navigate_up", toml::array{"w", "W"});
        bindings->insert_or_assign("navigate_down", toml::array{"s", "S"});
        bindings->insert_or_assign("navigate_left", toml::array{"a", "A"});
        bindings->insert_or_assign("navigate_right", toml::array{"d", "D"});
        write_toml(temp / "keybindings.toml", keys);
        settings = UISettings::load(temp / "settings.toml");
        check(settings.bound(KeyAction::FocusLeft, 'q') &&
                  settings.bound(KeyAction::FocusRight, 'e') &&
                  !settings.bound(KeyAction::NavigateUp, 'w'),
              "Old stock controls not migrated");
        bindings->insert_or_assign("navigate_up", toml::array{"k"});
        bindings->insert_or_assign("focus_left", toml::array{"["});
        bindings->insert_or_assign("focus_right", toml::array{"]"});
        write_toml(temp / "keybindings.toml", keys);
        settings = UISettings::load(temp / "settings.toml");
        check(settings.bound(KeyAction::NavigateUp, 'k') &&
                  settings.bound(KeyAction::FocusLeft, '[') &&
                  settings.bound(KeyAction::FocusRight, ']'),
              "Custom TOML controls lost");
        settings = UISettings::edit(temp / "settings.toml", false, "interface",
                                    "notification_error_seconds", "25");
        check(settings.notification_error_seconds == 25, "Interface edit did not apply");
        settings =
            UISettings::edit(temp / "settings.toml", false, "colors", "map_locked", "\"yellow\"");
        check(settings.colors.back().foreground == 3, "Color edit did not apply");
        settings = UISettings::edit(temp / "settings.toml", true, "keybindings", "focus_right",
                                    "[\"r\", \"R\"]");
        check(settings.bound(KeyAction::FocusRight, 'r'), "Keybinding edit did not apply");
        settings =
            UISettings::edit(temp / "settings.toml", false, "process_view.widths", "id", "20");
        check(toml::parse_file((temp / "settings.toml").string())["process_view"]["widths"]["id"]
                      .value_or(0) == 20,
              "Nested TOML setting was not persisted");
        const auto unchanged = toml::parse_file((temp / "settings.toml").string());
        bool rejected = false;
        try {
            UISettings::edit(temp / "settings.toml", false, "interface", "refresh_milliseconds",
                             "0");
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected && toml::parse_file((temp / "settings.toml").string()) == unchanged,
              "Invalid edit replaced settings");
        rejected = false;
        try {
            UISettings::edit(temp / "settings.toml", true, "keybindings", "focus_right",
                             "[\"Up\"]");
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected &&
                  UISettings::load(temp / "settings.toml").bound(KeyAction::FocusRight, 'r'),
              "Conflicting edit replaced keybindings");
        check(std::filesystem::exists(temp / "settings.toml.bak") &&
                  std::filesystem::exists(temp / "keybindings.toml.bak"),
              "Settings backup missing");
        std::filesystem::remove_all(temp);
        std::cout << "Standard controls and migration tests passed\n";
    } catch (const std::exception& e) {
        std::filesystem::remove_all(temp);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
