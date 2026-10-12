#include "ai_sim/ui/ProcessViewConfig.h"

#include "ai_sim/ProcessFields.h"

#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

namespace ai_sim::ui {
namespace {

void fail(const std::string& message) {
    throw std::runtime_error("process_view: " + message);
}

void validate_field(const std::string& id) {
    try {
        (void)process_field(id);
    } catch (const std::exception&) {
        fail("Unknown field: " + id);
    }
}

std::string text_setting(const toml::table& table, std::string_view key,
                         const std::string& fallback) {
    const auto* node = table.get(key);

    if (!node) {
        return fallback;
    }

    const auto value = node->value<std::string>();

    if (!value) {
        fail(std::string(key) + " must be a string");
    }

    return *value;
}

Alignment alignment_setting(const toml::table& table, std::string_view key, Alignment fallback) {
    const auto* node = table.get(key);

    if (!node) {
        return fallback;
    }

    const auto value = node->value<std::string>();

    if (!value) {
        fail(std::string(key) + " must be a string");
    }

    if (*value == "left") {
        return Alignment::Left;
    }

    if (*value == "center") {
        return Alignment::Center;
    }

    if (*value == "right") {
        return Alignment::Right;
    }

    fail(std::string(key) + " must be left, center, or right");
    return fallback;
}

std::vector<std::string> field_list(const toml::table& table, std::string_view key, bool compact) {
    const auto* array = table[key].as_array();

    if (!array) {
        fail(std::string(key) + " must be an array of field names");
    }

    std::vector<std::string> result;
    std::unordered_set<std::string> seen;

    for (const auto& node : *array) {
        const auto value = node.value<std::string>();

        if (!value) {
            fail(std::string(key) + " must contain only strings");
        }

        validate_field(*value);

        if (compact && *value == "id") {
            fail("ID is already pinned; remove id from columns");
        }

        if (!seen.insert(*value).second) {
            fail("Duplicate field in " + std::string(key) + ": " + *value);
        }

        result.push_back(*value);
    }

    return result;
}

} // namespace

int ProcessViewConfig::width(const std::string& field) const {
    const auto found = widths.find(field);

    if (found != widths.end()) {
        return found->second;
    }

    return process_field(field).width;
}

ProcessViewConfig ProcessViewConfig::load(const toml::table& document) {
    const auto* table = document["process_view"].as_table();

    if (!table) {
        fail("Missing [process_view] table");
    }

    const std::unordered_set<std::string> allowed = {
        "columns",          "expanded_fields",    "widths",      "expanded_columns",
        "column_alignment", "expanded_alignment", "sort_by",     "sort_direction",
        "filter_name",      "filter_owner",       "filter_state"};

    for (const auto& [key, node] : *table) {
        (void)node;

        const std::string name(key.str());

        if (!allowed.contains(name)) {
            fail("Unknown setting: " + name);
        }
    }

    ProcessViewConfig result;

    result.columns = field_list(*table, "columns", true);
    result.expanded_fields = field_list(*table, "expanded_fields", false);

    if (const auto* node = table->get("expanded_columns")) {
        const auto value = node->value<std::int64_t>();

        if (!node->is_integer() || !value || *value < 1 || *value > 6) {
            fail("expanded_columns must be an integer from 1 to 6");
        }

        result.expanded_columns = static_cast<int>(*value);
    }

    result.column_alignment = alignment_setting(*table, "column_alignment", Alignment::Left);

    result.expanded_alignment = alignment_setting(*table, "expanded_alignment", Alignment::Center);

    result.sort_by = text_setting(*table, "sort_by", "id");
    validate_field(result.sort_by);

    const auto direction = text_setting(*table, "sort_direction", "ascending");

    if (direction != "ascending" && direction != "descending") {
        fail("sort_direction must be ascending or descending");
    }

    result.sort_descending = direction == "descending";

    result.filter_name = text_setting(*table, "filter_name", "");
    result.filter_owner = text_setting(*table, "filter_owner", "");
    result.filter_state = text_setting(*table, "filter_state", "");

    if (const auto* node = table->get("widths")) {
        const auto* widths = node->as_table();

        if (!widths) {
            fail("widths must be a table");
        }

        for (const auto& [key, value_node] : *widths) {
            const std::string id(key.str());
            validate_field(id);

            const auto value = value_node.value<std::int64_t>();

            if (!value_node.is_integer() || !value || *value < 4 || *value > 120) {
                fail("Width for " + id + " must be an integer from 4 to 120");
            }

            result.widths.emplace(id, static_cast<int>(*value));
        }
    }

    return result;
}

} // namespace ai_sim::ui