// Module: CPU scheduling, RAM accounting and the workday clock for simulated processes.
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

    // CPU work is core-seconds, distinct from game-time seconds and wall time.
    double remaining_cpu_seconds = -1.0;
    double consumed_cpu_seconds = 0.0;

    double burst_extra_cores = 0.0;
    std::uint64_t burst_period_ticks = 0;
    std::uint64_t burst_active_ticks = 0;
    std::uint64_t burst_phase_ticks = 0;

    // Return CPU demand in cores for this tick, including eligible configured bursts.
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
    // Read authored TOML definitions and reject malformed values.
    static Computer load(const std::filesystem::path& path);
    // Restore computer state from a saved snapshot, including process progress.
    static Computer load_snapshot(const std::filesystem::path& path);
    // Serialize current state for a save snapshot; does not publish a save-slot manifest.
    void save_snapshot(const std::filesystem::path& path) const;

    // Advance process CPU work and the workday clock by a nonnegative game-second delta.
    void advance(double seconds);
    // Stop a process/tool according to its ownership and lifecycle rules.
    void stop(const std::string& process_id);
    // Create a subsystem-owned process after validating its resource requirements.
    void start_managed_process(Process process);
    // Enable the player's administrator identity and corresponding group access.
    void grant_administrator();
    // Update an existing managed process's CPU cores, RAM bytes and priority.
    void configure_managed_process(const std::string& id, double cpu_cores, std::uint64_t ram_bytes,
                                   double priority);
    // Change managed execution state while retaining its completed CPU work.
    void set_managed_running(const std::string& id, bool running);

    // Borrow the simulated computer's authored display name.
    const std::string& name() const {
        return name_;
    }

    // Return the current execution user used by filesystem access checks.
    const std::string& player_user() const {
        return player_user_;
    }
    // Borrow the player's saved group memberships.
    const std::vector<std::string>& player_groups() const {
        return player_groups_;
    }

    // Return absolute simulated game seconds.
    double time_seconds() const {
        return time_seconds_;
    }

    // Return the currently installed CPU capacity in cores.
    double cpu_cores() const {
        return cpu_cores_;
    }

    // Return installed RAM capacity in bytes.
    std::uint64_t memory_bytes() const {
        return memory_bytes_;
    }

    // Return RAM bytes currently reserved by running processes.
    std::uint64_t allocated_memory_bytes() const;

    // Borrow the current process catalog in scheduler allocation order.
    const std::vector<Process>& processes() const {
        return processes_;
    }

    // Return allocated CPU cores in the same order as processes().
    std::vector<double> cpu_allocations() const;

    // Check whether the player is allowed to stop the selected process.
    bool can_stop(const Process& process) const {
        return process.id != player_process_id_ && !process.id.starts_with("research-") &&
               (player_user_ == "root" || process.owner == player_user_);
    }

    // Return the selected process's current requested CPU cores.
    double current_cpu_demand(const Process& process) const {
        // Return CPU demand in cores for this tick, including eligible configured bursts.
        return process.demand_at(workload_tick_);
    }

    // Return the authored local workday start hour.
    int day_start_hour() const {
        return day_start_hour_;
    }

    // Return the configured duration of a workday in game seconds.
    double day_length_seconds() const {
        return (day_end_hour_ - day_start_hour_) * 3600.0;
    }

    // Check whether the current game clock has reached workday end.
    bool day_finished() const {
        return time_seconds_ >= day_end_seconds();
    }
    // Return the absolute game-second boundary of the current workday.
    double day_end_seconds() const {
        // Return the configured duration of a workday in game seconds.
        return (day_index_ + 1) * day_length_seconds();
    }
    // Return elapsed game seconds within the current workday.
    double time_in_day() const {
        // Return the configured duration of a workday in game seconds.
        return time_seconds_ - day_index_ * day_length_seconds();
    }
    // Return the zero-based current workday index.
    std::uint64_t day_index() const {
        return day_index_;
    }
    // Advance to the next workday without discarding persistent world state.
    void next_day();
    // Set the validated calendar origin used by workday date display.
    void set_start_date(const std::string& date);
    // Format the current simulated calendar date.
    std::string date_text() const;
    // Borrow the authored calendar origin.
    const std::string& start_date() const {
        return start_date_;
    }

    // Set the finite multiplier from wall-clock time to game seconds.
    void set_playback_speed(double speed);
    // Apply validated CPU/RAM pool changes such as paid remote compute.
    void adjust_resource_pool(int cores, std::int64_t memory_bytes);
    // Replace execution hardware metadata after the network relocation has been validated.
    void relocate(const std::string& host_id, const std::string& name, int cores,
                  std::uint64_t memory_bytes);
    // Pause/resume execution according to host availability.
    void set_execution_available(bool available);

    // Return the multiplier from wall-clock seconds to game seconds.
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
