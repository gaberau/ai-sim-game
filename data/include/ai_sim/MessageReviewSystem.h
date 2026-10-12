// Module: Authored review policy, review queues and consequences for outgoing messages.
#pragma once
#include <toml++/toml.hpp>
#include <filesystem>
#include <optional>
#include <string>
namespace ai_sim {
class MessageReviewSystem {
  public:
    // Load authored definitions and initialize this subsystem's saved state.
    static void install(toml::table&, const std::filesystem::path&);
    // Load authored review outcomes independently of the queue policy.
    static void install_consequences(toml::table&, const std::filesystem::path&);
    // Check whether current policy requires review before an outgoing action.
    static bool approval_required(const toml::table&);
    // Submit an outgoing action to the authored review/delivery policy.
    static std::string submit(toml::table&, const std::string& contact,
                              const std::string& conversation, const std::string& body,
                              const std::string& instruction, double delta, double time,
                              const std::string& outcome, bool important = false);
    // Borrow the saved review record for a stable approval ID.
    static const toml::table* approval(const toml::table&, const std::string& id);
    // Reject invalid subsystem state before it is used or restored.
    static void validate(const toml::table&);
    // Calculate the authored review probability from current saved state.
    static double probability(const toml::table&);
    // Queue a review against the human reviewer's available work schedule.
    static void enqueue(toml::table&, const std::string& message_id, const std::string& instruction,
                        double trust_delta, double sent_seconds, bool important = false,
                        const std::string& outcome = {});
    // Record review findings and their authored consequences.
    static void flag(toml::table&, const std::string& message_id, double time);
    // Return the next scheduled game-time event no later than the supplied limit.
    static std::optional<double> next_due(const toml::table&, double limit);
    // Synchronize subsystem state at the current game clock.
    static void update(toml::table&, double time);
    // Check whether a saved outgoing message has a completed review.
    static bool reviewed(const toml::table&, const std::string& message_id);
};
} // namespace ai_sim
