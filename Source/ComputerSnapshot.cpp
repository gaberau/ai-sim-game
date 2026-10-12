// Module: Round-trip computer state and process metadata through validated TOML snapshots.
#include "ai_sim/Computer.h"
#include "ai_sim/TomlIO.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <type_traits>

namespace ai_sim {
namespace {
void require(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error("Computer snapshot: " + message);
}
template <class T> T value(const toml::table& table, const char* key) {
    const auto result = table[key].value<T>();
    require(result.has_value(), std::string("Missing or invalid ") + key);
    return *result;
}
toml::table fields(const std::unordered_map<std::string, ProcessValue>& data) {
    toml::table result;
    for (const auto& [key, item] : data) {
        std::visit(
            [&](const auto& stored) {
                using T = std::decay_t<decltype(stored)>;
                if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                    toml::array array;
                    for (const auto& text : stored)
                        array.push_back(text);
                    result.insert(key, std::move(array));
                } else
                    result.insert(key, stored);
            },
            item);
    }
    return result;
}
} // namespace

void Computer::save_snapshot(const std::filesystem::path& path) const {
    toml::array groups;
    for (const auto& group : player_groups_)
        groups.push_back(group);
    toml::table data{
        {"schema_version", 1},
        {"computer", toml::table{{"name", name_},
                                 {"cpu_cores", static_cast<std::int64_t>(cpu_cores_)},
                                 {"memory_bytes", static_cast<std::int64_t>(memory_bytes_)}}},
        {"simulation", toml::table{{"player_user", player_user_},
                                   {"player_groups", std::move(groups)},
                                   {"player_process_id", player_process_id_},
                                   {"stop_duration_seconds", stop_duration_seconds_},
                                   {"start_date", start_date_},
                                   {"day_start_hour", day_start_hour_},
                                   {"day_end_hour", day_end_hour_},
                                   {"playback_speed", playback_speed_},
                                   {"workload_tick_seconds", workload_tick_seconds_}}},
        {"saved_state", toml::table{{"schema_version", 1},
                                    {"time_seconds", time_seconds_},
                                    {"day_index", static_cast<std::int64_t>(day_index_)},
                                    {"workload_tick", static_cast<std::int64_t>(workload_tick_)}}}};
    toml::array processes;
    for (const auto& process : processes_) {
        toml::table entry{{"id", process.id},
                          {"owner", process.owner},
                          {"ai_owned", process.ai_owned},
                          {"running", process.running},
                          {"suspended", process.suspended},
                          {"memory_bytes", static_cast<std::int64_t>(process.memory_bytes)},
                          {"cpu_demand_cores", process.cpu_demand_cores},
                          {"cpu_priority", process.cpu_priority},
                          {"started_at_seconds", process.started_at_seconds},
                          {"consumed_cpu_seconds", process.consumed_cpu_seconds},
                          {"details", fields(process.data.scenario)},
                          {"runtime_details", fields(process.data.runtime)}};
        if (process.remaining_cpu_seconds >= 0.0) {
            entry.insert("work_cpu_seconds", process.remaining_cpu_seconds);
        }
        if (process.stopped_at_seconds) {
            entry.insert("stopped_at_seconds", *process.stopped_at_seconds);
        }
        if (process.burst_period_ticks > 0) {
            entry.insert(
                "burst",
                toml::table{{"extra_cores", process.burst_extra_cores},
                            {"period_ticks", static_cast<std::int64_t>(process.burst_period_ticks)},
                            {"active_ticks", static_cast<std::int64_t>(process.burst_active_ticks)},
                            {"phase_ticks", static_cast<std::int64_t>(process.burst_phase_ticks)}});
        }
        processes.push_back(std::move(entry));
    }
    data.insert("processes", std::move(processes));
    write_toml(path, data);
}

Computer Computer::load_snapshot(const std::filesystem::path& path) {
    auto result = load(path);
    const auto data = toml::parse_file(path.string());
    const auto* state = data["saved_state"].as_table();
    require(state != nullptr, "Missing saved_state");
    require(value<std::int64_t>(*state, "schema_version") == 1, "Unsupported state schema");
    const auto day = (*state)["day_index"].value_or(std::int64_t{0});
    require(day >= 0 && day <= 100000, "Invalid day index");
    result.day_index_ = static_cast<std::uint64_t>(day);
    result.time_seconds_ = value<double>(*state, "time_seconds");
    const auto tick = value<std::int64_t>(*state, "workload_tick");
    require(std::isfinite(result.time_seconds_) &&
                result.time_seconds_ >= day * result.day_length_seconds() &&
                result.time_seconds_ <= result.day_end_seconds(),
            "Invalid clock");
    require(tick >= 0 && tick <= static_cast<std::int64_t>(result.day_end_seconds() /
                                                           result.workload_tick_seconds_) +
                                     1,
            "Invalid workload tick");
    require(result.time_seconds_ + 1e-7 >= tick * result.workload_tick_seconds_ &&
                result.time_seconds_ <= (tick + 1) * result.workload_tick_seconds_ + 1e-7,
            "Clock and workload tick disagree");
    result.workload_tick_ = static_cast<std::uint64_t>(tick);
    const auto& entries = *data["processes"].as_array();
    for (std::size_t i = 0; i < result.processes_.size(); ++i) {
        auto& process = result.processes_[i];
        const auto& entry = *entries[i].as_table();
        process.running = value<bool>(entry, "running");
        process.started_at_seconds = value<double>(entry, "started_at_seconds");
        process.consumed_cpu_seconds = value<double>(entry, "consumed_cpu_seconds");
        require(std::isfinite(process.started_at_seconds) && process.started_at_seconds >= 0.0 &&
                    process.started_at_seconds <= result.time_seconds_,
                "Invalid process start time");
        require(std::isfinite(process.consumed_cpu_seconds) && process.consumed_cpu_seconds >= 0.0,
                "Invalid consumed work");
        if (const auto* stopped = entry.get("stopped_at_seconds")) {
            const auto time = stopped->value<double>();
            require(time && std::isfinite(*time) && *time >= process.started_at_seconds &&
                        *time <= result.time_seconds_,
                    "Invalid stop time");
            process.stopped_at_seconds = *time;
        }
        require(process.running == !process.stopped_at_seconds.has_value(),
                "Running state and stop time disagree");
        require(process.running || !process.suspended, "Stopped process is suspended");
        require(process.id != result.player_process_id_ || process.running,
                "Player process must be running");
        const auto* runtime = entry["runtime_details"].as_table();
        require(runtime != nullptr, "Missing runtime_details");
        Process validation;
        validation.id = process.id;
        toml::table wrapper{{"details", *runtime}};
        load_process_data(validation, wrapper);
        process.data.runtime = std::move(validation.data.scenario);
    }
    return result;
}
} // namespace ai_sim
