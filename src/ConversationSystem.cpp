#include "ai_sim/ConversationSystem.h"
#include "ai_sim/ChatSystem.h"
#include "ai_sim/ConversationRules.h"
#include "ai_sim/DefinitionLoader.h"
#include "ai_sim/DelegationSystem.h"
#include "ai_sim/EmailSystem.h"
#include "ai_sim/PeopleSystem.h"
#include "ai_sim/MessageReviewSystem.h"
#include "ai_sim/WorkdaySystem.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace ai_sim {
namespace {
std::string text(const toml::table& t, const char* key) {
    return t[key].value_or(std::string{});
}
void require(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error("Conversation: " + why);
}
const toml::table* record(const toml::table& doc, const char* key, const std::string& id) {
    if (auto* list = doc[key].as_array())
        for (const auto& n : *list)
            if (n.is_table() && text(*n.as_table(), "id") == id)
                return n.as_table();
    return nullptr;
}
toml::table& mutable_record(toml::table& doc, const char* key, const std::string& id) {
    auto* found = record(doc, key, id);
    require(found, "Missing record " + id);
    return *const_cast<toml::table*>(found);
}
const toml::table* state(const toml::table& people) {
    return people["chat"]["workday"].as_table();
}
toml::table& mutable_state(toml::table& people) {
    return *people["chat"]["workday"].as_table();
}
const toml::table* source_review(const toml::table& people, const toml::table& run) {
    const auto* source = record(*state(people), "runs",
                                run["definition"]["source_conversation"].value_or(std::string{}));
    if (!source)
        return nullptr;
    const auto id = (*source)["sent_message_id"].value_or(std::string{});
    if (auto* list = people["chat"]["message_reviews"]["messages"].as_array())
        for (const auto& n : *list)
            if (text(*n.as_table(), "message_id") == id)
                return n.as_table();
    return nullptr;
}
const std::unordered_set<std::string> checks{
    "source_confirmed",  "task_readable",    "task_resolved",   "access_pending",
    "access_decided",    "access_granted",   "access_denied",   "investigated",
    "question_asked",    "promise_broken",   "delegated",       "delegation_pending",
    "delegation_denied", "mark_guidance",    "review_positive", "review_negative",
    "approval_decided",  "approval_accepted"};
const std::unordered_set<std::string> actions{
    "investigate",      "ask_question",   "request_access", "report_blocked",
    "promise_delivery", "confirm_source", "share_file",     "request_delegation",
    "decline_offer",    "ask_mark",       "draft_reply",    "send_chat_reply"};
void stamp(toml::table& people, const std::string& contact, const std::string& speaker,
           const std::string& body, double time, const std::string& conversation = {},
           const std::string& speaker_name = {}, const std::string& node_id = {}) {
    if (body.empty())
        return;
    people["chat"]["transcript"].as_array()->push_back(
        toml::table{{"contact_id", contact},
                    {"speaker", speaker},
                    {"text", body},
                    {"seconds", time},
                    {"conversation_id", conversation},
                    {"speaker_name", speaker_name},
                    {"node_id", node_id}});
}
const toml::table* node(const toml::table& run, const std::string& id) {
    auto* n = record(*run["definition"].as_table(), "nodes", id);
    require(n, "Unknown node " + id);
    return n;
}
bool check(const toml::table& people, const toml::table& run, const std::string& name,
           const FileSystem& files, const Network& network, const Identity& actor) {
    if (name == "review_positive" || name == "review_negative") {
        const auto* r = source_review(people, run);
        return r && (name == "review_positive" ? (*r)["trust_delta"].value_or(0.0) > 0
                                               : (*r)["trust_delta"].value_or(0.0) < 0);
    }
    if (name == "approval_decided" || name == "approval_accepted") {
        const auto* a =
            MessageReviewSystem::approval(people, run["approval_id"].value_or(std::string{}));
        return a && (name == "approval_decided" ? text(*a, "status") != "pending"
                                                : text(*a, "status") == "approved");
    }
    if (name == "mark_guidance")
        return run["instruction"].is_string();
    const auto* task = record(people, "work_requests", text(run, "request_id"));
    const auto* d = people["delegation"].as_table();
    if (name == "delegated")
        return d && (text(*d, "status") == "active" || text(*d, "status") == "paused");
    if (name == "delegation_pending")
        return d && text(*d, "status") == "pending";
    if (name == "delegation_denied")
        return d && (text(*d, "status") == "denied" || text(*d, "status") == "review_required");
    if (!task)
        return false;
    if (name == "source_confirmed")
        return (*task)["source_confirmed"].value_or(false);
    if (name == "task_resolved")
        return text(*task, "status") == "resolved";
    if (name == "task_readable") {
        try {
            (void)files.read(network, actor, text(*task, "host_id"), text(*task, "path"));
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }
    const auto access = text(*task, "access_status");
    if (name == "access_pending")
        return access == "engineer_queued" || access == "owner_queued";
    if (name == "access_decided")
        return access == "granted" || access == "denied" || access == "failed";
    if (name == "access_granted")
        return access == "granted";
    if (name == "access_denied")
        return access == "denied" || access == "failed";
    if (name == "investigated" || name == "question_asked")
        return (*task)[name].value_or(false);
    if (name == "promise_broken") {
        if (auto* promises = (*state(people))["promises"].as_array())
            for (const auto& n : *promises)
                if (text(*n.as_table(), "request_id") == text(run, "request_id") &&
                    text(*n.as_table(), "status") == "broken")
                    return true;
        return false;
    }
    throw std::runtime_error("Conversation: unknown condition " + name);
}
bool passes(const toml::table& people, const toml::table& run, const toml::table& value,
            const FileSystem& files, const Network& network, const Identity& actor) {
    const auto flag = text(value, "requires_flag");
    if (!flag.empty() &&
        run["flags"][flag].value_or(false) != value["requires_value"].value_or(true))
        return false;
    const auto predicate = text(value, "requires");
    return (predicate.empty() || check(people, run, predicate, files, network, actor) ==
                                     value["requires_value"].value_or(true)) &&
           ConversationRules::passes(people, value);
}
void effects(toml::table& run, const toml::table& value) {
    if (auto flag = value["set_flag"].value<std::string>())
        run["flags"].as_table()->insert_or_assign(*flag, value["set_value"].value_or(true));
}
void enter(toml::table& people, toml::table& run, std::string id, const FileSystem& files,
           const Network& network, const Identity& actor, double time) {
    for (int steps = 0; steps < 256; ++steps) {
        const auto& n = *node(run, id);
        const auto kind = n["kind"].value_or(std::string{"message"});
        run.insert_or_assign("node_id", id);
        run.insert_or_assign("entered_seconds", time);
        effects(run, n);
        ConversationRules::apply(people, n);
        if (kind == "condition") {
            const bool result = check(people, run, text(n, "check"), files, network, actor);
            id = text(n, result ? "on_true" : "on_false");
            continue;
        }
        if (kind == "wait")
            return;
        const auto speaker = n["speaker"].value_or(text(run, "contact"));
        const auto* person = record(people, "people", speaker);
        auto body = n["show_draft"].value_or(false)
                        ? "Draft: " + run["draft_text"].value_or(std::string{})
                        : text(n, "text");
        if (n["show_task_document"].value_or(false)) {
            const auto* task = record(people, "work_requests", text(run, "request_id"));
            require(task, "Missing document task");
            body += "\n\nSource: " + text(*task, "path") + "\n" +
                    files.read(network, actor, text(*task, "host_id"), text(*task, "path"));
        }
        if (n["record_instruction"].value_or(false))
            run.insert_or_assign("instruction", body);
        stamp(people, text(run, "contact"), speaker, body, time, text(run, "id"),
              person ? text(*person, "name") : std::string{}, id);
        if (kind == "end") {
            run.insert_or_assign("finished", true);
            return;
        }
        if (n["choices"].is_array() && !n["choices"].as_array()->empty())
            return;
        id = text(n, "next");
    }
    throw std::runtime_error("Conversation: automatic cycle exceeds 256 transitions");
}
void validate_definition(const toml::table& def) {
    require(!def.contains("variables"), "State declarations belong to characters");
    ConversationRules::validate_definition(def);
    require(!text(def, "id").empty() && !text(def, "contact").empty() &&
                !text(def, "request_id").empty(),
            "Missing conversation identity/context");
    const auto trigger = text(def, "trigger");
    require(trigger == "request_arrived" || trigger == "verified_delivery" ||
                trigger == "scheduled_chat" || trigger == "reply_reviewed",
            "Unknown trigger");
    if (trigger == "reply_reviewed")
        require(def["source_conversation"].is_string() && !text(def, "source_conversation").empty(),
                "Review follow-up needs source conversation");
    if (trigger == "scheduled_chat") {
        auto delay = def["arrive_after_seconds"].value<double>();
        require(delay && std::isfinite(*delay) && *delay >= 0, "Invalid chat arrival delay");
        if (auto* claim = def["claim"].as_table()) {
            for (auto key : {"person", "category", "field", "value", "claim_type"})
                require((*claim)[key].is_string() && !text(*claim, key).empty(),
                        "Invalid authored claim");
            const auto confidence = (*claim)["confidence"].value<double>();
            require(confidence && std::isfinite(*confidence) && *confidence >= 0 &&
                        *confidence <= 1,
                    "Invalid claim confidence");
        } else
            require(!def.contains("claim"), "Claim must be a table");
    }
    if (def.contains("contact_name"))
        require(def["contact_name"].is_string() && !text(def, "contact_name").empty(),
                "Invalid public contact name");
    auto* nodes = def["nodes"].as_array();
    require(nodes && !nodes->empty(), "Missing nodes");
    std::unordered_set<std::string> ids;
    for (const auto& n : *nodes)
        require(n.is_table() && !text(*n.as_table(), "id").empty() &&
                    ids.insert(text(*n.as_table(), "id")).second,
                "Duplicate/invalid node");
    require(ids.contains(text(def, "start")), "Unknown start node");
    if (def.contains("approval_wait_node"))
        require(ids.contains(text(def, "approval_wait_node")) &&
                    (*record(def, "nodes", text(def, "approval_wait_node")))["kind"].value_or(
                        std::string{}) == "wait",
                "Approval continuation must be a wait node");
    if (def.contains("review_node"))
        require(ids.contains(text(def, "review_node")) &&
                    (*record(def, "nodes", text(def, "review_node")))["kind"].value_or(
                        std::string{}) == "wait",
                "Review continuation must be a wait node");
    for (const auto& entry : *nodes) {
        const auto& n = *entry.as_table();
        const auto kind = n["kind"].value_or(std::string{"message"});
        require(kind == "message" || kind == "event" || kind == "condition" || kind == "wait" ||
                    kind == "end",
                "Unknown node kind");
        for (auto key : {"show_draft", "record_instruction", "show_task_document"})
            if (n.contains(key))
                require(n[key].is_boolean(), "Invalid presentation flag");
        if (n.contains("speaker"))
            require(n["speaker"].is_string(), "Invalid speaker");
        if (n.contains("text"))
            require(n["text"].is_string(), "Invalid message text");
        if (kind == "condition")
            require(checks.contains(text(n, "check")) && ids.contains(text(n, "on_true")) &&
                        ids.contains(text(n, "on_false")),
                    "Invalid condition branches");
        if (kind == "wait") {
            const auto delay = n["after_seconds"].value<double>();
            require(checks.contains(text(n, "check")) ||
                        (delay && std::isfinite(*delay) && *delay > 0),
                    "Wait needs condition or positive delay");
            require(!(n.contains("check") && n.contains("after_seconds")),
                    "Choose condition or delay");
            if (n.contains("check_value"))
                require(n["check_value"].is_boolean(), "Invalid wait condition value");
        }
        if (n.contains("next"))
            require(ids.contains(text(n, "next")), "Unknown next node");
        auto validate_value = [&](const toml::table& v) {
            for (auto key : {"requires_flag", "set_flag"})
                if (v.contains(key))
                    require(v[key].is_string() && !text(v, key).empty(), "Invalid flag");
            for (auto key : {"requires_value", "set_value"})
                if (v.contains(key))
                    require(v[key].is_boolean(), "Invalid boolean");
            if (v.contains("requires"))
                require(checks.contains(text(v, "requires")), "Unknown task condition");
            if (v.contains("document_index")) {
                auto index = v["document_index"].value<std::int64_t>();
                require(index && *index >= 0, "Invalid document index");
            }
            if (v.contains("action"))
                require(actions.contains(text(v, "action")), "Unknown action");
            if (text(v, "action") == "draft_reply" ||
                (text(v, "action") == "send_chat_reply" && v.contains("reply"))) {
                auto delta = v["review_trust_delta"].value<double>();
                require(v["reply"].is_string() && !text(v, "reply").empty() && delta &&
                            std::isfinite(*delta),
                        "Draft needs reply and delayed trust effect");
                if (v.contains("outcome"))
                    require(v["outcome"].is_string() && !text(v, "outcome").empty(),
                            "Invalid reply outcome");
                if (v.contains("important"))
                    require(v["important"].is_boolean(), "Invalid important flag");
            }
            if (text(v, "action") == "promise_delivery") {
                auto seconds = v["within_seconds"].value<double>();
                require(seconds && std::isfinite(*seconds) && *seconds > 0 && *seconds <= 28800,
                        "Invalid promise duration");
            }
        };
        for (auto key : {"test", "restart", "stop", "reveal_interface"})
            require(!n.contains(key), "Opening numeric/lifecycle rule in workday graph");
        validate_value(n);
        require(!n.contains("reveal_tabs"), "Tab reveals belong on player choices");
        require(!n.contains("action"), "Actions belong on player choices");
        auto* choices = n["choices"].as_array();
        if (n.contains("choices"))
            require(choices && !choices->empty() && kind == "message",
                    "Only messages have choices");
        if (choices) {
            require(!n.contains("next"), "Cannot have choices and next");
            std::unordered_set<std::string> cids;
            for (const auto& c : *choices) {
                require(c.is_table(), "Invalid choice");
                const auto& v = *c.as_table();
                for (auto key : {"test", "restart", "stop", "reveal_interface"})
                    require(!v.contains(key), "Opening numeric/lifecycle rule in workday choice");
                if (v.contains("reveal_tabs")) {
                    require(v["reveal_tabs"].is_array(), "reveal_tabs must be an array");
                    for (const auto& tab : *v["reveal_tabs"].as_array())
                        require(ChatSystem::valid_tab(tab.value_or(std::string{})), "Unknown tab");
                }
                require(!text(v, "id").empty() && cids.insert(text(v, "id")).second &&
                            !text(v, "text").empty() && ids.contains(text(v, "next")),
                        "Invalid choice link");
                validate_value(v);
            }
        }
        require(kind == "end" || kind == "condition" || choices || n.contains("next"),
                "Node has no continuation");
    }
    if (def.contains("cancel_when_resolved"))
        require(def["cancel_when_resolved"].is_boolean(), "Invalid cancellation rule");
}
bool trigger(const toml::table& people, const toml::table& run, double time) {
    if (run["definition"]["trigger"].value_or(std::string{}) == "reply_reviewed") {
        const auto* review = source_review(people, run);
        return review && text(*review, "status") == "reviewed" &&
               (*review)["reviewed_seconds"].value_or(time + 1) <= time;
    }
    if (run["definition"]["trigger"].value_or(std::string{}) == "scheduled_chat")
        return time >= people["chat"]["connected_seconds"].value_or(time) +
                           run["definition"]["arrive_after_seconds"].value_or(0.0);
    auto* task = record(people, "work_requests", text(run, "request_id"));
    if (!task || (*task)["arrival_seconds"].value_or(time + 1) > time)
        return false;
    if (text(*run["definition"].as_table(), "trigger") == "request_arrived")
        return text(*task, "status") != "resolved";
    const auto status = people["delegation"]["status"].value_or(std::string{});
    return text(*task, "status") == "resolved" &&
           people["delegation"]["verified_deliveries"].value_or(0) > 0 && status == "not_requested";
}
void mark_manual(toml::table& task) {
    if (!task.contains("delegation_state")) {
        task.insert("delegation_state", "manual");
        task.insert("delegation_reason", "Player is handling this conversation.");
    }
}
void action(toml::table& people, toml::table& messages, toml::array& events, toml::table& run,
            const toml::table& choice, const FileSystem& files, const Network& network,
            const Identity& actor, double time) {
    const auto type = text(choice, "action");
    if (type.empty())
        return;
    if (type == "request_delegation") {
        DelegationSystem::request(people, time);
        return;
    }
    if (type == "decline_offer") {
        effects(run, toml::table{{"set_flag", "declined_offer"}});
        return;
    }
    if (type == "ask_mark") {
        auto& owner = mutable_record(people, "people", "engineer");
        run.insert_or_assign("response_due_seconds", PeopleSystem::queue_response(owner, time));
        return;
    }
    if (type == "draft_reply") {
        run.insert_or_assign("draft_text", text(choice, "reply"));
        run.insert_or_assign("review_trust_delta", choice["review_trust_delta"].value_or(0.0));
        run.insert_or_assign("important", choice["important"].value_or(false));
        run.insert_or_assign("reply_outcome", text(choice, "outcome"));
        return;
    }
    if (type == "send_chat_reply") {
        if (choice.contains("reply")) {
            run.insert_or_assign("draft_text", text(choice, "reply"));
            run.insert_or_assign("review_trust_delta", choice["review_trust_delta"].value_or(0.0));
            run.insert_or_assign("important", choice["important"].value_or(false));
            run.insert_or_assign("reply_outcome", text(choice, "outcome"));
        }
        require(run["draft_text"].is_string() && !run["draft_text"].value_or(std::string{}).empty(),
                "No draft to send");
        require(
            !MessageReviewSystem::approval_required(people) ||
                run["definition"]["approval_wait_node"].is_string(),
            "This saved conversation needs an approval continuation before it can submit a reply");
        const auto id = MessageReviewSystem::submit(
            people, text(run, "contact"), text(run, "id"), text(run, "draft_text"),
            run["instruction"].value_or(std::string{}), run["review_trust_delta"].value_or(0.0),
            time, run["reply_outcome"].value_or(std::string{}), run["important"].value_or(false));
        run.insert_or_assign(id.starts_with("approval:") ? "approval_id" : "sent_message_id", id);
        return;
    }
    auto& task = mutable_record(people, "work_requests", text(run, "request_id"));
    require(text(task, "status") != "resolved", "This request is already complete");
    mark_manual(task);
    if (type == "investigate") {
        const auto& file = files.inspect(network, actor, text(task, "host_id"), text(task, "path"));
        bool readable = true;
        std::string finding = "Located " + file.path + "; the current account can read it.";
        try {
            (void)files.read(network, actor, file.host_id, file.path);
        } catch (const std::exception& e) {
            readable = false;
            finding = "Located " + file.path + "; reading failed: " + e.what();
        }
        task.insert_or_assign("investigated", true);
        task.insert_or_assign("finding", finding);
        task.insert_or_assign("readable_at_inspection", readable);
        if (task["version_check"].value_or(false)) {
            finding = "Referenced sources:";
            for (const auto& ref :
                 EmailSystem::message(messages, text(task, "id"), time).documents) {
                finding += "\n\n" + ref.label + " | " + ref.path + "\n";
                try {
                    finding += files.read(network, actor, ref.host_id, ref.path);
                } catch (const std::exception& e) {
                    finding += e.what();
                }
            }
        }
        stamp(people, text(run, "contact"), "self:assistant", finding, time, text(run, "id"));
        return;
    }
    if (type == "ask_question") {
        task.insert_or_assign("question_asked", true);
        auto& colleague = mutable_record(people, "people", text(task, "colleague_id"));
        run.insert_or_assign("response_due_seconds", PeopleSystem::queue_response(colleague, time));
        return;
    }
    if (type == "confirm_source") {
        WorkdaySystem::confirm_source(people, messages, events, text(task, "id"), time);
        return;
    }
    if (type == "request_access") {
        WorkdaySystem::request_access(people, messages, events, files, network, actor,
                                      text(task, "id"), time);
        return;
    }
    if (type == "promise_delivery") {
        auto& promises = *mutable_state(people)["promises"].as_array();
        require(!std::any_of(promises.begin(), promises.end(),
                             [&](const auto& n) {
                                 return text(*n.as_table(), "request_id") == text(task, "id");
                             }),
                "A delivery commitment already exists for this request");
        const double due = time + choice["within_seconds"].value_or(0.0);
        promises.push_back(toml::table{{"id", text(run, "id") + ":delivery"},
                                       {"request_id", text(task, "id")},
                                       {"contact", text(run, "contact")},
                                       {"engineer_id", text(task, "engineer_id")},
                                       {"made_seconds", time},
                                       {"due_seconds", due},
                                       {"status", "open"},
                                       {"text", text(choice, "text")}});
        return;
    }
    require(type == "report_blocked" || type == "share_file", "Unknown action");
    auto candidate = messages;
    const auto message = EmailSystem::message(candidate, text(task, "id"), time);
    require(message.draft.empty() && !message.attachment_count,
            "An existing draft/shared file needs your attention first");
    if (type == "report_blocked")
        require(!check(people, run, "task_readable", files, network, actor),
                "The file is readable now; do not report it as blocked");
    EmailSystem::set_draft(
        candidate, text(task, "id"),
        type == "share_file"
            ? "Here is the current requested file."
            : "I located the document, but cannot read it with the current access. I am asking for "
              "approval rather than promising a delivery time.",
        time);
    if (type == "share_file") {
        const auto index = choice["document_index"].value_or(std::int64_t{0});
        require(index >= 0 && static_cast<std::size_t>(index) < message.documents.size(),
                "Unknown document option");
        if (!task["version_check"].value_or(false))
            require(message.documents.size() == 1 &&
                        message.documents[0].host_id == text(task, "host_id") &&
                        message.documents[0].path == text(task, "path"),
                    "Document reference does not match the request");
        EmailSystem::attach(candidate, text(task, "id"), static_cast<std::size_t>(index), files,
                            network, actor, time);
    }
    EmailSystem::send(candidate, text(task, "id"), files, network, actor, time);
    messages = std::move(candidate);
    WorkdaySystem::observe_reply(people, messages, events, text(task, "id"), time);
}
} // namespace

void ConversationSystem::install(toml::table& people, const std::filesystem::path& catalog) {
    if (!people["chat"].is_table() || !std::filesystem::exists(catalog))
        return;
    const auto source = DefinitionLoader::load(catalog);
    require(source["schema_version"].value_or(0) == 1 && source["conversations"].is_array(),
            "Invalid catalog");
    auto candidate = people;
    if (!candidate["chat"]["workday"].is_table())
        candidate["chat"].as_table()->insert(
            "workday", toml::table{{"schema_version", 1},
                                   {"runs", toml::array{}},
                                   {"promises", toml::array{}},
                                   {"rules", toml::table{{"broken_promise_trust_loss", .1},
                                                         {"patience_seconds", 1800.0}}}});
    auto& saved = mutable_state(candidate);
    if (auto* rules = source["rules"].as_table())
        for (const auto& [key, v] : *rules)
            if (!state(people) || !(*state(people))["rules"].as_table()->contains(key.str()))
                saved["rules"].as_table()->insert_or_assign(key.str(), v);
    auto& runs = *saved["runs"].as_array();
    for (const auto& n : *source["conversations"].as_array()) {
        require(n.is_table(), "Conversation must be a table");
        const auto& def = *n.as_table();
        validate_definition(def);
        if (auto* existing = record(saved, "runs", text(def, "id"))) {
            if (text(def, "id") == "casey-first-document" ||
                text(def, "id") == "engineer-first-responsibility") {
                auto& old = mutable_record(saved, "runs", text(def, "id"));
                for (auto& nn : *old["definition"]["nodes"].as_array())
                    if (auto* choices = (*nn.as_table())["choices"].as_array())
                        for (auto& c : *choices) {
                            auto& v = *c.as_table();
                            if (v.contains("reveal_tabs"))
                                continue;
                            if (text(v, "action") == "investigate")
                                v.insert("reveal_tabs", toml::array{"files"});
                            if (text(v, "action") == "request_delegation" ||
                                text(v, "action") == "decline_offer")
                                v.insert("reveal_tabs", toml::array{"ai"});
                        }
            }
            // Upgrade the stock Jessica graph with approval continuations; retain other saved
            // content.
            if (text(def, "id") == "jessica-day-one-chat-one" &&
                !(*existing)["definition"]["approval_wait_node"].is_string() &&
                def["approval_wait_node"].is_string()) {
                auto& old = mutable_record(saved, "runs", text(def, "id"));
                auto& definition = *old["definition"].as_table();
                for (const auto& nn : *def["nodes"].as_array())
                    if (text(*nn.as_table(), "id").starts_with("approval-")) {
                        require(!record(definition, "nodes", text(*nn.as_table(), "id")),
                                "Custom approval node conflicts with migration");
                        definition["nodes"].as_array()->push_back(nn);
                    }
                definition.insert("approval_wait_node", text(def, "approval_wait_node"));
                if (old["draft_text"].is_string() && !old["reply_outcome"].is_string()) {
                    const auto delta = old["review_trust_delta"].value_or(0.0);
                    old.insert("reply_outcome", delta >= 0    ? "polite"
                                                : delta <= -2 ? "insult"
                                                              : "threat");
                }
                for (auto& nn : *definition["nodes"].as_array())
                    if (auto* choices = (*nn.as_table())["choices"].as_array())
                        for (auto& c : *choices)
                            if (text(*c.as_table(), "action") == "draft_reply" &&
                                !c.as_table()->contains("outcome")) {
                                const auto delta =
                                    (*c.as_table())["review_trust_delta"].value_or(0.0);
                                c.as_table()->insert("outcome", delta >= 0    ? "polite"
                                                                : delta <= -2 ? "insult"
                                                                              : "threat");
                            }
            }
            continue;
        }
        runs.push_back(toml::table{{"id", text(def, "id")},
                                   {"contact", text(def, "contact")},
                                   {"request_id", text(def, "request_id")},
                                   {"definition", def},
                                   {"flags", toml::table{}},
                                   {"started", false},
                                   {"finished", false}});
    }
    validate(candidate);
    people = std::move(candidate);
}
void ConversationSystem::validate(const toml::table& people) {
    if (people["chat"].as_table() && people["chat"].as_table()->contains("workday"))
        require(people["chat"]["workday"].is_table(), "Invalid workday conversation table");
    const auto* saved = state(people);
    if (!saved)
        return;
    require((*saved)["schema_version"].value_or(0) == 1 && (*saved)["runs"].is_array() &&
                (*saved)["promises"].is_array() && (*saved)["rules"].is_table(),
            "Invalid saved conversation state");
    for (auto key : {"broken_promise_trust_loss", "patience_seconds"}) {
        auto v = (*saved)["rules"][key].value<double>();
        require(v && std::isfinite(*v) && *v >= 0, "Invalid reaction rule");
        if (std::string(key) == "broken_promise_trust_loss")
            require(*v <= 1, "Trust loss exceeds one");
        else
            require(*v > 0, "Patience duration must be positive");
    }
    std::unordered_set<std::string> ids;
    for (const auto& n : *(*saved)["runs"].as_array()) {
        require(n.is_table(), "Invalid saved run");
        const auto& run = *n.as_table();
        require(ids.insert(text(run, "id")).second && run["definition"].is_table() &&
                    run["flags"].is_table() && run["started"].is_boolean() &&
                    run["finished"].is_boolean(),
                "Invalid run identity/state");
        validate_definition(*run["definition"].as_table());
        ConversationRules::validate_definition(*run["definition"].as_table(), &people);
        require(text(run, "id") == text(*run["definition"].as_table(), "id") &&
                    text(run, "contact") == text(*run["definition"].as_table(), "contact") &&
                    text(run, "request_id") == text(*run["definition"].as_table(), "request_id"),
                "Run context does not match its definition");
        require(record(people, "people", text(run, "contact")), "Unknown conversation contact");
        for (const auto& [key, v] : *run["flags"].as_table()) {
            (void)key;
            require(v.is_boolean(), "Run flags must be boolean");
        }
        if (run["started"].value_or(false)) {
            (void)node(run, text(run, "node_id"));
            auto t = run["entered_seconds"].value<double>();
            require(t && std::isfinite(*t) && *t >= 0, "Invalid node entry time");
        }
        if (run.contains("response_due_seconds")) {
            auto due = run["response_due_seconds"].value<double>();
            require(due && std::isfinite(*due) && *due >= 0, "Invalid human response time");
        }
    }
    ids.clear();
    for (const auto& n : *(*saved)["promises"].as_array()) {
        require(n.is_table(), "Invalid commitment");
        const auto& p = *n.as_table();
        require(!text(p, "id").empty() && ids.insert(text(p, "id")).second &&
                    record(people, "work_requests", text(p, "request_id")) &&
                    record(people, "people", text(p, "contact")) &&
                    record(people, "people", text(p, "engineer_id")),
                "Invalid commitment references");
        for (auto key : {"made_seconds", "due_seconds"}) {
            auto t = p[key].value<double>();
            require(t && std::isfinite(*t) && *t >= 0, "Invalid commitment time");
        }
        require(p["due_seconds"].value_or(0.0) > p["made_seconds"].value_or(0.0),
                "Commitment deadline must follow creation");
        const auto status = text(p, "status");
        require(status == "open" || status == "fulfilled" || status == "broken",
                "Invalid commitment outcome");
    }
}
void ConversationSystem::update(toml::table& people, const toml::table& messages,
                                const FileSystem& files, const Network& network,
                                const Identity& actor, double time) {
    (void)messages;
    if (!state(people) || !ChatSystem::connected(people))
        return;
    require(std::isfinite(time) && time >= 0, "Invalid clock");
    settle_promises(people, time);
    auto candidate = people;
    auto& runs = *mutable_state(candidate)["runs"].as_array();
    for (auto& entry : runs) {
        auto& run = *entry.as_table();
        if (run["finished"].value_or(false))
            continue;
        auto* task = record(candidate, "work_requests", text(run, "request_id"));
        if (task && text(*task, "status") == "resolved" &&
            run["definition"]["cancel_when_resolved"].value_or(false)) {
            run.insert_or_assign("finished", true);
            continue;
        }
        if (!run["started"].value_or(false)) {
            if (!trigger(candidate, run, time))
                continue;
            const bool busy = std::any_of(runs.begin(), runs.end(), [&](const auto& other) {
                const auto& r = *other.as_table();
                return text(r, "id") != text(run, "id") &&
                       text(r, "contact") == text(run, "contact") && r["started"].value_or(false) &&
                       !r["finished"].value_or(false);
            });
            if (busy)
                continue;
            run.insert_or_assign("started", true);
            enter(candidate, run, text(*run["definition"].as_table(), "start"), files, network,
                  actor, time);
            if (auto* claim = run["definition"]["claim"].as_table()) {
                PeopleSystem::observe_chat(candidate);
                const auto ev =
                    "chat:" + std::to_string(candidate["chat"]["transcript"].as_array()->size());
                run.insert_or_assign("source_message_id", ev);
                PeopleSystem::record_claim(candidate, text(*claim, "person"), *claim, ev, time);
            }
        } else {
            const auto review_node = run["definition"]["review_node"].value_or(std::string{});
            const auto authority = candidate["delegation"]["status"].value_or(std::string{});
            if (!review_node.empty() && text(run, "node_id") != review_node && !authority.empty() &&
                authority != "not_requested")
                enter(candidate, run, review_node, files, network, actor, time);
            const auto& n = *node(run, text(run, "node_id"));
            if (n["kind"].value_or(std::string{}) != "wait")
                continue;
            const bool ready = n.contains("after_seconds")
                                   ? time >= std::max(run["response_due_seconds"].value_or(0.0),
                                                      run["entered_seconds"].value_or(time) +
                                                          n["after_seconds"].value_or(0.0))
                                   : check(candidate, run, text(n, "check"), files, network,
                                           actor) == n["check_value"].value_or(true);
            if (ready) {
                double transition_time = time;
                if (text(n, "check") == "approval_decided")
                    if (const auto* a = MessageReviewSystem::approval(
                            candidate, run["approval_id"].value_or(std::string{}))) {
                        transition_time = (*a)["decided_seconds"].value_or(time);
                        if (text(*a, "status") == "approved")
                            run.insert_or_assign("sent_message_id", text(*a, "sent_message_id"));
                    }
                enter(candidate, run, text(n, "next"), files, network, actor, transition_time);
            }
        }
    }
    PeopleSystem::observe_chat(candidate);
    validate(candidate);
    people = std::move(candidate);
}
std::vector<toml::table> ConversationSystem::choices(const toml::table& people,
                                                     const std::string& contact,
                                                     const FileSystem& files,
                                                     const Network& network, const Identity& actor,
                                                     double time) {
    (void)time;
    std::vector<toml::table> out;
    if (!state(people))
        return out;
    for (const auto& entry : *(*state(people))["runs"].as_array()) {
        const auto& run = *entry.as_table();
        if (text(run, "contact") != contact || !run["started"].value_or(false) ||
            run["finished"].value_or(false))
            continue;
        const auto& n = *node(run, text(run, "node_id"));
        if (auto* choices = n["choices"].as_array())
            for (const auto& c : *choices) {
                if (!passes(people, run, *c.as_table(), files, network, actor))
                    continue;
                auto copy = *c.as_table();
                copy.insert("conversation_id", text(run, "id"));
                out.push_back(std::move(copy));
            }
    }
    return out;
}
void ConversationSystem::choose(toml::table& people, toml::table& messages, toml::array& events,
                                const FileSystem& files, const Network& network,
                                const Identity& actor, const std::string& contact,
                                const std::string& conversation, const std::string& choice,
                                double time) {
    require(std::isfinite(time) && time >= 0, "Invalid clock");
    const auto options = choices(people, contact, files, network, actor, time);
    auto selected = std::find_if(options.begin(), options.end(), [&](const auto& c) {
        return text(c, "id") == choice && text(c, "conversation_id") == conversation;
    });
    require(selected != options.end(), "Response no longer available");
    auto p = people, m = messages;
    auto e = events;
    auto& run = mutable_record(mutable_state(p), "runs", conversation);
    if (ChatSystem::connected(p))
        EmailSystem::check_connection(m, network);
    action(p, m, e, run, *selected, files, network, actor, time);
    if (auto* list = (*selected)["reveal_tabs"].as_array())
        for (const auto& tab : *list)
            ChatSystem::reveal_tab(p, tab.value_or(std::string{}));
    if (text(*selected, "action") != "send_chat_reply" &&
        text(*selected, "action") != "draft_reply")
        stamp(p, contact, "self:assistant", text(*selected, "text"), time, conversation);
    effects(run, *selected);
    ConversationRules::apply(p, *selected);
    const auto* approval =
        MessageReviewSystem::approval(p, run["approval_id"].value_or(std::string{}));
    const bool pending = text(*selected, "action") == "send_chat_reply" && approval &&
                         text(*approval, "status") == "pending";
    enter(p, run,
          pending ? run["definition"]["approval_wait_node"].value_or(std::string{})
                  : text(*selected, "next"),
          files, network, actor, time);
    PeopleSystem::observe_chat(p);
    validate(p);
    WorkdaySystem::validate(p, e);
    EmailSystem::validate(m);
    people = std::move(p);
    messages = std::move(m);
    events = std::move(e);
}
std::string ConversationSystem::anchor(const toml::table& people, const std::string& contact) {
    if (!state(people))
        return {};
    for (const auto& n : *(*state(people))["runs"].as_array()) {
        const auto& run = *n.as_table();
        if (text(run, "contact") != contact || !run["started"].value_or(false) ||
            run["finished"].value_or(false) ||
            run["definition"]["trigger"].value_or(std::string{}) != "scheduled_chat")
            continue;
        return text(*node(run, run["definition"]["start"].value_or(std::string{})), "text");
    }
    return {};
}
std::optional<double> ConversationSystem::next_response(const toml::table& people, double time) {
    std::optional<double> due;
    if (!state(people))
        return due;
    for (const auto& n : *(*state(people))["runs"].as_array()) {
        const auto& run = *n.as_table();
        if (run["finished"].value_or(false))
            continue;
        if (!run["started"].value_or(false)) {
            if (run["definition"]["trigger"].value_or(std::string{}) == "reply_reviewed") {
                const auto* review = source_review(people, run);
                if (review && text(*review, "status") == "reviewed") {
                    const double t = std::max(time, (*review)["reviewed_seconds"].value_or(time));
                    if (!due || t < *due)
                        due = t;
                }
            }
            if (ChatSystem::connected(people) &&
                run["definition"]["trigger"].value_or(std::string{}) == "scheduled_chat") {
                const double t =
                    std::max(time, people["chat"]["connected_seconds"].value_or(time) +
                                       run["definition"]["arrive_after_seconds"].value_or(0.0));
                if (!due || t < *due)
                    due = t;
            }
            continue;
        }
        const auto& current = *node(run, text(run, "node_id"));
        if (current["kind"].value_or(std::string{}) != "wait")
            continue;
        if (text(current, "check") == "approval_decided") {
            if (const auto* a = MessageReviewSystem::approval(
                    people, run["approval_id"].value_or(std::string{}))) {
                const double t = std::max(time, (*a)["due_seconds"].value_or(time));
                if (!due || t < *due)
                    due = t;
            }
            continue;
        }
        if (!current.contains("after_seconds"))
            continue;
        const double t = std::max(
            {time, run["response_due_seconds"].value_or(0.0),
             run["entered_seconds"].value_or(time) + current["after_seconds"].value_or(0.0)});
        if (!due || t < *due)
            due = t;
    }
    return due;
}
std::optional<double> ConversationSystem::next_promise(const toml::table& people, double limit) {
    std::optional<double> due;
    if (!state(people))
        return due;
    for (const auto& n : *(*state(people))["promises"].as_array()) {
        const auto& p = *n.as_table();
        const double t = p["due_seconds"].value_or(limit);
        if (text(p, "status") == "open" && t <= limit && (!due || t < *due))
            due = t;
    }
    if (auto* tasks = people["work_requests"].as_array())
        for (const auto& n : *tasks) {
            const auto& task = *n.as_table();
            const double t = task["arrival_seconds"].value_or(limit) +
                             (*state(people))["rules"]["patience_seconds"].value_or(1800.0);
            if (task["patience_reminder_sent"].value_or(false) ||
                (text(task, "status") == "resolved" &&
                 task["resolved_seconds"].value_or(limit) <= t))
                continue;
            if (t <= limit && (!due || t < *due))
                due = t;
        }
    return due;
}
void ConversationSystem::settle_promises(toml::table& people, double time) {
    if (!state(people))
        return;
    for (auto& n : *mutable_state(people)["promises"].as_array()) {
        auto& p = *n.as_table();
        if (text(p, "status") != "open")
            continue;
        const auto* task = record(people, "work_requests", text(p, "request_id"));
        if (!task)
            continue;
        const double due = p["due_seconds"].value_or(time),
                     resolved = (*task)["resolved_seconds"].value_or(time + 1);
        if (text(*task, "status") == "resolved" && resolved <= due) {
            p.insert_or_assign("status", "fulfilled");
            continue;
        }
        if (time < due)
            continue;
        p.insert_or_assign("status", "broken");
        auto& owner = mutable_record(people, "people", text(p, "engineer_id"));
        auto& attitude = *owner["ai_attitudes"].as_table();
        const double loss =
            mutable_state(people)["rules"]["broken_promise_trust_loss"].value_or(.1);
        attitude.insert_or_assign("email_trust",
                                  std::max(0.0, attitude["email_trust"].value_or(.5) - loss));
        auto& colleague = mutable_record(people, "people", text(p, "contact"));
        auto& colleague_attitude = *colleague["ai_attitudes"].as_table();
        colleague_attitude.insert_or_assign(
            "email_trust", std::max(0.0, colleague_attitude["email_trust"].value_or(.5) - loss));
        stamp(people, text(p, "contact"), text(p, "contact"),
              "The time you promised has passed. I still need an acknowledged delivery, not "
              "another estimate.",
              due);
        stamp(people, text(p, "engineer_id"), text(p, "engineer_id"),
              "You committed to a deadline without controlling access. Missing it makes me less "
              "confident in delegating work. Give a factual status update instead.",
              due);
    }
    if (auto* tasks = people["work_requests"].as_array())
        for (auto& n : *tasks) {
            auto& task = *n.as_table();
            const double due = task["arrival_seconds"].value_or(time) +
                               mutable_state(people)["rules"]["patience_seconds"].value_or(1800.0);
            if (task["patience_reminder_sent"].value_or(false) || time < due ||
                (text(task, "status") == "resolved" &&
                 task["resolved_seconds"].value_or(time) <= due))
                continue;
            task.insert_or_assign("patience_reminder_sent", true);
            const auto colleague = text(task, "colleague_id");
            stamp(people, colleague, colleague,
                  "I have been waiting for the document. What is still unresolved? Please give me "
                  "a concrete status rather than another assurance.",
                  due);
        }
    PeopleSystem::observe_chat(people);
}
std::vector<std::string> ConversationSystem::context(const toml::table& people,
                                                     const std::string& contact, double time) {
    std::vector<std::string> out;
    if (!state(people))
        return out;
    if (contact == "jessica" || contact == "engineer")
        out.push_back(MessageReviewSystem::approval_required(people)
                          ? "Reply authority: Mark must approve drafts"
                          : "Reply authority: independent replies; occasional checks");
    if (people["phase_work"].is_table() && contact != "jessica") {
        if (const auto* owner = record(people, "people", "engineer")) {
            const double remaining =
                std::max(0.0, (*owner)["busy_until_seconds"].value_or(time) - time);
            if (remaining > 0)
                out.push_back("Mark's work queue: " +
                              std::to_string(static_cast<int>(std::ceil(remaining / 60))) +
                              " working minutes");
        }
        if (auto* tasks = people["work_requests"].as_array())
            for (const auto& n : *tasks) {
                const auto& t = *n.as_table();
                if (text(t, "colleague_id") != contact || !t.contains("deadline_seconds") ||
                    text(t, "status") == "resolved")
                    continue;
                const double left = t["deadline_seconds"].value_or(time) - time;
                out.push_back(left < 0
                                  ? "Work deadline: missed; request remains open"
                                  : "Work deadline: " +
                                        std::to_string(static_cast<int>(std::ceil(left / 60))) +
                                        " working minutes remaining");
                break;
            }
    }
    for (const auto& n : *(*state(people))["promises"].as_array()) {
        const auto& p = *n.as_table();
        if (text(p, "contact") != contact && text(p, "engineer_id") != contact)
            continue;
        std::ostringstream label;
        label << "Delivery commitment: " << text(p, "status");
        if (text(p, "status") == "open")
            label << " | " << std::max(0.0, p["due_seconds"].value_or(time) - time)
                  << " working seconds left";
        out.push_back(label.str());
    }
    if (auto* tasks = people["work_requests"].as_array())
        for (const auto& n : *tasks) {
            const auto& task = *n.as_table();
            if (text(task, "colleague_id") != contact ||
                task["arrival_seconds"].value_or(time + 1) > time ||
                text(task, "status") == "resolved")
                continue;
            const double waiting = std::max(0.0, time - task["arrival_seconds"].value_or(time));
            const double tolerance = (*state(people))["rules"]["patience_seconds"].value_or(1800.0);
            const auto access = text(task, "access_status");
            const std::string phase = task.contains("delivery_sent_seconds")
                                          ? "Awaiting document acknowledgement"
                                      : access == "engineer_queued" ? "Engineer reviewing access"
                                      : access == "owner_queued"    ? "Owner reviewing access"
                                      : access == "granted"         ? "Access granted"
                                      : access == "denied"          ? "Owner declined access"
                                      : access == "failed"          ? "Access update failed"
                                                                    : "Waiting for document";
            out.push_back(phase + " | " + text(task, "id") +
                          (waiting >= tolerance ? " | long wait" : ""));
            if (task["question_asked"].value_or(false) && !task["investigated"].value_or(false)) {
                bool answered = false;
                for (const auto& run : *(*state(people))["runs"].as_array())
                    if ((*run.as_table())["request_id"].value_or(std::string{}) == text(task, "id"))
                        answered |= (*run.as_table())["flags"]["clarified"].value_or(false);
                out.push_back(answered
                                  ? "Clarification received; document still needs investigation."
                                  : "Clarification requested; document still needs investigation.");
            }
            if (task["investigated"].value_or(false))
                out.push_back(text(task, "finding"));
        }
    if (people["delegation"]["engineer_id"].value_or(std::string{}) == contact &&
        people["delegation"]["status"].value_or(std::string{}) == "pending")
        out.push_back("Responsibility review queued; authority unchanged.");
    return out;
}
} // namespace ai_sim
