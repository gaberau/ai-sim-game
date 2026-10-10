#include "ai_sim/ui/TerminalUI.h"
#include <sstream>
#include <algorithm>
#include <ncurses.h>

namespace ai_sim::ui {
void TerminalUI::open_settings() {
    open_menu("Settings", {
                              {"Keybindings",
                               [this] {
                                   open_settings_section("keybindings", true);
                               },
                               {}},
                              {"Colors",
                               [this] {
                                   open_settings_section("colors");
                               },
                               {}},
                              {"Background colors",
                               [this] {
                                   open_settings_section("backgrounds");
                               },
                               {}},
                              {"Interface and maps",
                               [this] {
                                   open_settings_section("interface");
                               },
                               {}},
                              {"Process view",
                               [this] {
                                   open_settings_section("process_view");
                               },
                               {}},
                              {"Process column widths",
                               [this] {
                                   open_settings_section("process_view.widths");
                               },
                               {}},
                              {"Reload TOML settings",
                               [this] {
                                   run_action("reload");
                                   open_settings();
                               },
                               {}},
                              {"Return to game",
                               [] {
                               },
                               {}},
                          });
}

void TerminalUI::open_settings_section(const std::string& section, bool editing_keybindings) {
    auto document = toml::parse_file(
        (editing_keybindings ? settings_path_.parent_path() / "keybindings.toml" : settings_path_)
            .string());
    // Show defaulted keybindings and color roles too, so every role is editable.
    auto* section_table = &document;
    std::istringstream section_path(section);
    std::string section_name;
    while (std::getline(section_path, section_name, '.')) {
        if (!(*section_table)[section_name].is_table())
            section_table->insert_or_assign(section_name, toml::table{});
        section_table = (*section_table)[section_name].as_table();
    }
    auto& values = *section_table;
    if (editing_keybindings)
        for (std::size_t i = 0; i < KeyActionNames.size(); ++i) {
            if (values.contains(KeyActionNames[i]))
                continue;
            toml::array labels;
            for (const auto& label : settings_.keybindings[i].labels)
                labels.push_back(label);
            values.insert(KeyActionNames[i], std::move(labels));
        }
    if (section == "colors" || section == "backgrounds")
        for (std::size_t i = 0; i < ColorRoleNames.size(); ++i)
            if (!values.contains(ColorRoleNames[i]))
                values.insert(ColorRoleNames[i], section == "colors"
                                                     ? settings_.colors[i].foreground
                                                     : settings_.colors[i].background);
    std::vector<MenuAction> actions;
    for (const auto& [name, node] : values) {
        const std::string key{name.str()};
        std::ostringstream formatted;
        formatted << toml::table{{"value", node}};
        const auto assignment = formatted.str();
        const auto literal = assignment.substr(assignment.find('=') + 1);
        if (node.is_table())
            continue;
        actions.push_back(
            {key + " =" + literal,
             [this, section, key, editing_keybindings, literal] {
                 auto document = toml::parse("value = " + literal);
                 if (document["value"].is_boolean()) {
                     apply_settings_edit(section, key, false,
                                         document["value"].value_or(false) ? "false" : "true");
                 } else if (editing_keybindings) {
                     open_menu("Bind " + key,
                               {{"Press a key",
                                 [this, section, key, literal] {
                                     open_text(
                                         "Press a key for " + key,
                                         "Escape cancels. A successful capture replaces this "
                                         "action's keys.",
                                         [this, section, key](const std::string& value) {
                                             apply_settings_edit(section, key, true, value);
                                         },
                                         {},
                                         [this, section] {
                                             open_settings_section(section, true);
                                         });
                                     key_capture_active_ = true;
                                 },
                                 {}},
                                {"Edit TOML (multiple keys)",
                                 [this, section, key, literal] {
                                     edit_settings_value(section, key, true, literal);
                                 },
                                 {}}},
                               [this, section] {
                                   open_settings_section(section, true);
                               });
                 } else if (section == "colors" || section == "backgrounds") {
                     std::vector<MenuAction> palette;
                     for (const auto* color :
                          {"default", "black", "red", "green", "yellow", "blue", "magenta", "cyan",
                           "white", "gray", "bright_red", "bright_green", "bright_yellow",
                           "bright_blue", "bright_magenta", "bright_cyan", "bright_white",
                           "orange"}) {
                         palette.push_back({color,
                                            [this, section, key, color] {
                                                apply_settings_edit(section, key, false,
                                                                    "\"" + std::string(color) +
                                                                        "\"");
                                            },
                                            {}});
                     }
                     palette.push_back({"Custom palette index / TOML",
                                        [this, section, key, literal] {
                                            edit_settings_value(section, key, false, literal);
                                        },
                                        {}});
                     open_menu("Color | " + key, std::move(palette), [this, section] {
                         open_settings_section(section);
                     });
                 } else {
                     edit_settings_value(section, key, false, literal);
                 }
             },
             {},
             editing_keybindings ? "Choose key capture or edit multiple keys with TOML."
             : node.is_boolean() ? "Enter toggles this setting immediately."
             : section == "colors" || section == "backgrounds"
                 ? "Choose a named color or custom palette index."
                 : "Edit a TOML value; invalid values leave your settings unchanged."});
    }
    if (actions.empty())
        actions.push_back({"No settings defined", {}, "Add this section to settings.toml."});
    open_menu("Settings | " + section, std::move(actions), [this] {
        open_settings();
    });
}
void TerminalUI::edit_settings_value(const std::string& section, const std::string& key,
                                     bool editing_keybindings, const std::string& current_literal) {
    open_text(
        "Edit " + section + "." + key,
        "TOML value: quoted strings, true/false, numbers, or [\"key\", \"key\"].",
        [this, section, key, editing_keybindings](const std::string& literal) {
            apply_settings_edit(section, key, editing_keybindings, literal);
        },
        current_literal,
        [this, section, editing_keybindings] {
            open_settings_section(section, editing_keybindings);
        });
}
void TerminalUI::apply_settings_edit(const std::string& section, const std::string& key,
                                     bool editing_keybindings, const std::string& literal) {
    const auto updated =
        UISettings::edit(settings_path_, editing_keybindings, section, key, literal);
    settings_ = updated;
    saves_ = SaveSystem(settings_.save_directory);
    apply_settings();
    add_log("Settings saved and applied.", NoticeKind::Success);
    open_settings_section(section, editing_keybindings);
}

void TerminalUI::open_context_help() {
    std::vector<MenuAction> lines;
    auto explain = [&](const std::string& text) {
        const auto width = static_cast<std::size_t>(std::max(12, getmaxx(stdscr) - 8));
        std::istringstream words(text);
        std::string word, line;
        while (words >> word) {
            if (!line.empty() && line.size() + word.size() + 1 > width) {
                lines.push_back({line, {}, {}});
                line.clear();
            }
            if (!line.empty())
                line += ' ';
            line += word;
        }
        if (!line.empty())
            lines.push_back({line, {}, {}});
    };
    auto control = [&](KeyAction action, const std::string& text) {
        explain(settings_.binding_label(action) + ": " + text);
    };
    std::string title = "Help";
    if (editor_active_) {
        title += " | File editor";
        explain("Edit the working copy; save keeps changes, cancel discards them.");
        control(KeyAction::EditorSave, "keep edits");
        control(KeyAction::EditorCancel, "discard edits");
        control(KeyAction::EditorFocus, "switch editor pane");
    } else {
        const char* names[] = {"Chat", "Processes", "Activity", "Network", "Files", "News",
                               "AI",   "Dossiers",  "Web",      "Agents",  "Phone"};
        title += " | " + std::string(names[static_cast<int>(selected_tab_)]);
        switch (selected_tab_) {
        case Tab::Chat:
            explain("Choose a response at the bottom. Waiting advances to a pending reply.");
            explain("Browse history, then choose Back to latest to resume responding.");
            control(KeyAction::Submit, "send the selected response or open a contact");
            break;
        case Tab::Network:
            explain("Select a server to inspect its status and available actions.");
            explain("+ accessible; ! credentials needed; x offline/unreachable; * your AI.");
            explain("Highlighted lines connect to the selected server.");
            control(KeyAction::Submit, "server actions and map legend");
            break;
        case Tab::People:
            explain("Select a person to read their dossier and relationship evidence.");
            explain("Dotted relationships are uncertain; highlighted lines touch the selection.");
            control(KeyAction::ToggleExpanded, "expand dossier sections");
            control(KeyAction::Submit, "person actions and map legend");
            break;
        case Tab::Files:
            explain("Select a file or directory; actions reflect your current permissions.");
            control(KeyAction::Submit, "open, edit, or inspect sharing");
            break;
        case Tab::Processes:
            explain("Select a process to inspect it. Protected processes cannot be stopped.");
            control(KeyAction::ToggleExpanded, "expand process details");
            control(KeyAction::Submit, "available process actions");
            break;
        case Tab::Activity:
            explain("Resource activity reflects the current simulation state.");
            break;
        case Tab::News:
            explain("Select a headline, then focus its details to read the article.");
            break;
        case Tab::AI:
            explain("Inspect the AI's current capabilities and state.");
            explain("Actions -> Money-making tools: outside client jobs and recurring products.");
            explain("Mark must authorize self-modification first; each tool has its own unlocks.");
            break;
        case Tab::Web:
            explain("Follow bookmarks or enter an in-game website address.");
            explain("Sim Exchange offers stock quotes, trading, and your portfolio.");
            explain("The City Infrastructure Directory lists utility facilities and dependencies.");
            explain("Web Browsing research unlocks this view; trading requires its own unlocks.");
            control(KeyAction::Submit, "follow the selected link");
            break;
        case Tab::Agents:
            explain("Build agents in the self-modification workshop, then choose Run or Stop.");
            explain("Running agents use CPU and RAM; installed storage persists when stopped.");
            control(KeyAction::Submit, "open the workshop");
            break;
        case Tab::Phone:
            explain("Mark trust 30 and Phone Access research authenticate his phone.");
            explain("Text/2FA, GPS and battery telemetry require additional authored data.");
            break;
        case Tab::Count:
            break;
        }
        control(KeyAction::SelectPrevious, "previous selection");
        control(KeyAction::SelectNext, "next selection");
        control(KeyAction::FocusLeft, "focus the left pane");
        control(KeyAction::FocusRight, "focus the right pane");
        control(KeyAction::ScrollUp, "scroll up / older history");
        control(KeyAction::ScrollDown, "scroll down / newer history");
        control(KeyAction::NextTab, "next unlocked screen");
        control(KeyAction::Pause, "pause or resume simulation time");
    }
    explain("Escape: close help; Escape during play: settings.");
    control(KeyAction::GameMenu, "save, load, wait, and other game actions");
    lines.push_back({"Return to game",
                     [] {
                     },
                     {}});
    open_menu(title, std::move(lines));
}
} // namespace ai_sim::ui
