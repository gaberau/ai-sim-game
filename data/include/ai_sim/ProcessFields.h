#pragma once

#include <toml++/toml.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ai_sim {

struct Process;
class Computer;

using ProcessValue =
    std::variant<std::string, std::int64_t, double, bool, std::vector<std::string>>;

struct ProcessData {
    // Future backends can supply live values.
    std::unordered_map<std::string, ProcessValue> runtime;

    // TOML supplies optional scenario snapshots.
    std::unordered_map<std::string, ProcessValue> scenario;
};

enum class FieldType { Text, Integer, Number, Boolean, List };

struct ProcessField {
    std::string id;
    std::string label;
    FieldType type;
    std::string unit;
    int width;

    // True when the existing simulation already supplies this field.
    bool live;
};

const std::vector<ProcessField>& process_fields();

const ProcessField& process_field(std::string_view id);

void load_process_data(Process& process, const toml::table& table);

std::string process_field_text(const Process& process, const Computer& computer,
                               double allocated_cores, std::string_view id);

} // namespace ai_sim