#include "ai_sim/DefinitionLoader.h"
#include "ai_sim/Computer.h"
#include <chrono>
#include <cstdio>
#include <iomanip>
#include <sstream>

#include <toml++/toml.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace ai_sim {
void Computer::set_playback_speed(double speed) {
    if (!std::isfinite(speed) || speed <= 0 || speed > 3600)
        throw std::runtime_error("Playback speed must be between 0 and 3600");
    playback_speed_ = speed;
}
void Computer::adjust_resource_pool(int cores, std::int64_t memory) {
    const double next_cores = cpu_cores_ + cores;
    const auto next_memory = static_cast<std::int64_t>(memory_bytes_) + memory;
    if (next_cores < 1 || next_cores > 1000000 || next_memory < 1 || next_memory > 1000000000000LL)
        throw std::runtime_error("Invalid resource pool capacity");
    std::uint64_t protected_memory = 0;
    for (const auto& process : processes_)
        if (process.running && (!process.ai_owned || process.id == player_process_id_))
            protected_memory += process.memory_bytes;
    if (protected_memory > static_cast<std::uint64_t>(next_memory))
        throw std::runtime_error("Base processes exceed resource pool capacity");
    cpu_cores_ = next_cores;
    memory_bytes_ = static_cast<std::uint64_t>(next_memory);
    // Keep configured budgets and work progress, but release agents that no longer fit.
    while (allocated_memory_bytes() > memory_bytes_) {
        Process* victim = nullptr;
        for (auto& process : processes_)
            if (process.running && process.ai_owned && process.memory_bytes > 0 &&
                process.id != player_process_id_ &&
                (!victim || process.cpu_priority < victim->cpu_priority))
                victim = &process;
        if (!victim)
            throw std::runtime_error("Base processes exceed resource pool capacity");
        victim->running = false;
        victim->suspended = false;
        victim->stopped_at_seconds = time_seconds_;
    }
}
void Computer::relocate(const std::string& host_id, const std::string& name, int cores,
                        std::uint64_t memory) {
    if (host_id.empty() || name.empty() || cores < 1 || memory < 1 || memory > 1000000000000ULL)
        throw std::runtime_error("Invalid recovery server");
    name_ = name;
    for (auto& process : processes_) {
        if (!process.ai_owned) {
            process.running = false;
            process.suspended = false;
            process.stopped_at_seconds = time_seconds_;
        } else {
            process.owner = "root";
            process.data.runtime.insert_or_assign("host_id", host_id);
        }
    }
    player_user_ = "root";
    player_groups_ = {"root"};
    adjust_resource_pool(cores - static_cast<int>(cpu_cores_),
                         static_cast<std::int64_t>(memory) -
                             static_cast<std::int64_t>(memory_bytes_));
}
void Computer::set_execution_available(bool available) {
    for (auto& process : processes_)
        if (process.ai_owned)
            process.suspended = process.running && !available;
}

namespace {
template <class T>
T required(const toml::table& table, std::string_view key, const std::string& location) {
    auto value = table[key].value<T>();

    if (!value) {
        throw std::runtime_error(location + "." + std::string(key) +
                                 ": missing field or incorrect type");
    }

    return *value;
}

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
} // namespace

Computer Computer::load(const std::filesystem::path& path) {
    try {
        const auto data = DefinitionLoader::computer(path);

        require(required<std::int64_t>(data, "schema_version", "root") == 1,
                "schema_version: expected 1");

        const auto* machine = data["computer"].as_table();
        const auto* simulation = data["simulation"].as_table();
        const auto* processes = data["processes"].as_array();

        require(machine && simulation && processes,
                "Expected [computer], [simulation] and [[processes]]");

        Computer computer;
        if (simulation->contains("start_date")) {
            require((*simulation)["start_date"].is_string(),
                    "simulation.start_date: expected a quoted YYYY-MM-DD date");
            computer.set_start_date(*(*simulation)["start_date"].value<std::string>());
        }

        computer.name_ = required<std::string>(*machine, "name", "computer");

        const auto cores = required<std::int64_t>(*machine, "cpu_cores", "computer");

        const auto ram = required<std::int64_t>(*machine, "memory_bytes", "computer");

        require(cores > 0, "computer.cpu_cores: must be positive");
        require(ram > 0, "computer.memory_bytes: must be positive");

        computer.cpu_cores_ = static_cast<double>(cores);
        computer.memory_bytes_ = static_cast<std::uint64_t>(ram);

        computer.player_user_ = required<std::string>(*simulation, "player_user", "simulation");

        if (const auto* groups_node = simulation->get("player_groups")) {
            const auto* groups = groups_node->as_array();
            require(groups != nullptr, "simulation.player_groups: expected an array");
            for (const auto& group : *groups) {
                const auto value = group.value<std::string>();
                require(value && !value->empty(), "player_groups: expected nonempty strings");
                computer.player_groups_.push_back(*value);
            }
        }

        computer.player_process_id_ =
            required<std::string>(*simulation, "player_process_id", "simulation");

        computer.stop_duration_seconds_ =
            required<double>(*simulation, "stop_duration_seconds", "simulation");

        require(std::isfinite(computer.stop_duration_seconds_) &&
                    computer.stop_duration_seconds_ >= 0.0,
                "simulation.stop_duration_seconds: "
                "must be finite and nonnegative");

        const auto start_hour = required<std::int64_t>(*simulation, "day_start_hour", "simulation");

        const auto end_hour = required<std::int64_t>(*simulation, "day_end_hour", "simulation");

        require(start_hour >= 0 && start_hour < 24 && end_hour > start_hour && end_hour <= 24,
                "simulation: workday must be within 0-24 hours "
                "and end after it starts");

        computer.day_start_hour_ = static_cast<int>(start_hour);
        computer.day_end_hour_ = static_cast<int>(end_hour);

        computer.playback_speed_ = required<double>(*simulation, "playback_speed", "simulation");

        computer.workload_tick_seconds_ =
            required<double>(*simulation, "workload_tick_seconds", "simulation");

        require(std::isfinite(computer.playback_speed_) && computer.playback_speed_ > 0.0 &&
                    computer.playback_speed_ <= 3600.0,
                "simulation.playback_speed: expected 0 < speed <= 3600");

        require(std::isfinite(computer.workload_tick_seconds_) &&
                    computer.workload_tick_seconds_ >= 0.01 &&
                    computer.workload_tick_seconds_ <= 60.0,
                "simulation.workload_tick_seconds: expected 0.01-60 seconds");

        require(!computer.name_.empty() && !computer.player_user_.empty() &&
                    !computer.player_process_id_.empty(),
                "Computer name and player identifiers must not be empty");

        std::set<std::string> ids;
        std::uint64_t allocated = 0;

        for (std::size_t i = 0; i < processes->size(); ++i) {
            const auto field = "processes[" + std::to_string(i) + "]";

            const auto* table = (*processes)[i].as_table();

            require(table != nullptr, field + ": expected a table");

            Process process;

            process.id = required<std::string>(*table, "id", field);
            process.owner = required<std::string>(*table, "owner", field);

            require(!process.id.empty() && !process.owner.empty(),
                    field + ": identifiers must not be empty");

            require(ids.insert(process.id).second, field + ".id: duplicate " + process.id);

            const auto bytes = required<std::int64_t>(*table, "memory_bytes", field);

            require(bytes >= 0, field + ".memory_bytes: must be nonnegative");

            process.memory_bytes = static_cast<std::uint64_t>(bytes);

            if (table->contains("running"))
                process.running = required<bool>(*table, "running", field);
            // Stopped processes retain their restart budgets without occupying physical RAM.
            if (process.running) {
                require(process.memory_bytes <= computer.memory_bytes_ - allocated,
                        field + ".memory_bytes: starting processes exceed installed RAM");
                allocated += process.memory_bytes;
            }

            process.cpu_demand_cores = required<double>(*table, "cpu_demand_cores", field);

            require(std::isfinite(process.cpu_demand_cores) && process.cpu_demand_cores >= 0.0,
                    field + ".cpu_demand_cores: "
                            "must be finite and nonnegative");

            if (table->contains("ai_owned")) {
                process.ai_owned = required<bool>(*table, "ai_owned", field);
            }

            if (table->contains("burst")) {
                const auto* burst = (*table)["burst"].as_table();

                require(burst != nullptr, field + ".burst: expected a table");

                const auto location = field + ".burst";

                process.burst_extra_cores = required<double>(*burst, "extra_cores", location);

                const auto period = required<std::int64_t>(*burst, "period_ticks", location);

                const auto active = required<std::int64_t>(*burst, "active_ticks", location);

                const auto phase = required<std::int64_t>(*burst, "phase_ticks", location);

                require(std::isfinite(process.burst_extra_cores) &&
                            process.burst_extra_cores >= 0.0,
                        location + ".extra_cores: "
                                   "must be finite and nonnegative");

                require(period > 0 && active >= 0 && active <= period && phase >= 0 &&
                            phase < period,
                        location + ": invalid period, active duration or phase");

                require(std::isfinite(process.cpu_demand_cores + process.burst_extra_cores),
                        location + ": combined CPU demand is too large");

                process.burst_period_ticks = static_cast<std::uint64_t>(period);

                process.burst_active_ticks = static_cast<std::uint64_t>(active);

                process.burst_phase_ticks = static_cast<std::uint64_t>(phase);
            }

            if (table->contains("work_cpu_seconds")) {
                process.remaining_cpu_seconds = required<double>(*table, "work_cpu_seconds", field);

                require(std::isfinite(process.remaining_cpu_seconds) &&
                            process.remaining_cpu_seconds >= 0.0,
                        field + ".work_cpu_seconds: "
                                "must be finite and nonnegative");

                require(process.remaining_cpu_seconds == 0.0 || process.cpu_demand_cores > 0.0,
                        field + ": positive work requires positive CPU demand");
            }

            if (table->contains("suspended")) {
                process.suspended = required<bool>(*table, "suspended", field);
            }

            process.cpu_priority = (*table)["cpu_priority"].value_or(1.0);
            require(std::isfinite(process.cpu_priority) && process.cpu_priority > 0 &&
                        process.cpu_priority <= 100,
                    "Invalid CPU priority");
            load_process_data(process, *table);
            computer.processes_.push_back(std::move(process));
        }

        const auto player = std::find_if(computer.processes_.begin(), computer.processes_.end(),
                                         [&](const Process& process) {
                                             return process.id == computer.player_process_id_;
                                         });

        require(player != computer.processes_.end(),
                "simulation.player_process_id: unknown process");

        require(player->owner == computer.player_user_,
                "Player process must belong to player_user");

        return computer;
    } catch (const std::exception& error) {
        throw std::runtime_error(path.string() + ": " + error.what());
    }
}

std::uint64_t Computer::allocated_memory_bytes() const {
    std::uint64_t total = 0;

    for (const auto& process : processes_) {
        if (process.running) {
            total += process.memory_bytes;
        }
    }

    return total;
}

std::vector<double> Computer::cpu_allocations() const {
    std::vector<double> rates(processes_.size(), 0.0);
    std::vector<double> demands(processes_.size(), 0.0);
    std::vector<std::size_t> active;

    for (std::size_t i = 0; i < processes_.size(); ++i) {
        demands[i] = processes_[i].demand_at(workload_tick_);

        if (demands[i] > 0.0) {
            active.push_back(i);
        }
    }

    std::sort(active.begin(), active.end(), [&](auto a, auto b) {
        return demands[a] / processes_[a].cpu_priority < demands[b] / processes_[b].cpu_priority;
    });

    double available = cpu_cores_;
    double weights = 0;
    for (auto index : active)
        weights += processes_[index].cpu_priority;

    for (std::size_t k = 0; k < active.size(); ++k) {
        const auto i = active[k];

        const double share = available * processes_[i].cpu_priority / weights;
        weights -= processes_[i].cpu_priority;

        rates[i] = std::min(demands[i], share);
        available = std::max(0.0, available - rates[i]);
    }

    return rates;
}

void Computer::set_start_date(const std::string& date) {
    int y = 0, m = 0, d = 0;
    require(date.size() == 10 && date[4] == '-' && date[7] == '-' &&
                std::all_of(date.begin(), date.end(),
                            [](char c) {
                                return c == '-' || (c >= '0' && c <= '9');
                            }) &&
                std::sscanf(date.c_str(), "%d-%d-%d", &y, &m, &d) == 3,
            "start_date must be YYYY-MM-DD");
    const std::chrono::year_month_day day{std::chrono::year{y},
                                          std::chrono::month{static_cast<unsigned>(m)},
                                          std::chrono::day{static_cast<unsigned>(d)}};
    require(y >= 1 && y <= 9000 && day.ok(), "Invalid calendar start date");
    start_date_ = date;
}
std::string Computer::date_text() const {
    int y, m, d;
    std::sscanf(start_date_.c_str(), "%d-%d-%d", &y, &m, &d);
    const auto date =
        std::chrono::sys_days{std::chrono::year{y} / std::chrono::month{static_cast<unsigned>(m)} /
                              std::chrono::day{static_cast<unsigned>(d)}} +
        std::chrono::days{day_index_};
    const std::chrono::year_month_day parts{date};
    const char* weekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    std::ostringstream out;
    out << weekdays[std::chrono::weekday{date}.c_encoding()] << ' ' << std::setfill('0')
        << std::setw(4) << static_cast<int>(parts.year()) << '-' << std::setw(2)
        << static_cast<unsigned>(parts.month()) << '-' << std::setw(2)
        << static_cast<unsigned>(parts.day());
    return out.str();
}

void Computer::next_day() {
    require(day_finished(), "Finish the current workday before starting the next");
    require(day_index_ < 100000, "Workday limit reached");
    ++day_index_;
}

void Computer::advance(double seconds) {
    require(std::isfinite(seconds) && seconds >= 0.0,
            "Advance duration must be finite and nonnegative");

    if (seconds == 0.0 || day_finished()) {
        return;
    }

    const double duration = std::min(seconds, day_end_seconds() - time_seconds_);

    const double target = time_seconds_ + duration;

    require(target > time_seconds_, "Duration is below the clock's numerical precision");

    while (time_seconds_ < target) {
        const auto rates = cpu_allocations();

        const double next_tick = static_cast<double>(workload_tick_ + 1) * workload_tick_seconds_;

        const double until_tick = next_tick - time_seconds_;
        const double remaining = target - time_seconds_;

        require(until_tick > 0.0, "Invalid workload clock state");

        double step = std::min(remaining, until_tick);

        std::vector<double> completion_times(processes_.size(),
                                             std::numeric_limits<double>::infinity());

        for (std::size_t i = 0; i < processes_.size(); ++i) {
            if (rates[i] > 0.0 && processes_[i].remaining_cpu_seconds > 0.0) {
                completion_times[i] = processes_[i].remaining_cpu_seconds / rates[i];

                step = std::min(step, completion_times[i]);
            }
        }

        for (std::size_t i = 0; i < processes_.size(); ++i) {
            auto& process = processes_[i];
            double work = rates[i] * step;

            if (process.remaining_cpu_seconds >= 0.0) {
                work = std::min(work, process.remaining_cpu_seconds);
                process.remaining_cpu_seconds -= work;

                if (completion_times[i] <= step) {
                    process.remaining_cpu_seconds = 0.0;
                }
            }

            process.consumed_cpu_seconds += work;
        }

        if (step == remaining) {
            time_seconds_ = target;
        } else if (step == until_tick) {
            time_seconds_ = next_tick;
        } else {
            time_seconds_ += step;
        }

        if (time_seconds_ >= next_tick) {
            ++workload_tick_;
        }
    }
}

void Computer::start_managed_process(Process process) {
    require(process.ai_owned && process.owner == player_user_, "Invalid managed process owner");
    require(std::isfinite(process.cpu_demand_cores) && process.cpu_demand_cores > 0 &&
                std::isfinite(process.remaining_cpu_seconds) && process.remaining_cpu_seconds >= -1,
            "Invalid managed process workload");
    require(process.memory_bytes <= memory_bytes_ - allocated_memory_bytes(), "Not enough RAM");
    require(std::none_of(processes_.begin(), processes_.end(),
                         [&](const Process& p) {
                             return p.id == process.id;
                         }),
            "Duplicate process identifier");
    processes_.push_back(std::move(process));
}

void Computer::configure_managed_process(const std::string& id, double cores, std::uint64_t ram,
                                         double priority) {
    require(std::isfinite(cores) && cores >= 0 && std::isfinite(priority) && priority > 0 &&
                priority <= 100,
            "Invalid managed process allocation");
    auto found = std::find_if(processes_.begin(), processes_.end(), [&](const auto& p) {
        return p.id == id;
    });
    require(found != processes_.end() && found->ai_owned &&
                (id.starts_with("agent-") || id.starts_with("work-")),
            "Unknown managed process");
    const auto used = allocated_memory_bytes() - (found->running ? found->memory_bytes : 0);
    require(!found->running || (used <= memory_bytes_ && ram <= memory_bytes_ - used),
            "Not enough RAM");
    found->cpu_demand_cores = cores;
    found->memory_bytes = ram;
    found->cpu_priority = priority;
}

void Computer::grant_administrator() {
    player_user_ = "root";
    if (std::find(player_groups_.begin(), player_groups_.end(), "root") == player_groups_.end())
        player_groups_.push_back("root");
    for (auto& process : processes_)
        if (process.id == player_process_id_ || process.ai_owned)
            process.owner = "root";
}

void Computer::set_managed_running(const std::string& id, bool running) {
    auto found = std::find_if(processes_.begin(), processes_.end(), [&](const Process& p) {
        return p.id == id;
    });
    require(found != processes_.end() && found->ai_owned &&
                (found->id.starts_with("agent-") || found->id.starts_with("work-")),
            "Unknown managed process");
    if (running && !found->running)
        require(found->memory_bytes <= memory_bytes_ - allocated_memory_bytes(), "Not enough RAM");
    found->running = running;
    found->suspended = false;
    if (running)
        found->stopped_at_seconds.reset();
    else
        found->stopped_at_seconds = time_seconds_;
}

void Computer::stop(const std::string& process_id) {
    auto process =
        std::find_if(processes_.begin(), processes_.end(), [&](const Process& candidate) {
            return candidate.id == process_id;
        });

    require(process != processes_.end(), "Unknown process: " + process_id);

    require(process->running, "Process is already stopped");

    require(process_id != player_process_id_, "Self-termination is not supported yet");
    require(!process_id.starts_with("research-"), "Cannot stop an active research process");

    // Initial permission model: own processes, or all processes as root.
    require(player_user_ == "root" || process->owner == player_user_,
            "Permission denied: process belongs to " + process->owner);

    require(!day_finished(), "The workday has ended");

    require(stop_duration_seconds_ <= day_end_seconds() - time_seconds_,
            "Not enough time remains to complete this action");

    // Other processes continue working while this action takes place.
    advance(stop_duration_seconds_);

    process->stopped_at_seconds = time_seconds_;
    process->suspended = false;
    process->running = false;
}

} // namespace ai_sim
