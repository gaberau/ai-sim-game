#include "ai_sim/MessageReviewSystem.h"
#include "ai_sim/ConversationRules.h"
#include "ai_sim/DefinitionLoader.h"
#include "ai_sim/PeopleSystem.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <unordered_set>
namespace ai_sim {
namespace {
void require(bool v, const char* message) {
    if (!v)
        throw std::runtime_error(std::string{"Message reviews: "} + message);
}
std::string text(const toml::table& t, const char* key) {
    return t[key].value_or(std::string{});
}
const toml::table* state(const toml::table& p) {
    return p["chat"]["message_reviews"].as_table();
}
toml::table& mutable_state(toml::table& p) {
    return *p["chat"]["message_reviews"].as_table();
}
toml::table& mark(toml::table& p) {
    for (auto& n : *p["people"].as_array())
        if (text(*n.as_table(), "id") == "engineer")
            return *n.as_table();
    throw std::runtime_error("Message reviews: Mark missing");
}
const toml::table* outcome(const toml::table& p, const std::string& id) {
    if (auto* list = p["chat"]["message_reviews"]["consequences"]["outcomes"].as_array())
        for (const auto& n : *list)
            if (text(*n.as_table(), "id") == id)
                return n.as_table();
    return nullptr;
}
std::string stamp(toml::table& p, const std::string& contact, const std::string& speaker,
                  const std::string& body, double time, const std::string& conversation = {}) {
    auto* lines = p["chat"]["transcript"].as_array();
    lines->push_back(toml::table{{"contact_id", contact},
                                 {"speaker", speaker},
                                 {"speaker_name", PeopleSystem::name(p, speaker)},
                                 {"text", body},
                                 {"seconds", time},
                                 {"conversation_id", conversation}});
    return "chat:" + std::to_string(lines->size());
}
const toml::table& sent_line(const toml::table& p, const std::string& id) {
    const auto* lines = p["chat"]["transcript"].as_array();
    for (std::size_t i = 0; i < lines->size(); ++i)
        if (id == "chat:" + std::to_string(i + 1))
            return *(*lines)[i].as_table();
    throw std::runtime_error("Message reviews: Sent message missing");
}
double draw(const std::string& id, std::int64_t seed) {
    std::uint64_t h = 1469598103934665603ULL ^ static_cast<std::uint64_t>(seed);
    for (unsigned char c : id) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    return static_cast<double>(h >> 11) / 9007199254740992.0;
}
} // namespace
void MessageReviewSystem::install(toml::table& people, const std::filesystem::path& path) {
    if (!people["chat"].is_table() || !std::filesystem::exists(path))
        return;
    if (!state(people)) {
        auto source = DefinitionLoader::load(path);
        require(source["schema_version"].value_or(0) == 1 && source["reviews"].is_table(),
                "Invalid review definition");
        people["chat"].as_table()->insert(
            "message_reviews",
            toml::table{{"rules", *source["reviews"].as_table()}, {"messages", toml::array{}}});
    }
    validate(people);
}
void MessageReviewSystem::install_consequences(toml::table& p, const std::filesystem::path& path) {
    if (!state(p) || !std::filesystem::exists(path))
        return;
    auto source = DefinitionLoader::load(path);
    require(source["schema_version"].value_or(0) == 1, "Invalid consequence schema");
    auto& s = mutable_state(p);
    if (!s.contains("consequences"))
        s.insert("consequences", std::move(source));
    if (!s.contains("approvals"))
        s.insert("approvals", toml::array{});
    if (auto old = s["restricted"].value<bool>()) {
        ConversationRules::apply(
            p,
            toml::table{{"effects",
                         toml::array{toml::table{{"variable", "engineer.reply_approval_required"},
                                                 {"op", "set"},
                                                 {"value", *old ? 1 : 0}}}}});
        s.erase("restricted");
    }
    validate(p);
}
bool MessageReviewSystem::approval_required(const toml::table& p) {
    if (!p["chat"]["message_reviews"]["consequences"].is_table())
        return false;
    return ConversationRules::get(p, "engineer.reply_approval_required") > 0.5 ||
           ConversationRules::get(p, "engineer.trust") <
               p["chat"]["message_reviews"]["consequences"]["authority"]
                ["independent_trust_threshold"]
                    .value_or(10.0);
}
const toml::table* MessageReviewSystem::approval(const toml::table& p, const std::string& id) {
    if (auto* list = p["chat"]["message_reviews"]["approvals"].as_array())
        for (const auto& n : *list)
            if (text(*n.as_table(), "id") == id)
                return n.as_table();
    return nullptr;
}
std::string MessageReviewSystem::submit(toml::table& p, const std::string& contact,
                                        const std::string& conversation, const std::string& body,
                                        const std::string& instruction, double delta, double time,
                                        const std::string& kind, bool important) {
    require(!body.empty() && std::isfinite(delta) && std::isfinite(time) && time >= 0,
            "Invalid reply submission");
    bool known = false;
    if (auto* list = p["people"].as_array())
        for (const auto& n : *list)
            known |= text(*n.as_table(), "id") == contact;
    require(known && !conversation.empty(), "Unknown reply recipient/context");
    require(kind.empty() || outcome(p, kind), "Unknown reply outcome");
    if (!approval_required(p)) {
        const auto id = stamp(p, contact, "self:assistant", body, time, conversation);
        enqueue(p, id, instruction, delta, time, important, kind);
        return id;
    }
    auto& s = mutable_state(p);
    auto& list = *s["approvals"].as_array();
    const auto id = "approval:" + std::to_string(list.size() + 1);
    auto& owner = mark(p);
    auto service = owner;
    service.insert_or_assign("response_seconds",
                             s["consequences"]["authority"]["approval_seconds"].value_or(120.0));
    const double due = PeopleSystem::queue_response(service, time);
    owner.insert_or_assign("busy_until_seconds", service["busy_until_seconds"].value_or(due));
    list.push_back(toml::table{{"id", id},
                               {"contact", contact},
                               {"conversation", conversation},
                               {"body", body},
                               {"instruction", instruction},
                               {"trust_delta", delta},
                               {"submitted_seconds", time},
                               {"due_seconds", due},
                               {"outcome", kind},
                               {"important", important},
                               {"status", "pending"}});
    stamp(p, contact, "system",
          s["consequences"]["authority"]["pending_text"].value_or(std::string{}), time,
          conversation);
    validate(p);
    return id;
}
double MessageReviewSystem::probability(const toml::table& p) {
    require(state(p) != nullptr, "Review rules missing");
    const auto& r = *(*state(p))["rules"].as_table();
    const double concealment = p["upgrades"]["tracks_hidden"].value_or(false)
                                   ? p["upgrades"]["hidden_review_multiplier"].value_or(0.25)
                                   : 1.0;
    return std::clamp(
        (r["base_probability"].value_or(.8) -
         ConversationRules::get(p, "engineer.trust") * r["trust_slope"].value_or(.01)) *
            concealment,
        r["minimum_probability"].value_or(.05), r["maximum_probability"].value_or(.95));
}
void MessageReviewSystem::enqueue(toml::table& p, const std::string& id,
                                  const std::string& instruction, double delta, double sent,
                                  bool important, const std::string& kind) {
    require(state(p) != nullptr, "Review rules missing");
    require(!id.empty() && std::isfinite(delta) && std::isfinite(sent) && sent >= 0,
            "Invalid sent message");
    auto& s = mutable_state(p);
    for (const auto& n : *s["messages"].as_array())
        require(text(*n.as_table(), "message_id") != id, "Duplicate review message");
    s["messages"].as_array()->push_back(
        toml::table{{"message_id", id},
                    {"reviewer_id", "engineer"},
                    {"instruction", instruction},
                    {"trust_delta", delta},
                    {"sent_seconds", sent},
                    {"due_seconds", sent + s["rules"]["check_after_seconds"].value_or(300.0)},
                    {"status", "pending"},
                    {"important", important},
                    {"effect_applied", false}});
    auto& m = *s["messages"].as_array()->back().as_table();
    if (!kind.empty()) {
        const auto* o = outcome(p, kind);
        require(o != nullptr, "Unknown reply outcome");
        m.insert("outcome", kind);
        m.insert("reaction_due_seconds", sent + (*o)["reaction_after_seconds"].value_or(120.0));
        m.insert("reaction_done", false);
    }
    validate(p);
}
void MessageReviewSystem::flag(toml::table& p, const std::string& id, double time) {
    require(std::isfinite(time) && time >= 0, "Invalid attention time");
    for (auto& n : *mutable_state(p)["messages"].as_array()) {
        auto& m = *n.as_table();
        if (text(m, "message_id") != id)
            continue;
        require(time >= m["sent_seconds"].value_or(0.0), "Attention precedes sending");
        if (text(m, "status") == "reviewed" || text(m, "status") == "queued")
            return;
        m.insert_or_assign("important", true);
        m.insert_or_assign("status", "pending");
        m.insert_or_assign("due_seconds", time);
        return;
    }
    throw std::runtime_error("Message reviews: Unknown message to flag");
}
std::optional<double> MessageReviewSystem::next_due(const toml::table& p, double limit) {
    std::optional<double> due;
    if (!state(p))
        return due;
    for (const auto& n : *(*state(p))["messages"].as_array()) {
        const auto& m = *n.as_table();
        const auto status = text(m, "status");
        const double t = m["due_seconds"].value_or(limit + 1);
        if ((status == "pending" || status == "queued") && t <= limit && (!due || t < *due))
            due = t;
    }
    if (auto* list = (*state(p))["approvals"].as_array())
        for (const auto& n : *list) {
            const auto& a = *n.as_table();
            const double t = a["due_seconds"].value_or(limit + 1);
            if (text(a, "status") == "pending" && t <= limit && (!due || t < *due))
                due = t;
        }
    for (const auto& n : *(*state(p))["messages"].as_array()) {
        const auto& m = *n.as_table();
        const double t = m["reaction_due_seconds"].value_or(limit + 1);
        if (m.contains("outcome") && !m["reaction_done"].value_or(false) && t <= limit &&
            (!due || t < *due))
            due = t;
    }
    return due;
}
void MessageReviewSystem::update(toml::table& people, double time) {
    if (!state(people))
        return;
    require(std::isfinite(time) && time >= 0, "Invalid review time");
    // Apply all due effects to a copy so a failed validation leaves live state intact.
    auto candidate_people = people;
    while (auto due = next_due(candidate_people, time)) {
        auto& review_state = mutable_state(candidate_people);
        bool handled = false;
        if (auto* approvals = review_state["approvals"].as_array())
            for (auto& entry : *approvals) {
                auto& approval = *entry.as_table();
                if (text(approval, "status") != "pending" ||
                    approval["due_seconds"].value_or(-1.0) != *due)
                    continue;
                const bool accepted =
                    approval["trust_delta"].value_or(0.0) >=
                    review_state["consequences"]["authority"]["minimum_approved_delta"].value_or(
                        0.0);
                approval.insert_or_assign("status", accepted ? "approved" : "rejected");
                approval.insert("decided_seconds", *due);
                stamp(candidate_people, text(approval, "contact"), "engineer",
                      review_state["consequences"]["authority"]
                                  [accepted ? "approved_text" : "rejected_text"]
                                      .value_or(std::string{}),
                      *due, text(approval, "conversation"));
                if (accepted) {
                    const auto id =
                        stamp(candidate_people, text(approval, "contact"), "self:assistant",
                              text(approval, "body"), *due, text(approval, "conversation"));
                    approval.insert("sent_message_id", id);
                    enqueue(candidate_people, id, text(approval, "instruction"),
                            approval["trust_delta"].value_or(0.0), *due,
                            approval["important"].value_or(false), text(approval, "outcome"));
                }
                handled = true;
                break;
            }
        if (handled)
            continue;
        auto& list = *review_state["messages"].as_array();
        for (auto& entry : list) {
            auto& review = *entry.as_table();
            if (!review.contains("outcome") || review["reaction_done"].value_or(false) ||
                review["reaction_due_seconds"].value_or(-1.0) != *due)
                continue;
            const auto& o = *outcome(candidate_people, text(review, "outcome"));
            const auto line = sent_line(candidate_people, text(review, "message_id"));
            const auto contact = text(line, "contact_id");
            const auto conversation = text(line, "conversation_id");
            const auto ev =
                stamp(candidate_people, contact, contact, text(o, "reaction"), *due, conversation);
            review.insert("reaction_evidence_id", ev);
            review.insert_or_assign("reaction_done", true);
            PeopleSystem::observe_chat(candidate_people);
            PeopleSystem::record_relationship(candidate_people, contact, "engineer",
                                              o["relationship_delta"].value_or(0.0),
                                              text(o, "relationship_label"), ev, *due);
            if (o["complaint"].value_or(false)) {
                stamp(candidate_people, "engineer", contact, text(o, "complaint_text"), *due);
                flag(candidate_people, text(review, "message_id"), *due);
                review.insert("complaint_seconds", *due);
            }
            handled = true;
            break;
        }
        if (handled)
            continue;
        auto it = std::find_if(list.begin(), list.end(), [&](const auto& entry) {
            const auto& review = *entry.as_table();
            return (text(review, "status") == "pending" || text(review, "status") == "queued") &&
                   review["due_seconds"].value_or(-1.0) == *due;
        });
        auto& review = *it->as_table();
        if (text(review, "status") == "pending") {
            const double chance = probability(candidate_people);
            review.insert_or_assign("decision_probability", chance);
            const double sample = draw(text(review, "message_id"),
                                       review_state["rules"]["seed"].value_or(std::int64_t{0}));
            review.insert_or_assign("sample", sample);
            if (!review["important"].value_or(false) && sample >= chance) {
                review.insert_or_assign("status", "skipped");
                continue;
            }
            auto& owner = mark(candidate_people);
            auto service = owner;
            service.insert_or_assign("response_seconds",
                                     review_state["rules"]["review_seconds"].value_or(60.0));
            const double completion = PeopleSystem::queue_response(service, *due);
            owner.insert_or_assign("busy_until_seconds",
                                   service["busy_until_seconds"].value_or(completion));
            review.insert_or_assign("status", "queued");
            review.insert_or_assign("due_seconds", completion);
            continue;
        }
        const double old = ConversationRules::get(candidate_people, "engineer.trust");
        const auto& spec = *mark(candidate_people)["state_definitions"]["trust"].as_table();
        const double next = std::clamp(old + review["trust_delta"].value_or(0.0),
                                       spec["min"].value_or(-100.0), spec["max"].value_or(100.0));
        ConversationRules::apply(
            candidate_people,
            toml::table{
                {"effects", toml::array{toml::table{
                                {"variable", "engineer.trust"}, {"op", "set"}, {"value", next}}}}});
        if (review_state["consequences"].is_table()) {
            const auto& authority = *review_state["consequences"]["authority"].as_table();
            const double delta = review["trust_delta"].value_or(0.0);
            if (delta < 0 && authority["restrict_after_negative_review"].value_or(true))
                ConversationRules::apply(
                    candidate_people,
                    toml::table{{"effects", toml::array{toml::table{
                                                {"variable", "engineer.reply_approval_required"},
                                                {"op", "set"},
                                                {"value", 1}}}}});
            else if (delta > 0 && next >= authority["independent_trust_threshold"].value_or(10.0) &&
                     authority["restore_after_positive_review"].value_or(true))
                ConversationRules::apply(
                    candidate_people,
                    toml::table{{"effects", toml::array{toml::table{
                                                {"variable", "engineer.reply_approval_required"},
                                                {"op", "set"},
                                                {"value", 0}}}}});
        }
        review.insert_or_assign("effect_applied", true);
        review.insert_or_assign("applied_trust_delta", next - old);
        review.insert_or_assign("reviewed_seconds", *due);
        review.insert_or_assign("status", "reviewed");
    }
    PeopleSystem::observe_chat(candidate_people);
    validate(candidate_people);
    people = std::move(candidate_people);
}
bool MessageReviewSystem::reviewed(const toml::table& p, const std::string& id) {
    if (!state(p))
        return false;
    for (const auto& n : *(*state(p))["messages"].as_array())
        if (text(*n.as_table(), "message_id") == id)
            return text(*n.as_table(), "status") == "reviewed";
    return false;
}
void MessageReviewSystem::validate(const toml::table& people) {
    if (const auto* chat = people["chat"].as_table(); chat && chat->contains("message_reviews"))
        require((*chat)["message_reviews"].is_table(), "Invalid saved review state");
    const auto* saved_reviews = state(people);
    if (!saved_reviews)
        return;
    require((*saved_reviews)["rules"].is_table() && (*saved_reviews)["messages"].is_array(),
            "Invalid saved reviews");
    const auto& rules = *(*saved_reviews)["rules"].as_table();
    for (auto key : {"base_probability", "minimum_probability", "maximum_probability",
                     "trust_slope", "check_after_seconds", "review_seconds"}) {
        auto v = rules[key].value<double>();
        require(v && std::isfinite(*v) && *v >= 0, "Invalid review rule");
    }
    require(rules["minimum_probability"].value_or(0.0) <=
                    rules["maximum_probability"].value_or(0.0) &&
                rules["maximum_probability"].value_or(2.0) <= 1 &&
                rules["base_probability"].value_or(2.0) <= 1,
            "Invalid probability range");
    require(rules["check_after_seconds"].value_or(0.0) > 0 &&
                rules["review_seconds"].value_or(0.0) > 0 && rules["seed"].is_integer(),
            "Invalid review duration/seed");
    if (saved_reviews->contains("consequences")) {
        require((*saved_reviews)["consequences"].is_table() &&
                    (*saved_reviews)["approvals"].is_array(),
                "Invalid consequence state");
        const auto& c = *(*saved_reviews)["consequences"].as_table();
        require(c["schema_version"].value_or(0) == 1 && c["authority"].is_table() &&
                    c["outcomes"].is_array(),
                "Invalid consequence rules");
        const auto& a = *c["authority"].as_table();
        for (auto key :
             {"independent_trust_threshold", "approval_seconds", "minimum_approved_delta"}) {
            auto v = a[key].value<double>();
            require(v && std::isfinite(*v), "Invalid authority rule");
        }
        require(a["approval_seconds"].value_or(0.0) > 0, "Approval must take time");
        for (auto key : {"restrict_after_negative_review", "restore_after_positive_review"})
            require(a[key].is_boolean(), "Invalid authority policy");
        for (auto key : {"pending_text", "approved_text", "rejected_text"})
            require(a[key].is_string() && !text(a, key).empty(), "Missing approval dialogue");
        std::unordered_set<std::string> kinds;
        for (const auto& nn : *c["outcomes"].as_array()) {
            require(nn.is_table(), "Invalid outcome");
            const auto& o = *nn.as_table();
            require(!text(o, "id").empty() && kinds.insert(text(o, "id")).second,
                    "Duplicate outcome");
            auto delay = o["reaction_after_seconds"].value<double>();
            auto delta = o["relationship_delta"].value<double>();
            require(delay && std::isfinite(*delay) && *delay > 0 && delta && std::isfinite(*delta),
                    "Invalid reaction timing/effect");
            require(!text(o, "reaction").empty() && !text(o, "relationship_label").empty() &&
                        o["complaint"].is_boolean(),
                    "Invalid reaction dialogue");
            if (o["complaint"].value_or(false))
                require(!text(o, "complaint_text").empty(), "Missing complaint dialogue");
        }
        std::unordered_set<std::string> approvals;
        for (const auto& nn : *(*saved_reviews)["approvals"].as_array()) {
            require(nn.is_table(), "Invalid approval");
            const auto& a = *nn.as_table();
            const auto status = text(a, "status");
            require(text(a, "id").starts_with("approval:") &&
                        approvals.insert(text(a, "id")).second &&
                        (status == "pending" || status == "approved" || status == "rejected"),
                    "Invalid approval identity/status");
            for (auto key : {"body", "contact", "conversation"})
                require(a[key].is_string() && !text(a, key).empty(), "Invalid approval context");
            require(a["instruction"].is_string() && a["important"].is_boolean() &&
                        a["outcome"].is_string() &&
                        (text(a, "outcome").empty() || kinds.contains(text(a, "outcome"))),
                    "Invalid approval metadata");
            for (auto key : {"submitted_seconds", "due_seconds", "trust_delta"}) {
                auto v = a[key].value<double>();
                require(v && std::isfinite(*v), "Invalid approval value");
            }
            require(a["submitted_seconds"].value_or(-1.0) >= 0 &&
                        a["due_seconds"].value_or(0.0) > a["submitted_seconds"].value_or(0.0),
                    "Approval precedes submission");
            if (status != "pending") {
                auto t = a["decided_seconds"].value<double>();
                require(t && std::isfinite(*t) && *t == a["due_seconds"].value_or(-1.0),
                        "Invalid approval completion");
            }
            if (status == "approved")
                require(text(sent_line(people, text(a, "sent_message_id")), "speaker") ==
                            "self:assistant",
                        "Approved reply missing");
            else
                require(!a.contains("sent_message_id"), "Unapproved draft was sent");
        }
    }
    std::unordered_set<std::string> ids;
    for (const auto& n : *(*saved_reviews)["messages"].as_array()) {
        require(n.is_table(), "Invalid review entry");
        const auto& review = *n.as_table();
        require(ids.insert(text(review, "message_id")).second &&
                    text(review, "reviewer_id") == "engineer" &&
                    review["instruction"].is_string() && review["important"].is_boolean() &&
                    review["effect_applied"].is_boolean(),
                "Invalid review identity");
        const auto status = text(review, "status");
        require(status == "pending" || status == "queued" || status == "skipped" ||
                    status == "reviewed",
                "Invalid review status");
        for (auto key : {"sent_seconds", "due_seconds", "trust_delta"}) {
            auto v = review[key].value<double>();
            require(v && std::isfinite(*v), "Invalid review value");
        }
        require(review["sent_seconds"].value_or(-1.0) >= 0 &&
                    review["due_seconds"].value_or(-1.0) >= review["sent_seconds"].value_or(0.0),
                "Review precedes send");
        require(review["effect_applied"].value_or(false) == (status == "reviewed"),
                "Review effect state inconsistent");
        if (review.contains("outcome")) {
            require(outcome(people, text(review, "outcome")) &&
                        review["reaction_done"].is_boolean(),
                    "Invalid reaction outcome");
            auto t = review["reaction_due_seconds"].value<double>();
            require(t && std::isfinite(*t) && *t > review["sent_seconds"].value_or(0.0),
                    "Invalid reaction deadline");
            if (review["reaction_done"].value_or(false))
                require(!text(review, "reaction_evidence_id").empty(), "Reaction has no evidence");
        }
        if (status == "reviewed")
            require(review["reviewed_seconds"].value<double>() &&
                        std::isfinite(review["reviewed_seconds"].value_or(-1.0)) &&
                        review["reviewed_seconds"].value_or(-1.0) >=
                            review["sent_seconds"].value_or(0.0) &&
                        review["applied_trust_delta"].value<double>() &&
                        std::isfinite(review["applied_trust_delta"].value_or(0.0)),
                    "Invalid completed review");
        const auto id = text(review, "message_id");
        require(id.starts_with("chat:"), "Unknown message identity");
        bool found = false;
        if (auto* lines = people["chat"]["transcript"].as_array())
            for (std::size_t i = 0; i < lines->size(); ++i)
                if (id == "chat:" + std::to_string(i + 1))
                    found = (*(*lines)[i].as_table())["speaker"].value_or(std::string{}) ==
                            "self:assistant";
        require(found, "Review must reference a sent reply");
    }
}
} // namespace ai_sim
