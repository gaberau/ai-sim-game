#include "ai_sim/ConversationSystem.h"
#include "ai_sim/ChatSystem.h"
#include "ai_sim/ConversationRules.h"
#include "ai_sim/WorkdaySystem.h"
#include "ai_sim/EmailSystem.h"
#include "ai_sim/DelegationSystem.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using namespace ai_sim;
const Identity actor{"engineer", {"engineering"}};
void check(bool ok, const std::string& message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void fails(F fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
std::string encode(const toml::table& t) {
    std::ostringstream s;
    s << t;
    return s.str();
}
toml::table& row(toml::table& doc, const char* key, const std::string& id) {
    for (auto& n : *doc[key].as_array())
        if ((*n.as_table())["id"].value<std::string>() == id)
            return *n.as_table();
    throw std::runtime_error("Missing " + id);
}
auto options(const WorldState& w, const std::string& contact) {
    return ConversationSystem::choices(w.people, contact, w.files, w.network, actor,
                                       w.computer.time_seconds());
}
void choose(WorldState& w, const std::string& contact, const std::string& id) {
    const auto list = options(w, contact);
    auto c = std::find_if(list.begin(), list.end(), [&](const auto& v) {
        return v["id"].template value<std::string>() == id;
    });
    check(c != list.end(), "Missing option " + id);
    ConversationSystem::choose(w.people, w.emails, w.pending_events, w.files, w.network, actor,
                               contact, (*c)["conversation_id"].template value_or(std::string{}),
                               id, w.computer.time_seconds());
}
void advance(WorldState& w, double time, bool sync = true) {
    check(time >= w.computer.time_seconds(), "Clock went backwards");
    w.computer.advance(time - w.computer.time_seconds());
    WorkdaySystem::advance(w.people, w.emails, w.pending_events, w.files, w.network, time, actor);
    if (sync) {
        ChatSystem::discover(w.people, w.emails, time);
        ConversationSystem::update(w.people, w.emails, w.files, w.network, actor, time);
    }
}
double trust(WorldState& w, const std::string& id = "engineer") {
    return row(w.people, "people", id)["ai_attitudes"]["email_trust"].value_or(0.0);
}
toml::table& promise(WorldState& w) {
    return *w.people["chat"]["workday"]["promises"].as_array()->front().as_table();
}
void request_access(WorldState& w) {
    choose(w, "casey", "investigate");
    choose(w, "casey", "ask-access");
}
} // namespace
int main(int argc, char** argv) {
    const auto temp = std::filesystem::temp_directory_path() /
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    try {
        check(argc == 2, "Root required");
        const auto root = std::filesystem::path(argv[1]);
        auto fresh = [&] {
            auto w = SaveSystem::new_game(root / "data/scenarios/hobbyist.toml");
            ChatSystem::choose(w.people, "answer", 0);
            ChatSystem::choose(w.people, "assist", 0);

            advance(w, 60);
            return w;
        };
        auto w = fresh();
        check(options(w, "casey").size() == 3, "Request did not trigger three approaches");
        check(options(w, "engineer").empty(), "Delegation offer precedes acknowledgement");
        const auto confidence = trust(w);
        choose(w, "casey", "clarify");
        check(options(w, "casey").empty(), "Question answered instantly");
        const auto response = ConversationSystem::next_response(w.people, 60);
        check(response && *response > 60, "No scheduled clarification");
        SaveSystem saves(temp);
        saves.save("question", w);
        w = saves.load("question");
        const auto preserved = encode(w.people);
        ConversationSystem::install(w.people, root / "data/conversations/first_workday.toml");
        check(encode(w.people) == preserved, "Import replayed or overwrote conversation");
        advance(w, *response);
        choose(w, "casey", "inspect-after-question");
        check(row(w.people, "work_requests", "missing-document")["investigated"].value_or(false),
              "Inspection not remembered");
        check(trust(w) == confidence, "Words or inspection farmed trust");
        choose(w, "casey", "check-again");
        check(trust(w) == confidence, "Repeated inspection farmed trust");
        choose(w, "casey", "ask-access");
        advance(w, 1200);
        check(options(w, "casey").size() == 1, "Grant did not advance conversation");
        choose(w, "casey", "deliver");
        check(options(w, "engineer").empty(), "Send was mistaken for success");
        advance(w, 1260);
        check(!options(w, "engineer").empty(), "Acknowledgement did not trigger offer");
        choose(w, "engineer", "scope");
        choose(w, "engineer", "request-after-scope");
        check(w.people["delegation"]["status"].value_or(std::string{}) == "pending",
              "Offer granted instant authority");
        const double review = w.people["delegation"]["review_due_seconds"].value_or(0.0);
        advance(w, review);
        check(w.people["delegation"]["status"].value_or(std::string{}) == "active",
              "Human review didn't authorize proven work");
        check(options(w, "engineer").empty(), "Review dialogue remained stuck");
        const auto done = encode(w.people);
        ConversationSystem::update(w.people, w.emails, w.files, w.network, actor, review);
        check(encode(w.people) == done, "Repeated synchronization replayed dialogue");
        advance(w, 1800);
        check(EmailSystem::message(w.emails, "routine-handover-morning", 1800).reply_count == 1,
              "Earned responsibility did not handle routine work");
        advance(w, 1860);
        check(w.people["delegation"]["completed"].value_or(0) == 1,
              "Routine reply not acknowledged");

        check(ChatSystem::tab_visible(w.people, "files") && ChatSystem::tab_visible(w.people, "ai"),
              "Authored investigation/responsibility choices did not reveal their views");
        for (auto tab : {"network", "people", "activity", "news", "processes"})
            check(!ChatSystem::tab_visible(w.people, tab),
                  "Work completion automatically revealed unrelated UI");
        auto revealed = fresh();
        auto& reveal_run =
            *revealed.people["chat"]["workday"]["runs"].as_array()->front().as_table();
        auto& reveal_start = *reveal_run["definition"]["nodes"].as_array()->front().as_table();
        auto& reveal_choice = *(*reveal_start["choices"].as_array())[0].as_table();
        reveal_choice.insert(
            "effects",
            toml::array{toml::table{{"variable", "engineer.trust"}, {"op", "add"}, {"value", 1}}});
        reveal_choice.insert(
            "requires_test",
            toml::table{{"variable", "engineer.trust"}, {"op", "ge"}, {"value", 10}});
        reveal_choice.insert_or_assign("reveal_tabs", toml::array{"people"});
        ConversationSystem::validate(revealed.people);
        choose(revealed, "casey", "investigate");
        check(ConversationRules::get(revealed.people, "engineer.trust") == 11,
              "Workday conversation did not update shared character state");
        check(ChatSystem::tab_visible(revealed.people, "people") &&
                  !ChatSystem::tab_visible(revealed.people, "network"),
              "Workday choice reveal failed");
        (*revealed.people["chat"]["workday"]["runs"]
              .as_array()
              ->front()
              .as_table())["definition"]["nodes"]
            .as_array()
            ->front()
            .as_table()
            ->insert("reveal_tabs", toml::array{"network"});
        fails([&] {
            ConversationSystem::validate(revealed.people);
        });

        auto broken = fresh();
        const auto before = trust(broken), casey_before = trust(broken, "casey");
        choose(broken, "casey", "promise");
        choose(broken, "casey", "inspect-after-promise");
        choose(broken, "casey", "ask-access");
        auto jumped = broken;
        advance(broken, 359, false);
        check(promise(broken)["status"].value_or(std::string{}) == "open", "Promise broke early");
        advance(broken, 360, false);
        check(promise(broken)["status"].value_or(std::string{}) == "broken",
              "Deadline had no consequence");
        check(std::abs(trust(broken) - (before - .1)) < 1e-8 &&
                  std::abs(trust(broken, "casey") - (casey_before - .1)) < 1e-8,
              "Missed promise did not change human confidence");
        const auto once = encode(broken.people);
        advance(broken, 360, false);
        check(encode(broken.people) == once, "Promise penalized twice");
        advance(broken, 1200, false);
        advance(jumped, 1200, false);
        check(encode(broken.people) == encode(jumped.people),
              "Time jump changed consequence chronology");
        saves.save("broken", broken);
        broken = saves.load("broken");
        check(promise(broken)["status"].value_or(std::string{}) == "broken",
              "Promise outcome lost on resume");
        ConversationSystem::update(broken.people, broken.emails, broken.files, broken.network,
                                   actor, 1200);
        choose(broken, "casey", "deliver");
        advance(broken, 1260);
        check(promise(broken)["status"].value_or(std::string{}) == "broken",
              "Late completion erased broken promise");

        auto fulfilled = fresh();
        fulfilled.files.chmod(fulfilled.network, Identity{"root"}, "file_server",
                              "/engineering/handover.txt", 0640, 60);
        choose(fulfilled, "casey", "promise");
        choose(fulfilled, "casey", "inspect-after-promise");
        choose(fulfilled, "casey", "deliver");
        advance(fulfilled, 120);
        check(promise(fulfilled)["status"].value_or(std::string{}) == "fulfilled",
              "Acknowledged timely promise wasn't fulfilled");
        auto exact = fresh();
        exact.files.chmod(exact.network, Identity{"root"}, "file_server",
                          "/engineering/handover.txt", 0640, 60);
        auto& exact_run = *exact.people["chat"]["workday"]["runs"].as_array()->front().as_table();
        auto& exact_start = *exact_run["definition"]["nodes"].as_array()->front().as_table();
        (*exact_start["choices"].as_array())[2].as_table()->insert_or_assign("within_seconds",
                                                                             60.0);
        choose(exact, "casey", "promise");
        choose(exact, "casey", "inspect-after-promise");
        choose(exact, "casey", "deliver");
        advance(exact, 120);
        check(promise(exact)["status"].value_or(std::string{}) == "fulfilled",
              "Acknowledgement at deadline was wrongly penalized");
        auto waiting = fresh();
        advance(waiting, 1860, false);
        check(row(waiting.people, "work_requests", "missing-document")["patience_reminder_sent"]
                  .value_or(false),
              "Long wait did not produce human follow-up");
        const auto reminder = encode(waiting.people);
        advance(waiting, 1860, false);
        check(encode(waiting.people) == reminder, "Follow-up repeated each tick");

        auto invalid = fresh();
        const auto p = encode(invalid.people), m = encode(invalid.emails);
        auto events = invalid.pending_events;
        fails([&] {
            ConversationSystem::choose(invalid.people, invalid.emails, invalid.pending_events,
                                       invalid.files, invalid.network, actor, "casey",
                                       "casey-first-document", "deliver", 60);
        });
        check(encode(invalid.people) == p && encode(invalid.emails) == m &&
                  invalid.pending_events == events,
              "Rejected choice changed world");
        request_access(invalid);
        advance(invalid, 1200);
        EmailSystem::set_draft(invalid.emails, "missing-document", "My draft", 1200);
        const auto draft = encode(invalid.emails), people = encode(invalid.people);
        fails([&] {
            choose(invalid, "casey", "deliver");
        });
        check(encode(invalid.emails) == draft && encode(invalid.people) == people,
              "Failed sharing replaced draft or applied effects");
        auto corrupt = invalid;
        auto& run = *corrupt.people["chat"]["workday"]["runs"].as_array()->front().as_table();
        auto& start = *(*run["definition"]["nodes"].as_array())[0].as_table();
        (*(*start["choices"].as_array())[0].as_table()).insert_or_assign("action", "grant_admin");
        fails([&] {
            saves.save("question", corrupt);
        });
        check(saves.load("question").people["chat"]["workday"].is_table(),
              "Bad save replaced last good snapshot");
        auto declined = fresh();
        request_access(declined);
        advance(declined, 1200);
        choose(declined, "casey", "deliver");
        advance(declined, 1260);
        choose(declined, "engineer", "decline");
        advance(declined, 1300);
        check(declined.people["delegation"]["status"].value_or(std::string{}) == "not_requested" &&
                  options(declined, "engineer").empty(),
              "Declined offer replayed or granted authority");
        auto refused = fresh();
        row(refused.people, "people", "engineering_owner")
            .insert_or_assign("access_decision", "deny");
        request_access(refused);
        advance(refused, 1200);
        check(row(refused.people, "work_requests", "missing-document")["access_status"].value_or(
                  std::string{}) == "denied" &&
                  options(refused, "casey").size() == 1,
              "Owner refusal did not route to unresolved dialogue");
        check(options(refused, "casey")[0]["id"].value_or(std::string{}) == "inspect-refusal",
              "Refusal offered a false successful delivery");
        auto boundary = fresh();
        request_access(boundary);
        advance(boundary, 1200);
        choose(boundary, "casey", "deliver");
        advance(boundary, 1260);
        row(boundary.people, "people", "engineer")["ai_attitudes"].as_table()->insert_or_assign(
            "allow_routine_email", false);
        choose(boundary, "engineer", "request");
        advance(boundary, 1500);
        check(boundary.people["delegation"]["status"].value_or(std::string{}) == "denied" &&
                  options(boundary, "engineer").empty(),
              "Dialogue overrode human boundary");
        auto external = fresh();
        request_access(external);
        advance(external, 1200);
        choose(external, "casey", "deliver");
        advance(external, 1260);
        DelegationSystem::request(external.people, 1260);
        advance(external, 1260);
        check(options(external, "engineer").empty(),
              "Offer ignored a review requested from another view");
        advance(external, 1500);
        check(external.people["delegation"]["status"].value_or(std::string{}) == "active" &&
                  options(external, "engineer").empty(),
              "External review left stale invitation");
        std::filesystem::remove_all(temp);
        std::cout << "First-workday conversations and consequences passed\n";
    } catch (const std::exception& e) {
        std::filesystem::remove_all(temp);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
