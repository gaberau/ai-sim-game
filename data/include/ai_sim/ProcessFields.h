// Module: Expose typed live/scenario process fields to configurable views without mutating
// processes.
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

// Borrow the supported field catalog used by TOML column validation.
const std::vector<ProcessField>& process_fields();

// Resolve a supported field ID or reject an unknown authored name.
const ProcessField& process_field(std::string_view id);

// Decode scenario field snapshots while retaining live runtime values.
void load_process_data(Process& process, const toml::table& table);

// Format the chosen field from live values first; allocated_cores is the current scheduler rate.
std::string process_field_text(const Process& process, const Computer& computer,
                               double allocated_cores, std::string_view id);

} // namespace ai_sim