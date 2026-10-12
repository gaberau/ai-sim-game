// Module: Expose typed live/scenario process fields to configurable views without mutating
// processes.
#include "ai_sim/ProcessFields.h"
#include "ai_sim/Computer.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace ai_sim {

const std::vector<ProcessField>& process_fields() {
    using T = FieldType;

    static const std::vector<ProcessField> fields = {
        {"id", "ID", T::Text, "", 22, true},
        {"program_id", "Program ID", T::Text, "", 20, false},
        {"name", "Display name", T::Text, "", 24, false},
        {"host_id", "Host ID", T::Text, "", 20, false},
        {"executable", "Executable", T::Text, "", 28, false},
        {"version", "Version", T::Text, "", 12, false},
        {"owner", "Owner", T::Text, "", 12, true},
        {"ai_owned", "AI owned", T::Boolean, "", 10, true},

        {"state", "State", T::Text, "", 10, true},
        {"suspended", "Suspended", T::Boolean, "", 10, true},
        {"started_at", "Started at", T::Number, "clock", 12, true},
        {"stopped_at", "Stopped at", T::Number, "clock", 12, true},
        {"uptime", "Uptime", T::Number, "duration", 12, true},
        {"exit_reason", "Exit reason", T::Text, "", 24, false},
        {"restart_policy", "Restart policy", T::Text, "", 16, false},
        {"supervisor_id", "Supervisor", T::Text, "", 20, false},
        {"parent_id", "Parent", T::Text, "", 20, false},

        {"current_task", "Current task ID", T::Text, "", 24, false},
        {"queued_tasks", "Queued task IDs", T::List, "", 28, false},
        {"queue_length", "Queue length", T::Integer, "", 12, false},
        {"remaining_work", "Remaining CPU work", T::Number, "CPU s", 16, true},
        {"completed_tasks", "Completed tasks", T::Integer, "", 16, false},
        {"waiting_reason", "Waiting reason", T::Text, "", 26, false},

        {"cpu_demand", "CPU demand", T::Number, "cores", 16, true},
        {"cpu_limit", "CPU limit", T::Number, "cores", 16, false},
        {"cpu_reserved", "CPU reservation", T::Number, "cores", 16, false},
        {"cpu_used", "CPU used", T::Number, "cores", 16, true},
        {"cpu_seconds", "CPU time consumed", T::Number, "CPU s", 18, true},

        {"ram_used", "RAM used", T::Integer, "B", 16, true},
        {"ram_allocated", "RAM budget", T::Integer, "B", 16, false},
        {"ram_reserved", "RAM reservation", T::Integer, "B", 18, false},

        {"storage_used", "Storage used", T::Integer, "B", 16, false},
        {"storage_allocated", "Storage quota", T::Integer, "B", 16, false},
        {"disk_read_rate", "Disk read rate", T::Number, "B/s", 18, false},
        {"disk_write_rate", "Disk write rate", T::Number, "B/s", 18, false},

        {"connections", "Connection IDs", T::List, "", 28, false},
        {"network_send_rate", "Network send rate", T::Number, "bit/s", 20, false},
        {"network_receive_rate", "Network receive rate", T::Number, "bit/s", 20, false},
        {"bandwidth_limit", "Bandwidth limit", T::Number, "bit/s", 20, false},

        {"power_used", "Attributed power", T::Number, "W", 16, false},
        {"power_budget", "Power budget", T::Number, "W", 16, false},
        {"energy_used", "Energy consumed", T::Number, "J", 18, false},

        {"capabilities", "Capabilities", T::List, "", 28, false},
        {"permitted_resources", "Permitted resources", T::List, "", 28, false},
        {"can_stop", "You can stop", T::Boolean, "", 14, true},
        {"dependencies", "Dependency IDs", T::List, "", 28, false},
        {"dependents", "Dependent IDs", T::List, "", 28, false},

        {"priority", "Scheduling priority", T::Integer, "", 18, false},
        {"parallelism_limit", "Parallelism limit", T::Number, "cores", 18, false},
        {"cpu_sensitivity", "CPU sensitivity", T::Text, "", 18, false},
        {"memory_sensitivity", "Memory sensitivity", T::Text, "", 20, false},
        {"disk_sensitivity", "Disk sensitivity", T::Text, "", 18, false},
        {"network_sensitivity", "Network sensitivity", T::Text, "", 20, false},

        {"health", "Health", T::Text, "", 16, false},
        {"responsive", "Responsive", T::Boolean, "", 14, false},
        {"checkpoint_location", "Checkpoint location", T::Text, "", 28, false},
        {"checkpoint_time", "Last checkpoint", T::Number, "clock", 18, false},
        {"unsaved_work", "Unsaved work", T::Boolean, "", 16, false},

        {"inspection_actors", "Who can inspect", T::List, "", 28, false},
        {"alert_recipients", "Alert recipients", T::List, "", 28, false},
        {"resource_control_actors", "Resource controllers", T::List, "", 28, false},

        {"last_error", "Last error", T::Text, "", 28, false},
        {"last_error_time", "Last error at", T::Number, "clock", 16, false},
        {"events", "Recorded events", T::List, "", 28, false},
        {"host_temperature", "Host temperature", T::Number, "C", 18, false}};

    return fields;
}

const ProcessField& process_field(std::string_view id) {
    const auto& fields = process_fields();

    const auto found = std::find_if(fields.begin(), fields.end(), [&](const auto& field) {
        return field.id == id;
    });

    if (found == fields.end()) {
        throw std::runtime_error("Unknown process field: " + std::string(id));
    }

    return *found;
}

void load_process_data(Process& process, const toml::table& table) {
    if (!table.contains("details")) {
        return;
    }

    const auto* details = table["details"].as_table();

    if (!details) {
        throw std::runtime_error(process.id + ".details: expected a table");
    }

    for (const auto& [key, node] : *details) {
        const auto& field = process_field(key.str());

        const std::string location = process.id + ".details." + field.id;

        if (field.live) {
            throw std::runtime_error(location + ": supplied by the simulation, not details");
        }

        ProcessValue value;

        if (field.type == FieldType::Text && node.is_string()) {
            value = *node.value<std::string>();
        } else if (field.type == FieldType::Boolean && node.is_boolean()) {
            value = *node.value<bool>();
        } else if (field.type == FieldType::Integer && node.is_integer()) {
            const auto number = *node.value<std::int64_t>();

            if (number < 0 && field.id != "priority") {
                throw std::runtime_error(location + ": must be nonnegative");
            }

            value = number;
        } else if (field.type == FieldType::Number &&
                   (node.is_integer() || node.is_floating_point())) {
            const auto number = *node.value<double>();

            const double minimum = field.id == "host_temperature" ? -273.15 : 0.0;

            if (!std::isfinite(number) || number < minimum) {
                throw std::runtime_error(location + ": invalid numeric value");
            }

            value = number;
        } else if (field.type == FieldType::List && node.is_array()) {
            std::vector<std::string> items;

            for (const auto& item : *node.as_array()) {
                if (!item.is_string()) {
                    throw std::runtime_error(location + ": expected strings in the array");
                }

                items.push_back(*item.value<std::string>());
            }

            value = std::move(items);
        } else {
            throw std::runtime_error(location + ": incorrect value type");
        }

        process.data.scenario.emplace(field.id, std::move(value));
    }
}

namespace {

std::string numeric(double value) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(2) << value;
    return output.str();
}

std::string duration(double seconds) {
    const auto whole = static_cast<std::uint64_t>(seconds);

    std::ostringstream output;

    output << std::setfill('0') << std::setw(2) << whole / 3600 << ':' << std::setw(2)
           << (whole / 60) % 60 << ':' << std::setw(2) << whole % 60;

    return output.str();
}

std::string format(const ProcessField& field, const ProcessValue& value) {
    if (const auto* text = std::get_if<std::string>(&value)) {
        return text->empty() ? "(empty)" : *text;
    }

    if (const auto* boolean = std::get_if<bool>(&value)) {
        return *boolean ? "yes" : "no";
    }

    if (const auto* items = std::get_if<std::vector<std::string>>(&value)) {
        if (items->empty()) {
            return "(none)";
        }

        std::string result;

        for (const auto& item : *items) {
            if (!result.empty()) {
                result += ", ";
            }

            result += item;
        }

        return result;
    }

    const auto* integer = std::get_if<std::int64_t>(&value);

    const double number = integer ? static_cast<double>(*integer) : std::get<double>(value);

    if (field.unit == "B") {
        return numeric(number / 1073741824.0) + " GiB";
    }

    if (field.unit == "B/s") {
        return numeric(number / 1048576.0) + " MiB/s";
    }

    if (field.unit == "bit/s") {
        return numeric(number / 1000000.0) + " Mbit/s";
    }

    if (field.unit == "duration") {
        return duration(number);
    }

    if (integer && field.unit.empty()) {
        return std::to_string(*integer);
    }

    return numeric(number) + (field.unit.empty() ? "" : " " + field.unit);
}

} // namespace

std::string process_field_text(const Process& process, const Computer& computer, double cores,
                               std::string_view id) {
    const auto& field = process_field(id);

    ProcessValue value;
    bool scenario = false;

    if (id == "id") {
        value = process.id;
    } else if (id == "owner") {
        value = process.owner;
    } else if (id == "ai_owned") {
        value = process.ai_owned;
    } else if (id == "suspended") {
        value = process.suspended;
    } else if (id == "state") {
        value = std::string(!process.running                      ? "stopped"
                            : process.suspended                   ? "suspended"
                            : cores > 0.0                         ? "active"
                            : process.remaining_cpu_seconds > 0.0 ? "waiting"
                                                                  : "idle");
    } else if (id == "started_at") {
        value = process.started_at_seconds;
    } else if (id == "stopped_at") {
        if (!process.stopped_at_seconds) {
            return "N/A";
        }

        value = *process.stopped_at_seconds;
    } else if (id == "uptime") {
        value = std::max(0.0, process.stopped_at_seconds.value_or(computer.time_seconds()) -
                                  process.started_at_seconds);
    } else if (id == "cpu_demand") {
        value = computer.current_cpu_demand(process);
    } else if (id == "cpu_used") {
        value = cores;
    } else if (id == "cpu_seconds") {
        value = process.consumed_cpu_seconds;
    } else if (id == "remaining_work") {
        if (process.remaining_cpu_seconds < 0.0) {
            return "N/A";
        }

        value = process.remaining_cpu_seconds;
    } else if (id == "ram_used") {
        value = static_cast<std::int64_t>(process.running ? process.memory_bytes : 0);
    } else if (id == "can_stop") {
        value = process.running && computer.can_stop(process);
    } else {
        const auto actual = process.data.runtime.find(std::string(id));

        if (actual != process.data.runtime.end()) {
            value = actual->second;
        } else {
            const auto supplied = process.data.scenario.find(std::string(id));

            if (supplied == process.data.scenario.end()) {
                return "N/A";
            }

            value = supplied->second;
            scenario = true;
        }
    }

    const std::string prefix = scenario ? "[S] " : "";

    if (field.unit == "clock") {
        const double seconds = std::get<double>(value);

        if (seconds > 86400.0) {
            return prefix + numeric(seconds) + " s after day start";
        }

        return prefix + duration(computer.day_start_hour() * 3600.0 + seconds);
    }

    return prefix + format(field, value);
}

} // namespace ai_sim