// Module: Load and edit validated presentation settings, colors and configurable keybindings.
#include "ai_sim/ui/UISettings.h"
#include "ai_sim/TomlIO.h"
#include <chrono>
#include <sstream>

#include <toml++/toml.hpp>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <ncurses.h>
#include <charconv>
#include <cctype>

namespace ai_sim::ui {
namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <class T>
T required(const toml::table& table, std::string_view key, const std::string& location) {
    const auto value = table[key].value<T>();

    if (!value) {
        throw std::runtime_error(location + "." + std::string(key) +
                                 ": missing field or incorrect type");
    }

    return *value;
}

int integer_setting(const toml::table& table, std::string_view key, int minimum, int maximum) {
    const auto value = required<std::int64_t>(table, key, "interface");

    require(value >= minimum && value <= maximum, "interface." + std::string(key) + ": expected " +
                                                      std::to_string(minimum) + "-" +
                                                      std::to_string(maximum));

    return static_cast<int>(value);
}

short parse_color(const toml::node& node, const std::string& location) {
    if (node.is_integer()) {
        const auto value = node.value<std::int64_t>();

        require(value && *value >= -1 && *value <= 255,
                location + ": palette index must be -1 through 255");

        return static_cast<short>(*value);
    }

    if (node.is_string()) {
        const auto name = node.value<std::string>();

        static const std::unordered_map<std::string, short> names = {
            {"default", -1},      {"black", 0},
            {"red", 1},           {"green", 2},
            {"yellow", 3},        {"blue", 4},
            {"magenta", 5},       {"cyan", 6},
            {"white", 7},         {"gray", 8},
            {"grey", 8},          {"bright_red", 9},
            {"bright_green", 10}, {"bright_yellow", 11},
            {"bright_blue", 12},  {"bright_magenta", 13},
            {"bright_cyan", 14},  {"bright_white", 15},
            {"orange", 208}};

        const auto found = names.find(*name);

        require(found != names.end(), location + ": unknown color '" + *name + "'");

        return found->second;
    }

    throw std::runtime_error(location + ": expected a color name or palette index");
}

bool known_role(std::string_view name) {
    return std::find(ColorRoleNames.begin(), ColorRoleNames.end(), name) != ColorRoleNames.end();
}

std::vector<int> parse_key(const std::string& name, const std::string& location) {
    static const std::unordered_map<std::string, std::vector<int>> names = {
        {"Tab", {9}},
        {"Shift+Tab", {KEY_BTAB}},
        {"Enter", {10, 13, KEY_ENTER}},
        {"Backspace", {8, 127, KEY_BACKSPACE}},
        {"Escape", {27}},
        {"Space", {32}},
        {"PageUp", {KEY_PPAGE}},
        {"PageDown", {KEY_NPAGE}},
        {"Up", {KEY_UP}},
        {"Down", {KEY_DOWN}},
        {"Left", {KEY_LEFT}},
        {"Right", {KEY_RIGHT}},
        {"Home", {KEY_HOME}},
        {"End", {KEY_END}},
        {"Delete", {KEY_DC}}};

    if (const auto found = names.find(name); found != names.end()) {
        return found->second;
    }

    // Control combinations: Ctrl+A through Ctrl+Z.
    if (name.size() == 6 && name.substr(0, 5) == "Ctrl+") {
        const char letter = name[5];

        if (letter >= 'A' && letter <= 'Z') {
            return {letter - 'A' + 1};
        }
    }

    // Function keys: F1 through F63.
    if (name.size() > 1 && name[0] == 'F') {
        int function_number = 0;

        const auto result =
            std::from_chars(name.data() + 1, name.data() + name.size(), function_number);

        if (result.ec == std::errc{} && result.ptr == name.data() + name.size() &&
            function_number >= 1 && function_number <= 63) {
            return {KEY_F(function_number)};
        }
    }

    // A single printable ASCII character can also be bound.
    if (name.size() == 1) {
        const auto character = static_cast<unsigned char>(name[0]);

        if (character >= 32 && character <= 126) {
            return {static_cast<int>(character)};
        }
    }

    throw std::runtime_error(location + ": unknown key '" + name + "'");
}

void load_keybindings(UISettings& settings, const toml::table& table) {
    for (const auto& [key, value] : table) {
        (void)value;

        require(std::find(KeyActionNames.begin(), KeyActionNames.end(), key.str()) !=
                    KeyActionNames.end(),
                "keybindings." + std::string(key.str()) + ": unknown action");
    }

    // Detect keys assigned to different actions.
    std::unordered_map<int, std::string> assigned;

    for (std::size_t i = 0; i < KeyActionNames.size(); ++i) {
        const auto action_name = KeyActionNames[i];

        const std::string location = "keybindings." + std::string(action_name);

        const auto* node = table.get(action_name);
        const auto* entries = node ? node->as_array() : nullptr;
        // New UI actions default for older keybinding files without replacing them.
        toml::array defaults;
        if (!node && action_name == "archive_chat") {
            defaults.push_back("a");
            entries = &defaults;
        }
        if (!node && action_name == "toggle_archived_chats") {
            defaults.push_back("v");
            entries = &defaults;
        }
        if (!node && action_name == "game_menu") {
            defaults.push_back("F1");
            entries = &defaults;
        }
        if (!node && action_name == "help") {
            defaults.push_back("F5");
            entries = &defaults;
        }
        if (!node && action_name == "back") {
            defaults.push_back("Escape");
            entries = &defaults;
        }
        if (action_name == "toggle_focus" &&
            (!node || (entries && *entries == toml::array{"q", "Q"})))
            entries = &defaults;
        if (!node && action_name == "focus_left") {
            defaults.push_back("q");
            defaults.push_back("Q");
            entries = &defaults;
        }
        if (!node && action_name == "focus_right") {
            defaults.push_back("e");
            defaults.push_back("E");
            entries = &defaults;
        }
        if (!node && action_name == "editor_focus") {
            defaults.push_back("Ctrl+Q");
            entries = &defaults;
        }
        if (action_name == "navigate_up" &&
            (!node || (entries && *entries == toml::array{"w", "W"})))
            entries = &defaults;
        if (action_name == "navigate_down" &&
            (!node || (entries && *entries == toml::array{"s", "S"})))
            entries = &defaults;
        if (action_name == "navigate_left" &&
            (!node || (entries && *entries == toml::array{"a", "A"})))
            entries = &defaults;
        if (action_name == "navigate_right" &&
            (!node || (entries && *entries == toml::array{"d", "D"})))
            entries = &defaults;

        require(entries != nullptr, location + ": expected an array of key names");

        if (action_name == "submit" || action_name == "select_previous" ||
            action_name == "select_next")
            require(!entries->empty(), location + ": at least one key is required");
        auto& binding = settings.keybindings[i];

        for (const auto& entry : *entries) {
            require(entry.is_string(), location + ": every key must be a string");

            const auto name = entry.value<std::string>();

            const auto codes = parse_key(*name, location);
            if (!node && i >= static_cast<std::size_t>(KeyAction::ToggleFocus) &&
                std::any_of(codes.begin(), codes.end(), [&](int code) {
                    return assigned.contains(code);
                }))
                continue;
            binding.labels.push_back(*name);

            for (const int code : codes) {
                require(code != 27 || action_name == "back", "Escape is reserved for menu/back");
                const auto found = assigned.find(code);

                if (found != assigned.end()) {
                    require(found->second == action_name, location + ": key '" + *name +
                                                              "' conflicts with keybindings." +
                                                              found->second);
                } else {
                    assigned.emplace(code, std::string(action_name));
                }

                if (std::find(binding.codes.begin(), binding.codes.end(), code) ==
                    binding.codes.end()) {
                    binding.codes.push_back(code);
                }
            }
        }
    }
}

} // namespace

bool UISettings::page_enabled(std::string_view id) const {
    return std::find(hidden_pages.begin(), hidden_pages.end(), id) == hidden_pages.end();
}
std::string UISettings::page_title(std::string_view id, std::string_view fallback) const {
    auto it = page_labels.find(std::string(id));
    return it == page_labels.end() ? std::string(fallback) : it->second;
}
std::string UISettings::label(std::string_view key, std::string_view fallback) const {
    auto it = labels.find(std::string(key));
    return it == labels.end() ? std::string(fallback) : it->second;
}
std::string UISettings::menu_label(std::string_view text) const {
    std::string key = "menu_";
    bool separator = false;
    for (unsigned char ch : text) {
        if (std::isalnum(ch)) {
            if (separator && key.size() > 5)
                key += '_';
            key += static_cast<char>(std::tolower(ch));
            separator = false;
        } else
            separator = true;
    }
    return label(key, text);
}
int UISettings::list_divider(int width, bool operations) const {
    return std::clamp(
        width * (operations ? layout.operations_width_percent : layout.list_width_percent) / 100,
        operations ? layout.operations_min_width : layout.list_min_width,
        operations ? layout.operations_max_width : layout.list_max_width);
}

UISettings UISettings::load(const std::filesystem::path& path) {
    try {
        const auto data = toml::parse_file(path.string());

        require(required<std::int64_t>(data, "schema_version", "root") == 1,
                "schema_version: expected 1");

        const auto* interface = data["interface"].as_table();
        const auto* colors = data["colors"].as_table();

        require(interface && colors, "Expected [interface] and [colors] tables");

        const toml::table* backgrounds = nullptr;

        if (data.contains("backgrounds")) {
            backgrounds = data["backgrounds"].as_table();

            require(backgrounds != nullptr, "backgrounds: expected a table");
        }

        UISettings settings;
        const auto optional_table = [&](const char* name) -> const toml::table* {
            const auto* node = data.get(name);
            require(!node || node->is_table(), std::string(name) + ": expected table");
            return node ? node->as_table() : nullptr;
        };
        if (const auto* layout = optional_table("layout")) {
            struct Field {
                const char* name;
                int* value;
                int low, high;
            };
            const Field fields[] = {
                {"workspace_top_gap", &settings.layout.workspace_top_gap, 0, 4},
                {"workspace_bottom_gap", &settings.layout.workspace_bottom_gap, 0, 4},
                {"text_margin", &settings.layout.text_margin, 1, 4},
                {"pane_padding", &settings.layout.pane_padding, 1, 5},
                {"response_gap", &settings.layout.response_gap, 0, 3},
                {"response_heading_gap", &settings.layout.response_heading_gap, 0, 3},
                {"response_height_percent", &settings.layout.response_height_percent, 25, 70},
                {"list_width_percent", &settings.layout.list_width_percent, 20, 60},
                {"list_min_width", &settings.layout.list_min_width, 20, 60},
                {"list_max_width", &settings.layout.list_max_width, 20, 100},
                {"operations_width_percent", &settings.layout.operations_width_percent, 20, 60},
                {"operations_min_width", &settings.layout.operations_min_width, 20, 60},
                {"operations_max_width", &settings.layout.operations_max_width, 20, 100},
                {"minimum_columns", &settings.layout.minimum_columns, 80, 240},
                {"minimum_rows", &settings.layout.minimum_rows, 24, 80},
                {"footer_context_rows", &settings.layout.footer_context_rows, 1, 3},
                {"dialog_max_width", &settings.layout.dialog_max_width, 40, 180},
                {"dialog_max_height", &settings.layout.dialog_max_height, 12, 50},
                {"news_width_percent", &settings.layout.news_width_percent, 25, 60},
                {"email_editor_width_percent", &settings.layout.email_editor_width_percent, 30,
                 70}};
            for (const auto& [name, node] : *layout) {
                (void)node;
                auto field = std::find_if(std::begin(fields), std::end(fields), [&](const auto& f) {
                    return name.str() == f.name;
                });
                require(field != std::end(fields) && node.is_integer(),
                        "layout." + std::string(name.str()) + ": unknown setting");
                *field->value = integer_setting(*layout, field->name, field->low, field->high);
            }
            require(settings.layout.list_min_width <= settings.layout.list_max_width &&
                        settings.layout.operations_min_width <=
                            settings.layout.operations_max_width,
                    "layout: minimum pane width exceeds maximum");
            require(settings.layout.minimum_columns >=
                            settings.layout.list_max_width + settings.layout.pane_padding + 30 &&
                        settings.layout.minimum_columns >= settings.layout.operations_max_width +
                                                               settings.layout.pane_padding + 30,
                    "layout: terminal width cannot fit configured panes");
            require(settings.layout.minimum_rows - settings.layout.workspace_top_gap -
                            settings.layout.workspace_bottom_gap -
                            settings.layout.footer_context_rows >=
                        20,
                    "layout: margins leave insufficient workspace height");
        }
        const auto known_page = [](std::string_view id) {
            return std::find(WorkspaceIds.begin(), WorkspaceIds.end(), id) != WorkspaceIds.end();
        };
        if (const auto* pages = optional_table("pages")) {
            for (const auto& [key, node] : *pages) {
                (void)node;
                require(key.str() == "order" || key.str() == "hidden" || key.str() == "labels",
                        "pages: unknown setting " + std::string(key.str()));
            }
            for (const auto* key : {"order", "hidden"})
                if (const auto* node = pages->get(key)) {
                    const auto* array = node->as_array();
                    require(array, std::string("pages.") + key + ": expected array");
                    auto& target = std::string_view(key) == "order" ? settings.page_order
                                                                    : settings.hidden_pages;
                    for (const auto& item : *array) {
                        const auto id = item.value<std::string>();
                        require(id && known_page(*id),
                                std::string("pages.") + key + ": unknown page ID");
                        require(std::find(target.begin(), target.end(), *id) == target.end(),
                                std::string("pages.") + key + ": duplicate page ID");
                        target.push_back(*id);
                    }
                }
            require(settings.page_enabled("messages"),
                    "pages.hidden: Messages must remain available for progression");
            if (const auto* node = pages->get("labels")) {
                const auto* labels = node->as_table();
                require(labels, "pages.labels: expected table");
                for (const auto& [key, value] : *labels) {
                    const auto title = value.value<std::string>();
                    require(known_page(key.str()) && title && !title->empty() &&
                                title->size() <= 24 &&
                                title->find_first_of("\r\n\t") == std::string::npos,
                            "pages.labels: invalid page ID or title");
                    settings.page_labels.emplace(key.str(), *title);
                }
            }
        }
        if (const auto* chrome = optional_table("chrome")) {
            const std::pair<std::string_view, bool*> fields[] = {
                {"clock", &settings.show_clock},
                {"resources", &settings.show_resources},
                {"notifications", &settings.show_notifications},
                {"controls", &settings.show_controls},
                {"tabs", &settings.show_tabs}};
            for (const auto& [key, node] : *chrome) {
                auto field = std::find_if(std::begin(fields), std::end(fields), [&](const auto& f) {
                    return f.first == key.str();
                });
                const auto value = node.value<bool>();
                require(field != std::end(fields) && node.is_boolean() && value,
                        "chrome: unknown setting or non-boolean value");
                *field->second = *value;
            }
        }
        if (const auto* labels = optional_table("labels")) {
            for (const auto& [key, node] : *labels) {
                const auto value = node.value<std::string>();
                require(value && !value->empty() && value->size() <= 160 &&
                            value->find_first_of("\r\n\t") == std::string::npos,
                        "labels." + std::string(key.str()) + ": expected one nonempty line");
                settings.labels.emplace(key.str(), *value);
            }
        }

        if (const auto* cursor = interface->get("software_cursor")) {
            const auto value = cursor->value<bool>();
            require(value.has_value(), "interface.software_cursor: expected bool");
            settings.software_cursor = *value;
        }
        if (const auto* redraw = interface->get("full_redraw")) {
            const auto value = redraw->value<bool>();
            require(value.has_value(), "interface.full_redraw: expected bool");
            settings.full_redraw = *value;
        }
        if (const auto* directory = interface->get("save_directory")) {
            const auto value = directory->value<std::string>();
            require(value && !value->empty(), "interface.save_directory: expected a nonempty path");
            settings.save_directory = *value;
        }
        if (interface->contains("editor_max_bytes")) {
            settings.editor_max_bytes =
                integer_setting(*interface, "editor_max_bytes", 1, 67108864);
        }
        if (const auto* autosave = interface->get("autosave_on_quit")) {
            const auto value = autosave->value<bool>();
            require(value.has_value(), "interface.autosave_on_quit: expected bool");
            settings.autosave_on_quit = *value;
        }

        const auto optional_integer = [&](const char* key, int& value, int low, int high) {
            if (interface->contains(key))
                value = integer_setting(*interface, key, low, high);
        };
        optional_integer("notification_success_seconds", settings.notification_success_seconds, 1,
                         120);
        optional_integer("notification_error_seconds", settings.notification_error_seconds, 1, 120);
        optional_integer("opening_text_width", settings.opening_text_width, 20, 200);
        optional_integer("people_panel_width", settings.people_panel_width, 20, 100);
        optional_integer("people_wide_panel_width", settings.people_wide_panel_width, 20, 100);
        optional_integer("network_panel_width", settings.network_panel_width, 20, 100);
        optional_integer("map_label_width", settings.map_label_width, 4, 60);
        if (interface->contains("uncertain_relationship_threshold")) {
            const auto value = (*interface)["uncertain_relationship_threshold"].value<double>();
            require(value && *value >= 0 && *value <= 1,
                    "interface.uncertain_relationship_threshold: expected 0-1");
            settings.uncertain_relationship_threshold = *value;
        }

        settings.refresh_milliseconds =
            integer_setting(*interface, "refresh_milliseconds", 10, 1000);

        settings.scroll_step = integer_setting(*interface, "scroll_step", 1, 1000);

        settings.history_limit = integer_setting(*interface, "history_limit", 6, 10000);

        // Older settings files may still contain this retired Activity-page key.
        if (interface->contains("activity_bar_width"))
            (void)integer_setting(*interface, "activity_bar_width", 1, 32);

        for (const auto& [key, value] : *colors) {
            (void)value;

            require(key.str() == "background" || known_role(key.str()),
                    "colors." + std::string(key.str()) + ": unknown color role");
        }

        if (backgrounds) {
            for (const auto& [key, value] : *backgrounds) {
                (void)value;

                require(known_role(key.str()),
                        "backgrounds." + std::string(key.str()) + ": unknown color role");
            }
        }

        const auto* background_node = colors->get("background");

        require(background_node != nullptr, "colors.background: missing field");

        const short default_background = parse_color(*background_node, "colors.background");

        for (std::size_t i = 0; i < ColorRoleNames.size(); ++i) {
            const auto role = ColorRoleNames[i];
            const auto* foreground_node = colors->get(role);

            require(foreground_node != nullptr || i >= 13,
                    "colors." + std::string(role) + ": missing field");

            const short file_defaults[] = {2, 6, 208, 1, 8, 3};
            settings.colors[i].foreground =
                foreground_node ? parse_color(*foreground_node, "colors." + std::string(role))
                                : file_defaults[i - 13];

            settings.colors[i].background = default_background;

            if (backgrounds) {
                if (const auto* node = backgrounds->get(role)) {
                    settings.colors[i].background =
                        parse_color(*node, "backgrounds." + std::string(role));
                }
            }
        }
        const auto binding_data =
            toml::parse_file((path.parent_path() / "keybindings.toml").string());
        require(required<std::int64_t>(binding_data, "schema_version", "keybindings") == 1,
                "keybindings.toml: expected schema_version 1");
        const auto* keybindings = binding_data["keybindings"].as_table();

        require(keybindings != nullptr, "Expected a [keybindings] table");

        load_keybindings(settings, *keybindings);
        settings.process_view = ProcessViewConfig::load(data);
        return settings;
    } catch (const std::exception& error) {
        // Staged edits have long temporary paths; keep the actionable error readable.
        throw std::runtime_error(path.filename().string() + ": " + error.what());
    }
}
bool UISettings::bound(KeyAction action, int key) const {
    const auto& binding = keybindings[static_cast<std::size_t>(action)];

    return std::find(binding.codes.begin(), binding.codes.end(), key) != binding.codes.end();
}

std::string UISettings::binding_label(KeyAction action) const {
    const auto& binding = keybindings[static_cast<std::size_t>(action)];

    if (binding.labels.empty()) {
        return "unbound";
    }

    // Display the primary binding; additional aliases still work.
    return binding.labels.front();
}
} // namespace ai_sim::ui

namespace ai_sim::ui {
UISettings UISettings::edit(const std::filesystem::path& path, bool editing_keybindings,
                            const std::string& section, const std::string& key,
                            const std::string& literal) {
    const auto parsed = toml::parse("value = " + literal);
    if (parsed.size() != 1 || !parsed.contains("value"))
        throw std::runtime_error("Enter a single TOML value");
    auto settings = toml::parse_file(path.string());
    auto bindings = toml::parse_file((path.parent_path() / "keybindings.toml").string());
    auto& edited_document = editing_keybindings ? bindings : settings;
    auto* section_table = &edited_document;
    std::istringstream section_path(section);
    std::string section_name;
    while (std::getline(section_path, section_name, '.')) {
        if (!(*section_table)[section_name].is_table())
            section_table->insert_or_assign(section_name, toml::table{});
        section_table = (*section_table)[section_name].as_table();
    }
    section_table->insert_or_assign(key, *parsed.get("value"));
    const auto staging_directory =
        path.parent_path() /
        (".settings-edit-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!std::filesystem::create_directory(staging_directory))
        throw std::runtime_error("Cannot stage settings");
    try {
        write_toml(staging_directory / path.filename(), settings);
        write_toml(staging_directory / "keybindings.toml", bindings);
        const auto validated_settings = load(staging_directory / path.filename());
        const auto destination =
            editing_keybindings ? path.parent_path() / "keybindings.toml" : path;
        const auto backup = std::filesystem::path{destination.string() + ".bak"};
        if (!std::filesystem::exists(backup))
            std::filesystem::copy_file(destination, backup);
        std::filesystem::rename(staging_directory / destination.filename(), destination);
        sync_directory(path.parent_path());
        std::filesystem::remove_all(staging_directory);
        return validated_settings;
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove_all(staging_directory, ignored);
        throw;
    }
}
} // namespace ai_sim::ui
