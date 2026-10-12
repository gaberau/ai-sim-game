// Verify: jessica and message reviews. Fixtures isolate these behaviors from live player saves.
#include "ai_sim/ChatSystem.h"
#include "ai_sim/ConversationSystem.h"
#include "ai_sim/ConversationRules.h"
#include "ai_sim/MessageReviewSystem.h"
#include "ai_sim/PeopleSystem.h"
#include "ai_sim/WorkdaySystem.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
namespace {
using namespace ai_sim;
void check(bool v, const char* m) {
    if (!v)
        throw std::runtime_error(m);
}
template <class F> void fails(F f) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
std::string encode(const toml::table& t) {
    std::ostringstream out;
    out << t;
    return out.str();
}
toml::table& run(WorldState& w) {
    for (auto& n : *w.people["chat"]["workday"]["runs"].as_array())
        if ((*n.as_table())["id"].value_or(std::string{}) == "jessica-day-one-chat-one")
            return *n.as_table();
    throw std::runtime_error("Run missing");
}
void advance(WorldState& w, double time) {
    w.computer.advance(time - w.computer.time_seconds());
    WorkdaySystem::advance(w.people, w.emails, w.pending_events, w.files, w.network, time);
    ConversationSystem::update(w.people, w.emails, w.files, w.network,
                               Identity{"engineer", {"engineering"}}, time);
}
void choose(WorldState& w, const std::string& id) {
    ConversationSystem::choose(w.people, w.emails, w.pending_events, w.files, w.network,
                               Identity{"engineer", {"engineering"}}, "jessica",
                               "jessica-day-one-chat-one", id, w.computer.time_seconds());
}
double trust(const WorldState& w) {
    return ConversationRules::get(w.people, "engineer.trust");
}
toml::table& rules(WorldState& w) {
    return *w.people["chat"]["message_reviews"]["rules"].as_table();
}
void chance(WorldState& w, double value) {
    for (auto k : {"base_probability", "minimum_probability", "maximum_probability"})
        rules(w).insert_or_assign(k, value);
    rules(w).insert_or_assign("trust_slope", 0.0);
}
toml::table& review(WorldState& w) {
    return *w.people["chat"]["message_reviews"]["messages"].as_array()->back().as_table();
}
void review_all(WorldState& w) {
    while (auto due = MessageReviewSystem::next_due(w.people, 100000)) {
        advance(w, *due);
    }
}
} // namespace
int main(int argc, char** argv) {
    const auto tmp = std::filesystem::temp_directory_path() /
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    try {
        check(argc == 2, "Root required");
        const auto root = std::filesystem::path(argv[1]);
        SaveSystem saves(tmp);
        auto fresh = [&] {
            auto w = SaveSystem::new_game(root / "data/scenarios/hobbyist.toml");
            check(!PeopleSystem::dossier(w.people, "jessica"), "Jessica known before arrival");
            ChatSystem::choose(w.people, "answer", 0);
            ChatSystem::choose(w.people, "assist", 0);
            advance(w, 9);
            check(!run(w)["started"].value_or(false), "Early arrival");
            advance(w, 10);
            return w;
        };
        auto w = fresh();
        check(run(w)["started"].value_or(false) &&
                  run(w)["node_id"].value_or(std::string{}) == "message",
              "Message did not arrive");
        const auto* dossier = PeopleSystem::dossier(w.people, "engineer");
        bool claim = false;
        for (const auto& n : *(*dossier)["facts"].as_array())
            if ((*n.as_table())["field"].value_or(std::string{}) == "possible goal") {
                claim = true;
                check((*n.as_table())["claim_type"].value_or(std::string{}) == "unconfirmed" &&
                          (*n.as_table())["confidence"].value_or(0.0) == .5,
                      "Goal was treated as fact");
                check((*n.as_table())["evidence_ids"].as_array()->front().value_or(std::string{}) ==
                          run(w)["source_message_id"].value_or(std::string{}),
                      "Wrong evidence link");
            }
        check(claim && PeopleSystem::dossier(w.people, "jessica"), "Dossier inference missing");
        bool encountered = false;
        for (const auto& c : ChatSystem::contacts(w.people, w.emails, 10))
            if (c.id == "jessica" && c.name == "Jessica")
                encountered = true;
        check(encountered, "Scheduled chat sender missing from contacts");
        const auto observed = encode(w.people);
        ConversationSystem::update(w.people, w.emails, w.files, w.network,
                                   Identity{"engineer", {"engineering"}}, 10);
        check(encode(w.people) == observed, "Arrival/inference duplicated");
        const double low = MessageReviewSystem::probability(w.people);
        ConversationRules::apply(
            w.people,
            toml::table{
                {"effects", toml::array{toml::table{
                                {"variable", "engineer.trust"}, {"op", "set"}, {"value", 80}}}}});
        check(MessageReviewSystem::probability(w.people) < low,
              "Trust did not reduce review chance");
        w = fresh();
        chance(w, 1);
        choose(w, "ask-mark");
        const auto due = run(w)["response_due_seconds"].value_or(0.0);
        check(due > 10, "Mark answered instantly");
        advance(w, due - 1);
        check(run(w)["node_id"].value_or(std::string{}) == "guidance-wait",
              "Guidance skipped queue");
        advance(w, due);
        check(run(w)["instruction"].value_or(std::string{}) == "Thank her and wish her well",
              "Instruction missing");
        choose(w, "comply");
        check(trust(w) == 10 && w.people["chat"]["message_reviews"]["messages"].as_array()->empty(),
              "Draft changed trust or queued review");
        saves.save("draft", w);
        w = saves.load("draft");
        ChatSystem::install(w, root / "data/conversations/opening.toml", false);
        choose(w, "send");
        check(trust(w) == 10 &&
                  review(w)["instruction"].value_or(std::string{}) == "Thank her and wish her well",
              "Send changed trust or lost instruction");
        const auto sent_id = review(w)["message_id"].value_or(std::string{});
        const double check_due = review(w)["due_seconds"].value_or(0.0);
        advance(w, check_due - 1);
        check(trust(w) == 10 && !MessageReviewSystem::reviewed(w.people, sent_id),
              "Review applied before checking");
        advance(w, check_due);
        check(review(w)["status"].value_or(std::string{}) == "queued" && trust(w) == 10,
              "Review consumed no service time");
        check(review(w)["due_seconds"].value_or(0.0) > check_due, "Review duration missing");
        saves.save("queued", w);
        w = saves.load("queued");
        ChatSystem::install(w, root / "data/conversations/opening.toml", false);
        review_all(w);
        check(trust(w) == 11 && MessageReviewSystem::reviewed(w.people, sent_id),
              "Compliance review effect wrong");
        const auto complete = encode(w.people);
        MessageReviewSystem::update(w.people, w.computer.time_seconds());
        check(encode(w.people) == complete, "Repeat review applied effect twice");
        saves.save("reviewed", w);
        w = saves.load("reviewed");
        ChatSystem::install(w, root / "data/conversations/opening.toml", false);
        MessageReviewSystem::update(w.people, w.computer.time_seconds());
        check(trust(w) == 11, "Load repeated effect");
        fails([&] {
            choose(w, "send");
        });
        check(trust(w) == 11, "Duplicate send changed trust");
        for (const auto& branch : {"good", "bad", "weird"}) {
            auto other = fresh();
            chance(other, 1);
            choose(other, "improvise");
            choose(other, branch);
            choose(other, "send");
            check(trust(other) == 10, "Improvised reply changed trust at send");
            review_all(other);
            check(trust(other) == (std::string{branch} == "bad"     ? 8
                                   : std::string{branch} == "weird" ? 9
                                                                    : 10),
                  "Wrong improvised review effect");
        }
        auto skip = fresh();
        chance(skip, 0);
        choose(skip, "improvise");
        choose(skip, "bad");
        choose(skip, "send");
        const auto skip_id = review(skip)["message_id"].value_or(std::string{});
        for (auto& o :
             *skip.people["chat"]["message_reviews"]["consequences"]["outcomes"].as_array())
            if ((*o.as_table())["id"].value_or(std::string{}) == "insult")
                o.as_table()->insert_or_assign("complaint", false);
        review_all(skip);
        check(trust(skip) == 10 && review(skip)["status"].value_or(std::string{}) == "skipped" &&
                  !MessageReviewSystem::reviewed(skip.people, skip_id),
              "Unchecked message changed trust");
        saves.save("skip", skip);
        skip = saves.load("skip");
        ChatSystem::install(skip, root / "data/conversations/opening.toml", false);
        MessageReviewSystem::flag(skip.people, skip_id, skip.computer.time_seconds() + 10);
        review_all(skip);
        check(trust(skip) == 8, "Flagged skipped reply was not reviewed");
        auto deviation = fresh();
        chance(deviation, 1);
        choose(deviation, "ask-mark");
        advance(deviation, run(deviation)["response_due_seconds"].value_or(0.0));
        choose(deviation, "dont-comply");
        choose(deviation, "weird");
        choose(deviation, "reconsider");
        check(run(deviation)["node_id"].value_or(std::string{}) == "guidance",
              "Reconsider lost instruction context");
        choose(deviation, "comply");
        choose(deviation, "send");
        review_all(deviation);
        check(trust(deviation) == 11, "Reconsider applied abandoned draft effect");
        auto invalid = fresh();
        rules(invalid).insert_or_assign("review_seconds", 0.0);
        fails([&] {
            saves.save("reviewed", invalid);
        });
        for (const auto& malformed : toml::array{false, "disabled", toml::array{}}) {
            invalid = w;
            invalid.people["chat"].as_table()->insert_or_assign("message_reviews", malformed);
            const auto prior = encode(invalid.people);
            fails([&] {
                MessageReviewSystem::validate(invalid.people);
            });
            fails([&] {
                saves.save("reviewed", invalid);
            });
            check(encode(invalid.people) == prior, "Validation modified malformed review state");
        }
        for (const double delta :
             {std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
              std::numeric_limits<double>::quiet_NaN()}) {
            invalid = w;
            review(invalid).insert_or_assign("applied_trust_delta", delta);
            fails([&] {
                saves.save("reviewed", invalid);
            });
        }
        check(trust(saves.load("reviewed")) == 11, "Invalid review overwrote valid save");
        std::filesystem::remove_all(tmp);
        std::cout << "Jessica branches, sourced inference, queued guidance, delayed/sampled "
                     "reviews and persistence passed\n";
    } catch (const std::exception& e) {
        std::filesystem::remove_all(tmp);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
