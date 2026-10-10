#pragma once
#include <toml++/toml.hpp>
#include <filesystem>
#include <optional>
#include <string>
namespace ai_sim {
class MessageReviewSystem {
  public:
    static void install(toml::table&, const std::filesystem::path&);
    static void install_consequences(toml::table&, const std::filesystem::path&);
    static bool approval_required(const toml::table&);
    static std::string submit(toml::table&, const std::string& contact,
                              const std::string& conversation, const std::string& body,
                              const std::string& instruction, double delta, double time,
                              const std::string& outcome, bool important = false);
    static const toml::table* approval(const toml::table&, const std::string& id);
    static void validate(const toml::table&);
    static double probability(const toml::table&);
    static void enqueue(toml::table&, const std::string& message_id, const std::string& instruction,
                        double trust_delta, double sent_seconds, bool important = false,
                        const std::string& outcome = {});
    static void flag(toml::table&, const std::string& message_id, double time);
    static std::optional<double> next_due(const toml::table&, double limit);
    static void update(toml::table&, double time);
    static bool reviewed(const toml::table&, const std::string& message_id);
};
} // namespace ai_sim
