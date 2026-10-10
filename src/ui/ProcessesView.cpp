#include "ai_sim/ui/TerminalUI.h"

#include "ai_sim/ProcessFields.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

namespace ai_sim::ui {
namespace {

std::string lowercase(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    }

    return value;
}

bool contains_text(
    const std::string& value,
    const std::string& query)
{
    return query.empty()
        || lowercase(value).find(lowercase(query))
            != std::string::npos;
}

std::string cell(
    std::string value,
    int width,
    Alignment alignment)
{
    if (width <= 0) {
        return "";
    }

    for (char& character : value) {
        const auto byte = static_cast<unsigned char>(character);

        if (byte < 32 || byte == 127) {
            character = ' ';
        }
    }

    const auto size = static_cast<std::size_t>(width);

    if (value.size() > size) {
        value = value.substr(0, size - 1) + "~";
    }

    const auto padding = size - value.size();

    std::size_t left = 0;

    if (alignment == Alignment::Center) {
        left = padding / 2;
    }
    else if (alignment == Alignment::Right) {
        left = padding;
    }

    return std::string(left, ' ')
        + value
        + std::string(padding - left, ' ');
}

std::vector<std::string> wrap(
    std::string value,
    int width)
{
    std::vector<std::string> lines;

    for (char& character : value) {
        const auto byte = static_cast<unsigned char>(character);

        if (byte < 32 || byte == 127) {
            character = ' ';
        }
    }

    const auto line_width = static_cast<std::size_t>(
        std::max(1, width));

    while (!value.empty()) {
        if (value.size() <= line_width) {
            lines.push_back(value);
            break;
        }

        auto cut = value.rfind(' ', line_width);

        if (cut == std::string::npos || cut == 0) {
            cut = line_width;
        }

        lines.push_back(value.substr(0, cut));
        value.erase(0, cut);

        const auto first = value.find_first_not_of(' ');

        if (first == std::string::npos) {
            value.clear();
        }
        else {
            value.erase(0, first);
        }
    }

    if (lines.empty()) {
        lines.emplace_back();
    }

    return lines;
}

const ProcessValue* stored_value(
    const Process& process,
    const std::string& field)
{
    const auto runtime = process.data.runtime.find(field);

    if (runtime != process.data.runtime.end()) {
        return &runtime->second;
    }

    const auto scenario = process.data.scenario.find(field);

    if (scenario != process.data.scenario.end()) {
        return &scenario->second;
    }

    return nullptr;
}

// Use underlying numbers, not formatted strings such as "4.00 GiB".
std::optional<long double> numeric_value(
    const Process& process,
    const Computer& computer,
    double cpu_used,
    const std::string& field)
{
    if (field == "started_at") {
        return process.started_at_seconds;
    }

    if (field == "stopped_at") {
        if (process.stopped_at_seconds) {
            return *process.stopped_at_seconds;
        }

        return std::nullopt;
    }

    if (field == "uptime") {
        const double end = process.stopped_at_seconds.value_or(
            computer.time_seconds());

        return std::max(0.0, end - process.started_at_seconds);
    }

    if (field == "remaining_work") {
        if (process.remaining_cpu_seconds < 0.0) {
            return std::nullopt;
        }

        return process.remaining_cpu_seconds;
    }

    if (field == "cpu_demand") {
        return computer.current_cpu_demand(process);
    }

    if (field == "cpu_used") {
        return cpu_used;
    }

    if (field == "cpu_seconds") {
        return process.consumed_cpu_seconds;
    }

    if (field == "ram_used") {
        return process.running
            ? static_cast<long double>(process.memory_bytes)
            : 0.0L;
    }

    const auto* value = stored_value(process, field);

    if (!value) {
        return std::nullopt;
    }

    if (const auto* integer = std::get_if<std::int64_t>(value)) {
        return static_cast<long double>(*integer);
    }

    if (const auto* number = std::get_if<double>(value)) {
        return static_cast<long double>(*number);
    }

    return std::nullopt;
}

std::optional<std::string> text_value(
    const Process& process,
    const Computer& computer,
    double cpu_used,
    const std::string& field)
{
    if (process_field(field).live) {
        return process_field_text(
            process, computer, cpu_used, field);
    }

    const auto* value = stored_value(process, field);

    if (!value) {
        return std::nullopt;
    }

    if (const auto* text = std::get_if<std::string>(value)) {
        return *text;
    }

    if (const auto* boolean = std::get_if<bool>(value)) {
        return *boolean ? "yes" : "no";
    }

    if (const auto* list =
        std::get_if<std::vector<std::string>>(value))
    {
        std::string result;

        for (const auto& item : *list) {
            if (!result.empty()) {
                result += ", ";
            }

            result += item;
        }

        return result;
    }

    return std::nullopt;
}

template<class T>
int compare_optional(
    const std::optional<T>& left,
    const std::optional<T>& right,
    bool descending)
{
    // Missing values stay last in both sort directions.
    if (!left || !right) {
        if (left) return -1;
        if (right) return 1;
        return 0;
    }

    int result = 0;

    if (*left < *right) result = -1;
    else if (*right < *left) result = 1;

    return descending ? -result : result;
}

std::vector<std::size_t> visible_processes(
    const Computer& computer,
    const ProcessViewConfig& layout,
    const std::vector<double>& rates)
{
    const auto& processes = computer.processes();
    std::vector<std::size_t> result;

    for (std::size_t i = 0; i < processes.size(); ++i) {
        const auto& process = processes[i];

        const auto name = text_value(
            process, computer, rates[i], "name").value_or("");

        // Name searches also match the always-visible process ID.
        const bool name_matches =
            layout.filter_name.empty()
            || contains_text(name, layout.filter_name)
            || contains_text(process.id, layout.filter_name);

        if (!name_matches) {
            continue;
        }

        if (!contains_text(process.owner, layout.filter_owner)) {
            continue;
        }

        const auto state = process_field_text(
            process, computer, rates[i], "state");

        // State uses an exact, case-insensitive match.
        if (!layout.filter_state.empty()
            && lowercase(state) != lowercase(layout.filter_state))
        {
            continue;
        }

        result.push_back(i);
    }

    const auto type = process_field(layout.sort_by).type;
    const bool numeric =
        type == FieldType::Integer || type == FieldType::Number;

    std::sort(
        result.begin(),
        result.end(),
        [&](std::size_t left, std::size_t right) {
            int comparison;

            if (numeric) {
                comparison = compare_optional(
                    numeric_value(
                        processes[left], computer,
                        rates[left], layout.sort_by),
                    numeric_value(
                        processes[right], computer,
                        rates[right], layout.sort_by),
                    layout.sort_descending);
            }
            else {
                auto left_value = text_value(
                    processes[left], computer,
                    rates[left], layout.sort_by);

                auto right_value = text_value(
                    processes[right], computer,
                    rates[right], layout.sort_by);

                if (left_value) {
                    *left_value = lowercase(*left_value);
                }

                if (right_value) {
                    *right_value = lowercase(*right_value);
                }

                comparison = compare_optional(
                    left_value, right_value,
                    layout.sort_descending);
            }

            if (comparison != 0) {
                return comparison < 0;
            }

            // Stable ordering for equal values.
            return processes[left].id < processes[right].id;
        });

    return result;
}

} // namespace

void TerminalUI::select_process(int direction) {
    const auto& processes = computer_.processes();
    const auto rates = computer_.cpu_allocations();

    const auto visible = visible_processes(
        computer_, settings_.process_view, rates);

    if (visible.empty()) {
        return;
    }

    const auto found = std::find_if(
        visible.begin(),
        visible.end(),
        [&](std::size_t index) {
            return processes[index].id == selected_process_id_;
        });

    if (found == visible.end()) {
        selected_process_id_ = processes[visible.front()].id;
    }
    else {
        const int position = std::clamp(
            static_cast<int>(found - visible.begin()) + direction,
            0,
            static_cast<int>(visible.size()) - 1);

        selected_process_id_ = processes[visible[position]].id;
    }

    reveal_selection_ = true;
}

void TerminalUI::toggle_process() {
    const auto& processes = computer_.processes();
    const auto rates = computer_.cpu_allocations();

    const auto visible = visible_processes(
        computer_, settings_.process_view, rates);

    if (visible.empty()) {
        return;
    }

    const auto found = std::find_if(
        visible.begin(),
        visible.end(),
        [&](std::size_t index) {
            return processes[index].id == selected_process_id_;
        });

    if (found == visible.end()) {
        selected_process_id_ = processes[visible.front()].id;
    }

    if (!expanded_processes_.erase(selected_process_id_)) {
        expanded_processes_.insert(selected_process_id_);
    }

    reveal_selection_ = true;
}

void TerminalUI::scroll_process_columns(int direction) {
    process_horizontal_offset_ = std::max(
        0,
        process_horizontal_offset_
            + direction * settings_.scroll_step);
}

void TerminalUI::draw_processes(int top, int bottom, int width) {
    const auto& processes = computer_.processes();
    const auto& layout = settings_.process_view;
    const auto rates = computer_.cpu_allocations();

    const auto visible = visible_processes(
        computer_, layout, rates);

    process_display_row_count_ = 0;

    if (visible.empty()) {
        process_offset_ = 0;

        write(
            top, 1,
            processes.empty()
                ? "No processes."
                : "No processes match the filters.",
            Heading);

        write(bottom, 1, "Edit settings.toml, then type reload.", Footer);
        return;
    }

    const auto selected = std::find_if(
        visible.begin(),
        visible.end(),
        [&](std::size_t index) {
            return processes[index].id == selected_process_id_;
        });

    if (selected == visible.end()) {
        selected_process_id_ = processes[visible.front()].id;
        reveal_selection_ = true;
    }

    const int id_width = layout.width("id");
    const int pinned_width = id_width + 6;

    const int available_width =
        std::max(0, width - pinned_width - 2);

    const int detail_width = std::max(1, width - 8);

    const int detail_columns = std::max(
        1,
        std::min(
            layout.expanded_columns,
            std::max(1, (detail_width + 3) / 27)));

    const int detail_cell_width = std::max(
        1,
        (detail_width - (detail_columns - 1) * 3)
            / detail_columns);

    struct DisplayRow {
        std::size_t process;
        bool detail;
        std::string text;
    };

    std::vector<DisplayRow> rows;
    std::string header;

    for (const auto& id : layout.columns) {
        header += cell(
            process_field(id).label,
            layout.width(id),
            layout.column_alignment) + " ";
    }

    int selected_row = 0;
    int longest_compact = static_cast<int>(header.size());

    for (const auto i : visible) {
        const auto& process = processes[i];

        if (process.id == selected_process_id_) {
            selected_row = static_cast<int>(rows.size());
        }

        std::string compact;

        for (const auto& id : layout.columns) {
            compact += cell(
                process_field_text(process, computer_, rates[i], id),
                layout.width(id),
                layout.column_alignment) + " ";
        }

        longest_compact = std::max(
            longest_compact,
            static_cast<int>(compact.size()));

        rows.push_back({i, false, std::move(compact)});

        if (!expanded_processes_.contains(process.id)) {
            continue;
        }

        for (
            std::size_t first = 0;
            first < layout.expanded_fields.size();
            first += static_cast<std::size_t>(detail_columns))
        {
            std::vector<std::vector<std::string>> cells;
            std::size_t block_height = 1;

            for (int column = 0; column < detail_columns; ++column) {
                const auto index =
                    first + static_cast<std::size_t>(column);

                if (index >= layout.expanded_fields.size()) {
                    cells.emplace_back();
                    continue;
                }

                const auto& id = layout.expanded_fields[index];

                auto lines = wrap(
                    process_field(id).label + ": "
                        + process_field_text(
                            process, computer_, rates[i], id),
                    detail_cell_width);

                block_height = std::max(block_height, lines.size());
                cells.push_back(std::move(lines));
            }

            for (std::size_t line = 0; line < block_height; ++line) {
                std::string grid_row;

                for (int column = 0; column < detail_columns; ++column) {
                    if (column > 0) {
                        grid_row += " | ";
                    }

                    const auto& lines = cells[column];

                    grid_row += cell(
                        line < lines.size() ? lines[line] : "",
                        detail_cell_width,
                        layout.expanded_alignment);
                }

                rows.push_back({i, true, std::move(grid_row)});
            }
        }
    }

    process_display_row_count_ = static_cast<int>(rows.size());

    process_horizontal_offset_ = std::clamp(
        process_horizontal_offset_,
        0,
        std::max(0, longest_compact - available_width));

    const auto shifted = [&](const std::string& value) {
        return value.substr(
            std::min(
                value.size(),
                static_cast<std::size_t>(process_horizontal_offset_)));
    };

    write(
        top, 1,
        "      "
            + cell("ID", id_width, layout.column_alignment)
            + shifted(header),
        Heading,
        true);

    const int visible_rows = std::max(0, bottom - top - 1);

    if (reveal_selection_ && visible_rows > 0) {
        if (selected_row < process_offset_) {
            process_offset_ = selected_row;
        }
        else if (selected_row >= process_offset_ + visible_rows) {
            process_offset_ = selected_row - visible_rows + 1;
        }

        reveal_selection_ = false;
    }

    process_offset_ = std::clamp(
        process_offset_,
        0,
        std::max(0, process_display_row_count_ - visible_rows));

    for (int line = 0; line < visible_rows; ++line) {
        const int index = process_offset_ + line;

        if (index >= process_display_row_count_) {
            break;
        }

        const auto& row = rows[index];
        const auto& process = processes[row.process];

        const bool is_selected = process.id == selected_process_id_;

        const int color =
            process.ai_owned ? Player
            : computer_.can_stop(process) ? Stoppable
            : Protected;

        std::string value;

        if (row.detail) {
            value = "      " + row.text;
        }
        else {
            value =
                std::string(is_selected ? "> " : "  ")
                + (expanded_processes_.contains(process.id)
                    ? "[-] "
                    : "[+] ")
                + cell(
                    process.id, id_width, layout.column_alignment)
                + shifted(row.text);
        }

        write(
            top + 1 + line,
            1,
            value,
            color,
            is_selected && !row.detail,
            !process.running);
    }

    const bool filtered =
        !layout.filter_name.empty()
        || !layout.filter_owner.empty()
        || !layout.filter_state.empty();

    write(
        bottom, 1,
        std::to_string(visible.size())
            + "/" + std::to_string(processes.size())
            + " processes | Sort: " + layout.sort_by
            + (layout.sort_descending ? " desc" : " asc")
            + (filtered ? " | FILTERED" : "")
            + " | "
            + settings_.binding_label(KeyAction::ToggleExpanded)
            + ": expand | [S]: scenario",
        Footer);
}

} // namespace ai_sim::ui