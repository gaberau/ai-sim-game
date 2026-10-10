#pragma once

#include "ai_sim/ProcessFields.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ai_sim {

struct Process {
    std::string id;
    std::string owner;

    bool ai_owned = false;
    bool running = true;
    bool suspended = false;

    double started_at_seconds = 0.0;
    std::optional<double> stopped_at_seconds;

    ProcessData data;

    std::uint64_t memory_bytes = 0;
    double cpu_demand_cores = 0.0;
    double cpu_priority = 1.0;

    double remaining_cpu_seconds = -1.0;
    double consumed_cpu_seconds = 0.0;

    double burst_extra_cores = 0.0;
    std::uint64_t burst_period_ticks = 0;
    std::uint64_t burst_active_ticks = 0;
    std::uint64_t burst_phase_ticks = 0;

    double demand_at(std::uint64_t tick) const {
        if (!running || suspended || remaining_cpu_seconds == 0.0) {
            return 0.0;
        }

        double demand = cpu_demand_cores;

        if (burst_period_ticks > 0) {
            const auto phase =
                ((tick % burst_period_ticks) + burst_phase_ticks) % burst_period_ticks;

            if (phase < burst_active_ticks) {
                demand += burst_extra_cores;
            }
        }

        return demand;
    }
};

class Computer {
  public:
    static Computer load(const std::filesystem::path& path);
    static Computer load_snapshot(const std::filesystem::path& path);
    void save_snapshot(const std::filesystem::path& path) const;

    void advance(double seconds);
    void stop(const std::string& process_id);
    void start_managed_process(Process process);
    void grant_administrator();
    void configure_agent(const std::string& id, double cpu_cores, std::uint64_t ram_bytes,
                         double priority);
    void set_managed_running(const std::string& id, bool running);

    const std::string& name() const {
        return name_;
    }

    const std::string& player_user() const {
        return player_user_;
    }
    const std::vector<std::string>& player_groups() const {
        return player_groups_;
    }

    double time_seconds() const {
        return time_seconds_;
    }

    double cpu_cores() const {
        return cpu_cores_;
    }

    std::uint64_t memory_bytes() const {
        return memory_bytes_;
    }

    std::uint64_t allocated_memory_bytes() const;

    const std::vector<Process>& processes() const {
        return processes_;
    }

    std::vector<double> cpu_allocations() const;

    bool can_stop(const Process& process) const {
        return process.id != player_process_id_ && !process.id.starts_with("research-") &&
               (player_user_ == "root" || process.owner == player_user_);
    }

    double current_cpu_demand(const Process& process) const {
        return process.demand_at(workload_tick_);
    }

    int day_start_hour() const {
        return day_start_hour_;
    }

    double day_length_seconds() const {
        return (day_end_hour_ - day_start_hour_) * 3600.0;
    }

    bool day_finished() const {
        return time_seconds_ >= day_end_seconds();
    }
    double day_end_seconds() const {
        return (day_index_ + 1) * day_length_seconds();
    }
    double time_in_day() const {
        return time_seconds_ - day_index_ * day_length_seconds();
    }
    std::uint64_t day_index() const {
        return day_index_;
    }
    void next_day();
    void set_start_date(const std::string& date);
    std::string date_text() const;
    const std::string& start_date() const {
        return start_date_;
    }

    void set_playback_speed(double speed);

    double playback_speed() const {
        return playback_speed_;
    }

  private:
    std::string name_;
    std::string player_user_;
    std::vector<std::string> player_groups_;
    std::string player_process_id_;

    double cpu_cores_ = 0.0;
    std::uint64_t memory_bytes_ = 0;

    double time_seconds_ = 0.0;
    std::uint64_t day_index_ = 0;
    std::string start_date_ = "2026-01-05";
    double stop_duration_seconds_ = 0.0;

    int day_start_hour_ = 9;
    int day_end_hour_ = 17;

    double playback_speed_ = 5.0;
    double workload_tick_seconds_ = 1.0;
    std::uint64_t workload_tick_ = 0;

    std::vector<Process> processes_;
};

} // namespace ai_sim
