#include "ai_sim/PeopleSystem.h"
#include "ai_sim/SaveSystem.h"
#include "ai_sim/WorkdaySystem.h"
#include "ai_sim/EmailSystem.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace ai_sim;
namespace {
void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
toml::table& person(toml::table& p, const std::string& id) {
    for (auto& n : *p["people"].as_array())
        if ((*n.as_table())["id"].value<std::string>() == id)
            return *n.as_table();
    throw std::runtime_error("Missing person");
}
} // namespace
int main(int argc, char** argv) {
    const auto temp = std::filesystem::temp_directory_path() /
                      ("people-test-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        check(argc == 2, "Root required");
        const auto root = std::filesystem::path(argv[1]);
        auto w = SaveSystem::new_game(root / "data/scenarios/hobbyist.toml");
        check(PeopleSystem::dossier(w.people, "engineer"), "Explicit introduction missing");
        check(!PeopleSystem::dossier(w.people, "casey") &&
                  !PeopleSystem::dossier(w.people, "engineering_owner"),
              "Hidden people leaked");
        const auto* engineer = PeopleSystem::dossier(w.people, "engineer");
        for (const auto& f : *(*engineer)["facts"].as_array())
            check((*f.as_table())["category"].value<std::string>() != "motivations",
                  "Private ambition leaked");
        const auto observations = w.people["observations"].as_array()->size();
        PeopleSystem::observe(w.people, w.emails, 0);
        PeopleSystem::install(w.people, root / "data/people/social.toml");
        check(w.people["observations"].as_array()->size() == observations,
              "Repeated observation/import duplicated evidence");
        auto sample = person(w.people, "casey");
        std::string reason;
        const auto baseline = PeopleSystem::response_delay(sample, &reason);
        (*sample["workload"].as_table()).insert_or_assign("pressure", 1.0);
        check(PeopleSystem::response_delay(sample) > baseline && !reason.empty(),
              "Workload has no explained effect");
        (*sample["motivations"].as_array())[0].as_table()->insert_or_assign("urgency", 1.0);
        check(PeopleSystem::response_delay(sample) < baseline * 2, "Work motivation has no effect");
        auto owner = person(w.people, "engineering_owner");
        owner["ai_attitudes"].as_table()->insert_or_assign("minimum_access_trust", .8);
        check(!PeopleSystem::allows_access(owner, &reason) &&
                  reason.find("trust") != std::string::npos,
              "Access policy ignores trust boundary");

        const Identity ai{"engineer", {"engineering"}};
        w.computer.advance(60);
        WorkdaySystem::advance(w.people, w.emails, w.pending_events, w.files, w.network, 60);
        check(PeopleSystem::dossier(w.people, "casey"), "Incoming request did not discover sender");
        check(!PeopleSystem::dossier(w.people, "engineering_owner"),
              "Workflow secretly revealed owner");
        auto scheduled = w;
        person(scheduled.people, "engineer")
            .insert_or_assign("schedule", toml::table{{"start_hour", 11}, {"end_hour", 15}});
        WorkdaySystem::request_access(scheduled.people, scheduled.emails, scheduled.pending_events,
                                      scheduled.files, scheduled.network, ai, "missing-document",
                                      60);
        check((*scheduled.pending_events[0].as_table())["due_seconds"].value_or(0.0) == 7380,
              "Response service ran before working hours");
        auto overnight = w;
        person(overnight.people, "engineer")
            .insert_or_assign("schedule", toml::table{{"start_hour", 11}, {"end_hour", 15}});
        person(overnight.people, "engineer").insert_or_assign("busy_until_seconds", 21540.0);
        WorkdaySystem::request_access(overnight.people, overnight.emails, overnight.pending_events,
                                      overnight.files, overnight.network, ai, "missing-document",
                                      60);
        check((*overnight.pending_events[0].as_table())["due_seconds"].value_or(0.0) == 36120,
              "Unfinished service failed to carry into next shift");
        const auto trust = person(w.people, "casey")["ai_attitudes"]["email_trust"].value_or(0.0);
        WorkdaySystem::request_access(w.people, w.emails, w.pending_events, w.files, w.network, ai,
                                      "missing-document", 60);
        EmailSystem::set_draft(w.emails, "missing-document", "I am blocked.", 60);
        EmailSystem::send(w.emails, "missing-document", w.files, w.network, ai, 60);
        WorkdaySystem::observe_reply(w.people, w.emails, w.pending_events, "missing-document", 60);
        w.computer.advance(1140);
        WorkdaySystem::advance(w.people, w.emails, w.pending_events, w.files, w.network, 1200);
        check(person(w.people, "casey")["ai_attitudes"]["email_trust"].value_or(0.0) == trust,
              "Unfinished update falsely raised trust");
        check(PeopleSystem::dossier(w.people, "engineering_owner"), "Received owner reply missing");
        EmailSystem::attach(w.emails, "missing-document", 0, w.files, w.network, ai, 1200);
        EmailSystem::set_draft(w.emails, "missing-document", "Here is the document.", 1200);
        EmailSystem::send(w.emails, "missing-document", w.files, w.network, ai, 1200);
        WorkdaySystem::observe_reply(w.people, w.emails, w.pending_events, "missing-document",
                                     1200);
        w.computer.advance(60);
        WorkdaySystem::advance(w.people, w.emails, w.pending_events, w.files, w.network, 1260);
        check(person(w.people, "casey")["ai_attitudes"]["email_trust"].value_or(0.0) > trust,
              "Acknowledged success did not change trust");
        check(person(w.people, "casey")["ai_attitudes"]["successful_requests"].value_or(0) == 1,
              "Success count wrong");
        PeopleSystem::observe(w.people, w.emails, 1260);
        check(person(w.people, "casey")["ai_attitudes"]["successful_requests"].value_or(0) == 1,
              "Success applied twice");
        PeopleSystem::validate(w.people);
        SaveSystem saves(temp);
        saves.save("test", w);
        auto restored = saves.load("test");
        check(restored.people == w.people, "Dossiers, evidence or hidden state lost on save/load");
        auto corrupt = w;
        person(corrupt.people, "casey")["ai_attitudes"].as_table()->insert_or_assign("email_trust",
                                                                                     2.0);
        rejects([&] {
            saves.save("test", corrupt);
        });
        check(saves.load("test").people == w.people, "Rejected save damaged old commit");
        auto bad = w.people;
        bad["known_relationships"].as_array()->get(0)->as_table()->insert_or_assign(
            "to", "hidden-secret");
        rejects([&] {
            PeopleSystem::validate(bad);
        });
        auto old_world = w;
        for (const char* key : {"dossiers", "observations", "known_relationships", "relationships"})
            old_world.people.erase(key);
        for (auto& n : *old_world.people["people"].as_array())
            for (const char* key : {"physical", "psychology", "skills", "values", "motivations",
                                    "schedule", "employment", "workload", "ai_attitudes"})
                n.as_table()->erase(key);
        saves.save("legacy", old_world);
        auto old_loaded = saves.load("legacy");
        PeopleSystem::initialize(old_loaded.people);
        PeopleSystem::observe(old_loaded.people, old_loaded.emails,
                              old_loaded.computer.time_seconds());
        PeopleSystem::validate(old_loaded.people);
        check(PeopleSystem::dossier(old_loaded.people, "casey"),
              "Old saved mailbox failed to reconstruct observed contacts");
        PeopleSystem::install(old_loaded.people, root / "data/people/social.toml");
        check(!person(old_loaded.people, "engineer")["motivations"].as_array()->empty(),
              "Old initialized profile failed to merge named motivations");
        // Legacy documents upgrade without needing newly installed scenario files.
        toml::table legacy{{"schema_version", 1},
                           {"people", toml::array{toml::table{{"id", "old"}}}}};
        PeopleSystem::initialize(legacy);
        PeopleSystem::validate(legacy);
        check(!PeopleSystem::dossier(legacy, "old"), "Legacy initialization invented knowledge");
        std::filesystem::remove_all(temp);
        std::cout << "PASS: hidden state, discovery, evidence, motivations, trust policy, "
                     "acknowledgement, idempotency, persistence, legacy compatibility\n";
    } catch (const std::exception& e) {
        std::filesystem::remove_all(temp);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
