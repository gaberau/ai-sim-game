// Module: Separate simulated personal reality from player-visible dossiers and evidence.
#pragma once
#include <toml++/toml.hpp>
#include <filesystem>
#include <string>
#include <vector>

namespace ai_sim {
// Reality is in people/relationships. Player-visible knowledge is exclusively
// in dossiers/observations/known_relationships. Both persist in people.toml.
class PeopleSystem {
  public:
    // Create required knowledge/state containers without exposing private personal facts.
    static void initialize(toml::table& people);
    // Load authored definitions and initialize this subsystem's saved state.
    static void install(toml::table& people, const std::filesystem::path& definition);
    // Record player-visible evidence from eligible incoming messages.
    static void observe(toml::table& people, const toml::table& emails, double time);
    // Record knowledge supported by the saved chat transcript.
    static void observe_chat(toml::table& people);
    // Save an attributed claim with evidence and game-time provenance.
    static void record_claim(toml::table&, const std::string& person, const toml::table& claim,
                             const std::string& evidence_id, double time);
    // Save an evidence-backed relationship observation and its confidence change.
    static void record_relationship(toml::table&, const std::string& from, const std::string& to,
                                    double delta, const std::string& label,
                                    const std::string& evidence_id, double time);
    // Reject invalid subsystem state before it is used or restored.
    static void validate(const toml::table& people);
    // Calculate service duration in game seconds from workload, fatigue and motivation.
    static double response_delay(const toml::table& person, std::string* explanation = nullptr);
    // Reserve a human response slot around existing commitments and working hours.
    static double queue_response(toml::table& person, double time,
                                 std::string* explanation = nullptr);
    // Evaluate authored human access policy and optionally explain the decision.
    static bool allows_access(const toml::table& person, std::string* explanation = nullptr);
    // Return person IDs currently known to the player, rather than all simulated people.
    static std::vector<std::string> known_ids(const toml::table& people);
    // Borrow the player-visible dossier, or return null when it is not known.
    static const toml::table* dossier(const toml::table& people, const std::string& id);
    // Resolve a display name from the available record, falling back to its stable ID.
    static std::string name(const toml::table& people, const std::string& id);
};
} // namespace ai_sim
