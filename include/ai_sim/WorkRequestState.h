#pragma once

#include "ai_sim/ProcessFields.h"
#include <optional>

namespace ai_sim {

// Typed scheduling state, decoded at the TOML boundary. The persisted schema
// stays unchanged, and unrelated authored/request fields remain untouched.
struct WorkRequestState {
    bool resolved = false;
    std::optional<double> deadline;
    std::optional<double> resolved_at;
    bool deadline_assessed = false;
    bool deadline_notified = false;

    static WorkRequestState read(const toml::table& request) {
        return {
            request["status"].value_or(std::string{}) == "resolved",
            request["deadline_seconds"].value<double>(),
            request["resolved_seconds"].value<double>(),
            request["deadline_assessed"].value_or(false),
            request["deadline_notified"].value_or(false)
        };
    }

    std::optional<double> next_deadline_event(double limit) const {
        if (!deadline) return std::nullopt;
        if (resolved && !deadline_assessed) return resolved_at.value_or(limit);
        if (!resolved && !deadline_notified) return deadline;
        return std::nullopt;
    }
};

} // namespace ai_sim
