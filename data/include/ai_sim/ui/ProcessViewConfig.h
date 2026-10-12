// Module: Validate process columns, filters, sorting and alignment from TOML.
#pragma once

#include <toml++/toml.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace ai_sim::ui {

enum class Alignment { Left, Center, Right };

struct ProcessViewConfig {
    std::vector<std::string> columns;
    std::vector<std::string> expanded_fields;
    std::unordered_map<std::string, int> widths;

    int expanded_columns = 2;

    Alignment column_alignment = Alignment::Left;
    Alignment expanded_alignment = Alignment::Center;

    std::string sort_by = "id";
    bool sort_descending = false;

    std::string filter_name;
    std::string filter_owner;
    std::string filter_state;

    // Return the configured width or the field catalog default, in terminal columns.
    int width(const std::string& field) const;

    // Read authored TOML definitions and reject malformed values.
    static ProcessViewConfig load(const toml::table& document);
};

} // namespace ai_sim::ui
