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
    static void initialize(toml::table& people);
    static void install(toml::table& people, const std::filesystem::path& definition);
    static void observe(toml::table& people, const toml::table& emails, double time);
    static void observe_chat(toml::table& people);
    static void record_claim(toml::table&, const std::string& person, const toml::table& claim,
                             const std::string& evidence_id, double time);
    static void record_relationship(toml::table&, const std::string& from, const std::string& to,
                                    double delta, const std::string& label,
                                    const std::string& evidence_id, double time);
    static void validate(const toml::table& people);
    static double response_delay(const toml::table& person, std::string* explanation = nullptr);
    static double queue_response(toml::table& person, double time,
                                 std::string* explanation = nullptr);
    static bool allows_access(const toml::table& person, std::string* explanation = nullptr);
    static std::vector<std::string> known_ids(const toml::table& people);
    static const toml::table* dossier(const toml::table& people, const std::string& id);
    static std::string name(const toml::table& people, const std::string& id);
};
} // namespace ai_sim
