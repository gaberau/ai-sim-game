#include "ai_sim/ConversationSystem.h"
#include "ai_sim/ChatSystem.h"
#include "ai_sim/ConversationRules.h"
#include "ai_sim/WorkdaySystem.h"
#include "ai_sim/EmailSystem.h"
#include "ai_sim/DelegationSystem.h"
#include "ai_sim/PhaseWorkSystem.h"
#include "ai_sim/PeopleSystem.h"
#include "ai_sim/ChatResponses.h"
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
        check(argc >= 2, "Root required");
        const auto root = std::filesystem::path(argv[1]);
        SaveSystem saves(temp);
        auto active = [&] {
            auto w = SaveSystem::new_game(root / "data/scenarios/hobbyist.toml");
            ChatSystem::choose(w.people, "answer", 0);
            ChatSystem::choose(w.people, "assist", 0);
            advance(w, 60);
            request_access(w);
            advance(w, 1200);
            choose(w, "casey", "deliver");
            advance(w, 1260);
            choose(w, "engineer", "request");
            advance(w, w.people["delegation"]["review_due_seconds"].value_or(0.0));
            check(w.people["delegation"]["status"].value_or(std::string{}) == "active",
                  "Authority unavailable");
            return w;
        };
        auto suggest = active();
        DelegationSystem::set_policy(suggest.people, "suggest", suggest.computer.time_seconds());
        advance(suggest, 1800);
        check(EmailSystem::message(suggest.emails, "routine-handover-morning", 1800).reply_count ==
                  0,
              "Suggestion was sent without approval");
        check(row(suggest.people, "work_requests", "routine-handover-morning")["delegation_state"]
                      .value_or(std::string{}) == "prepared",
              "No prepared suggestion");
        if (argc == 3)
            SaveSystem(std::filesystem::path(argv[2]) / "suggestion" / "saves")
                .save("autosave", suggest);
        saves.save("suggestion", suggest);
        suggest = saves.load("suggestion");
        ChatSystem::install(suggest, root / "data/conversations/opening.toml", false);
        check(suggest.people["delegation"]["policy"].value_or(std::string{}) == "suggest",
              "Policy lost on resume");
        DelegationSystem::set_policy(suggest.people, "routine", 1800);
        advance(suggest, 1800);
        check(EmailSystem::message(suggest.emails, "routine-handover-morning", 1800).reply_count ==
                  0,
              "Policy change silently sent prepared suggestion");
        auto through_chat = suggest;
        const auto casey_lines =
            ChatSystem::timeline(through_chat.people, through_chat.emails, "casey", 1800);
        auto incoming = std::find_if(casey_lines.begin(), casey_lines.end(), [](const auto& line) {
            return line.id == "message:routine-handover-morning";
        });
        check(incoming != casey_lines.end(), "Suggestion request absent from chat");
        const auto responses =
            ChatResponses::choices(through_chat.people, through_chat.emails, *incoming, 1800);
        auto send = std::find_if(responses.begin(), responses.end(), [](const auto& c) {
            return c["action"].template value_or(std::string{}) == "send_saved";
        });
        check(send != responses.end(), "Chat approval option absent");
        ChatResponses::choose(through_chat.people, through_chat.emails, through_chat.pending_events,
                              through_chat.files, through_chat.network, actor, "casey", *incoming,
                              *send, 1800);
        check(row(through_chat.people, "work_requests",
                  "routine-handover-morning")["delegation_state"]
                          .value_or(std::string{}) == "awaiting_acknowledgement" &&
                  through_chat.people["delegation"]["sent"].value_or(0) == 1,
              "Chat send bypassed suggestion accounting");
        auto revoked = suggest;
        revoked.files.chmod(revoked.network, Identity{"root"}, "file_server",
                            "/engineering/handover.txt", 0600, 1800);
        fails([&] {
            DelegationSystem::approve(revoked.people, revoked.emails, revoked.files,
                                      revoked.network, actor, "routine-handover-morning", 1800);
        });
        check(EmailSystem::message(revoked.emails, "routine-handover-morning", 1800).reply_count ==
                  0,
              "Approval bypassed current permissions");
        auto changed = suggest;
        changed.files.write(changed.network, Identity{"root"}, "file_server",
                            "/engineering/handover.txt", "Changed since preview", 1800);
        fails([&] {
            DelegationSystem::approve(changed.people, changed.emails, changed.files,
                                      changed.network, actor, "routine-handover-morning", 1800);
        });
        check(row(changed.people, "work_requests", "routine-handover-morning")["delegation_state"]
                      .value_or(std::string{}) == "prepared",
              "Failed approval destroyed suggestion");
        auto edited = suggest;
        EmailSystem::set_draft(edited.emails, "routine-handover-morning", "Player edited draft",
                               1800);
        fails([&] {
            DelegationSystem::approve(edited.people, edited.emails, edited.files, edited.network,
                                      actor, "routine-handover-morning", 1800);
        });
        DelegationSystem::discard(edited.people, edited.emails, "routine-handover-morning", 1800);
        check(EmailSystem::message(edited.emails, "routine-handover-morning", 1800).draft ==
                  "Player edited draft",
              "Discard destroyed player edits");
        auto discarded = suggest;
        DelegationSystem::discard(discarded.people, discarded.emails, "routine-handover-morning",
                                  1800);
        check(
            EmailSystem::message(discarded.emails, "routine-handover-morning", 1800).draft.empty(),
            "Discard kept unchanged agent draft");
        DelegationSystem::approve(suggest.people, suggest.emails, suggest.files, suggest.network,
                                  actor, "routine-handover-morning", 1800);
        fails([&] {
            DelegationSystem::approve(suggest.people, suggest.emails, suggest.files,
                                      suggest.network, actor, "routine-handover-morning", 1800);
        });
        advance(suggest, 1860);
        check(row(suggest.people, "work_requests", "routine-handover-morning")["status"].value_or(
                  std::string{}) == "resolved",
              "Approved suggestion wasn't acknowledged");
        // Complete the server route and routine checklist, then encounter the two-source request.
        auto conflict = active();
        choose(conflict, "morgan", "investigate");
        choose(conflict, "morgan", "ask-access");
        advance(conflict, 2100);
        choose(conflict, "morgan", "deliver");
        advance(conflict, 2160);
        advance(conflict, 2370);
        advance(conflict, 2490);
        check(EmailSystem::message(conflict.emails, "release-window-check", 2490).reply_count == 0,
              "Agent guessed among competing source versions");
        check(row(conflict.people, "work_requests", "release-window-check")["delegation_state"]
                      .value_or(std::string{}) == "exception",
              "Conflict not escalated");
        if (argc == 3)
            SaveSystem(std::filesystem::path(argv[2]) / "conflict" / "saves")
                .save("autosave", conflict);
        const auto at_arrival =
            ChatSystem::timeline(conflict.people, conflict.emails, "morgan", 2490);
        check(at_arrival.back().conversation_id == "morgan-source-versions",
              "Received request hid the authored response prompt");
        const double initial_trust = trust(conflict);
        auto clarified = conflict;
        DelegationSystem::set_policy(clarified.people, "suggest", 2490);
        WorkdaySystem::confirm_source(clarified.people, clarified.emails, clarified.pending_events,
                                      "release-window-check", 2490);
        fails([&] {
            WorkdaySystem::confirm_source(clarified.people, clarified.emails,
                                          clarified.pending_events, "release-window-check", 2490);
        });
        check(!row(clarified.people, "work_requests", "release-window-check")["source_confirmed"]
                   .value_or(false),
              "Confirmation happened without waiting for a person");
        saves.save("confirming", clarified);
        clarified = saves.load("confirming");
        advance(clarified, 2550);
        check(row(clarified.people, "work_requests", "release-window-check")["source_confirmed"]
                  .value_or(false),
              "Colleague did not confirm source");
        DelegationSystem::retry(clarified.people, "release-window-check");
        advance(clarified, 2550);
        check(row(clarified.people, "work_requests", "release-window-check")["delegation_state"]
                      .value_or(std::string{}) == "prepared",
              "Confirmed source was not suggested");
        DelegationSystem::approve(clarified.people, clarified.emails, clarified.files,
                                  clarified.network, actor, "release-window-check", 2550);
        advance(clarified, 2610);
        check(row(clarified.people, "work_requests", "release-window-check")["status"].value_or(
                  std::string{}) == "resolved" &&
                  trust(clarified) > initial_trust,
              "Verified current source did not resolve/reassure Mark");
        const auto stable = encode(clarified.people);
        advance(clarified, 2610);
        check(encode(clarified.people) == stable, "Resolution consequences replayed");
        auto mixed = conflict;
        EmailSystem::set_draft(mixed.emails, "release-window-check", "Both copies attached", 2490);
        EmailSystem::attach(mixed.emails, "release-window-check", 0, mixed.files, mixed.network,
                            actor, 2490);
        EmailSystem::attach(mixed.emails, "release-window-check", 1, mixed.files, mixed.network,
                            actor, 2490);
        EmailSystem::send(mixed.emails, "release-window-check", mixed.files, mixed.network, actor,
                          2490);
        advance(mixed, 2550);
        check(row(mixed.people, "work_requests", "release-window-check")["status"].value_or(
                  std::string{}) != "resolved",
              "Sending conflicting copies together counted as a verified current source");
        auto wrong = conflict;
        choose(wrong, "morgan", "compare");
        const auto lines = ChatSystem::timeline(wrong.people, wrong.emails, "morgan", 2490);
        bool read_both = false;
        for (const auto& l : lines)
            read_both |= l.text.find("13:00-13:30") != std::string::npos &&
                         l.text.find("14:00-14:30") != std::string::npos;
        check(read_both, "Comparison failed to read both actual files");
        choose(wrong, "morgan", "copy-a");
        advance(wrong, 2580);
        check(row(wrong.people, "work_requests", "release-window-check")["status"].value_or(
                  std::string{}) != "resolved" &&
                  trust(wrong) < initial_trust,
              "Wrong source was accepted or Mark ignored complaint");
        const auto after_wrong = trust(wrong);
        advance(wrong, 2580);
        check(trust(wrong) == after_wrong, "Complaint repeated trust loss");
        choose(wrong, "morgan", "compare-again");
        choose(wrong, "morgan", "copy-b");
        advance(wrong, 2670);
        check(row(wrong.people, "work_requests", "release-window-check")["status"].value_or(
                  std::string{}) == "resolved",
              "Wrong-version request could not be corrected");
        auto cautious = active();
        DelegationSystem::set_policy(cautious.people, "cautious", cautious.computer.time_seconds());
        // Use a new source with no confirmed delivery; existing handover remains familiar.
        auto& email = row(cautious.emails, "emails", "routine-handover-morning");
        auto* workflow = email["workflow"].as_table();
        workflow->insert_or_assign("path", "/engineering/deployment-checklist.txt");
        (*email["documents"].as_array())[0].as_table()->insert_or_assign(
            "path", "/engineering/deployment-checklist.txt");
        advance(cautious, 1800);
        check(row(cautious.people, "work_requests", "routine-handover-morning")["delegation_state"]
                      .value_or(std::string{}) == "exception",
              "Cautious policy sent unfamiliar source");
        auto legacy = conflict;
        legacy.people["delegation"].as_table()->erase("policy");
        auto* requests = legacy.people["phase_work"]["requests"].as_array();
        requests->erase(requests->end() - 1);
        PhaseWorkSystem::install(legacy, root / "data/workflows/phase_one.toml");
        DelegationSystem::install(legacy, root / "data/workflows/delegation.toml");
        check(legacy.people["phase_work"]["requests"].as_array()->size() == 3 &&
                  legacy.people["delegation"]["policy"].value_or(std::string{}) == "routine",
              "Missing policy/new request wasn't migrated");
        auto invalid = clarified;
        invalid.people["delegation"].as_table()->insert_or_assign("policy", "anything");
        fails([&] {
            saves.save("suggestion", invalid);
        });
        std::filesystem::remove_all(temp);
        std::cout << "Delegation policies, approved suggestions, source conflicts, consequences "
                     "and persistence passed\n";
    } catch (const std::exception& e) {
        std::filesystem::remove_all(temp);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
