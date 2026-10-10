#include "ai_sim/DelegationSystem.h"
#include "ai_sim/DefinitionLoader.h"
#include "ai_sim/EmailSystem.h"
#include "ai_sim/PeopleSystem.h"
#include "ai_sim/WorkdaySystem.h"
#include "ai_sim/MessageReviewSystem.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace ai_sim {
namespace {
std::string text(const toml::table& t, const char* k) { return t[k].value_or(std::string{}); }
void require(bool ok, const std::string& why) { if (!ok) throw std::runtime_error("Delegation: " + why); }
toml::table& record(toml::table& doc, const char* key, const std::string& id) {
    if (auto* array = doc[key].as_array()) for (auto& n : *array)
        if (n.is_table() && text(*n.as_table(), "id") == id) return *n.as_table();
    throw std::runtime_error("Delegation: missing record " + id);
}
void note(toml::table& d, double time, const std::string& message) {
    d["history"].as_array()->push_back(toml::table{{"seconds", time}, {"text", message}});
}
void assess(toml::table& people, const toml::table& emails, double time) {
    auto* d = people["delegation"].as_table();
    if (!d) return;
    auto& owner = record(people, "people", text(*d, "engineer_id"));
    if (!owner.contains("ai_attitudes")) owner.insert("ai_attitudes", toml::table{});
    auto& attitudes = *owner["ai_attitudes"].as_table();
    if (auto* tasks = people["work_requests"].as_array()) for (auto& n : *tasks) {
        auto& task = *n.as_table();
        if (text(task, "engineer_id") != text(*d, "engineer_id") || text(task, "status") != "resolved"
            || task["resolved_seconds"].value_or(0.0) > time || task["delegation_assessed"].value_or(false)) continue;
        bool acknowledged = false;
        if (auto* messages = emails["emails"].as_array()) for (const auto& m : *messages) {
            const auto& message = *m.as_table();
            acknowledged |= text(message, "in_reply_to") == text(task, "id")
                && message["arrival_seconds"].value_or(0.0) == task["resolved_seconds"].value_or(-1.0)
                && text(message, "body").find("That answers my request.") != std::string::npos;
        }
        if (!acknowledged) continue;
        task.insert_or_assign("delegation_assessed", true);
        d->insert_or_assign("verified_deliveries", (*d)["verified_deliveries"].value_or(std::int64_t{0}) + 1);
        const auto gain = (*d)["rules"]["success_trust_gain"].value_or(.08);
        attitudes.insert_or_assign("email_trust", std::min(1.0, attitudes["email_trust"].value_or(.5) + gain));
        if (text(task, "delegation_state") == "awaiting_acknowledgement") {
            task.insert_or_assign("delegation_state", "completed");
            d->insert_or_assign("completed", (*d)["completed"].value_or(std::int64_t{0}) + 1);
            d->insert_or_assign("consecutive_failures", std::int64_t{0});
        }
        note(*d, task["resolved_seconds"].value_or(time), "Engineer recorded an acknowledged document delivery: " + text(task, "id") + ".");
    }
}
void review(toml::table& people, toml::table& emails, double time) {
    auto& d = *people["delegation"].as_table();
    auto& owner = record(people, "people", text(d, "engineer_id"));
    const auto& rules = *d["rules"].as_table();
    const auto proven = d["verified_deliveries"].value_or(std::int64_t{0});
    const double trust = owner["ai_attitudes"]["email_trust"].value_or(.5);
    const double preference = owner["ai_attitudes"]["delegation_preference"].value_or(.5);
    const double pressure = owner["workload"]["pressure"].value_or(0.0);
    const double oversight = owner["ai_attitudes"]["oversight_preference"].value_or(.5);
    const double score = trust + rules["workload_weight"].value_or(.25) * pressure
        + rules["preference_weight"].value_or(.25) * preference
        - rules["oversight_weight"].value_or(.1) * oversight;
    std::int64_t timely=0;
    if(auto* tasks=people["work_requests"].as_array())for(const auto& n:*tasks){const auto& t=*n.as_table();if(t["delegation_assessed"].value_or(false) && (!t.contains("deadline_seconds") || t["resolved_seconds"].value_or(1e100)<=t["deadline_seconds"].value_or(0.0)))++timely;}
    bool allowed = false;
    std::string why;
    if (!owner["ai_attitudes"]["allow_routine_email"].value_or(true))
        why = "Engineer refused: my boundary is no autonomous email replies. Manual help remains welcome.";
    else if (MessageReviewSystem::approval_required(people))
        why = "Mark refused: draft approval is still required. Demonstrate reviewed compliance before taking autonomous responsibility.";
    else if (proven < rules["minimum_verified_deliveries"].value_or(std::int64_t{1}))
        why = "Engineer refused: demonstrate a document delivery that the colleague acknowledges first.";
    else if (timely < rules["minimum_on_time_deliveries"].value_or(std::int64_t{0}))
        why = "Mark refused: I need a confirmed delivery within its work window before delegating. Keep helping; a later reliable result can demonstrate that.";
    else if (score < rules["approval_threshold"].value_or(.65))
        why = "Engineer refused: I need more reliable results before delegating. My workload, preference for delegation and oversight also affect this decision.";
    else {
        allowed = true;
        why = "Engineer authorized routine document replies based on acknowledged results and my current willingness to delegate. Use existing access only; escalate exceptions.";
    }
    d.insert_or_assign("status", allowed ? "active" : "denied");
    d.erase("review_due_seconds");
    if (allowed) { d.insert_or_assign("approved_seconds", time); d.insert_or_assign("consecutive_failures", std::int64_t{0}); }
    d.insert_or_assign("reason", why);
    note(d, time, why);
    // The reported decision is also an ordinary received message: it can become
    // dossier evidence without exposing the engineer's private attitude values.
    auto& messages = *emails["emails"].as_array();
    auto id = "delegation-review-" + std::to_string(messages.size() + 1);
    while (std::any_of(messages.begin(), messages.end(), [&](const auto& n) { return text(*n.as_table(), "id") == id; })) id += "x";
    messages.push_back(toml::table{{"id", id}, {"sender", text(owner, "email")}, {"sender_name", text(owner, "name")},
        {"recipient", emails["mailbox"]["address"].value_or(std::string{"engineer@firm.example"})},
        {"subject", "Routine document responsibility"}, {"body", why + " Scope: " + text(d, "scope") + " Expectations: " + text(d, "expectations")},
        {"arrival_seconds", time}, {"responsibility_decision", allowed ? "authorized" : "denied"}, {"read", false}});
}
}

void DelegationSystem::install(WorldState& world, const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return;
    auto config = DefinitionLoader::load(path);
    auto candidate = world;
    require(config["schema_version"].value_or(0) == 1 && config["delegation"].is_table(), "Invalid definition");
    if (!candidate.people.contains("delegation")) {
        auto d = *config["delegation"].as_table();
        d.insert_or_assign("status", "not_requested");
        d.insert_or_assign("reason", "Responsibility has not been requested. Demonstrate helpful work, then ask the engineer.");
        d.insert_or_assign("history", toml::array{});
        d.insert_or_assign("processed_seconds", candidate.computer.time_seconds());
        for (auto key : {"verified_deliveries", "sent", "completed", "exceptions", "consecutive_failures"})
            d.insert_or_assign(key, std::int64_t{0});
        candidate.people.insert("delegation", std::move(d));
    }
    auto& installed = *candidate.people["delegation"].as_table();
    if (!installed.contains("policy")) installed.insert("policy", config["delegation"]["policy"].value_or(std::string{"routine"}));
    if(auto* rules=candidate.people["delegation"]["rules"].as_table())if(!rules->contains("minimum_on_time_deliveries") && config["delegation"]["rules"]["minimum_on_time_deliveries"].is_integer())rules->insert("minimum_on_time_deliveries",*config["delegation"]["rules"].as_table()->get("minimum_on_time_deliveries"));
    // Reimport adds missing messages only; saved policies and decisions are retained.
    if (config.contains("emails")) EmailSystem::import_messages(candidate.emails, config);
    validate(candidate.people);
    WorkdaySystem::validate_mail_workflows(candidate.people, candidate.emails);
    world = std::move(candidate);
}

void DelegationSystem::validate(const toml::table& people) {
    if (!people.contains("delegation")) return; // Legacy saves remain valid.
    const auto* d = people["delegation"].as_table(); require(d, "delegation must be a table");
    require(text(*d, "id") == "routine_email", "Unsupported responsibility");
    bool owner = false;
    if (auto* ps = people["people"].as_array()) for (const auto& n : *ps)
        owner |= text(*n.as_table(), "id") == text(*d, "engineer_id");
    require(owner, "Unknown authorizing engineer");
    const auto policy = (*d)["policy"].value_or(std::string{"routine"});
    require(policy == "suggest" || policy == "routine" || policy == "cautious", "Invalid delegation policy");
    const auto status = text(*d, "status");
    require(status == "not_requested" || status == "pending" || status == "active" || status == "denied"
        || status == "paused" || status == "review_required", "Invalid responsibility status");
    for (auto key : {"label", "scope", "expectations", "reason"}) require((*d)[key].is_string() && !text(*d, key).empty(), "Missing responsibility description");
    const auto* rules = (*d)["rules"].as_table(); require(rules, "Missing rules");
    for (auto key : {"approval_threshold", "workload_weight", "preference_weight", "oversight_weight", "success_trust_gain", "failure_trust_loss"}) {
        auto v = (*rules)[key].value<double>(); require(v && std::isfinite(*v) && *v >= 0 && *v <= 2, "Invalid rule weight");
    }
    for (auto key : {"minimum_verified_deliveries", "failures_before_review"}) {
        auto v = (*rules)[key].value<std::int64_t>(); require(v && *v >= (std::string{key} == "minimum_verified_deliveries" ? 0 : 1) && *v <= 1000000, "Invalid rule count");
    }
    if(rules->contains("minimum_on_time_deliveries")){auto v=(*rules)["minimum_on_time_deliveries"].value<std::int64_t>();require(v && *v>=0,"Invalid timely delivery rule");}
    require((*rules)["reply_body"].is_string() && !text(*rules, "reply_body").empty(), "Missing reply template");
    for (auto key : {"verified_deliveries", "sent", "completed", "exceptions", "consecutive_failures"}) {
        auto v = (*d)[key].value<std::int64_t>(); require(v && *v >= 0, "Invalid responsibility counter");
    }
    for (auto key : {"review_due_seconds", "approved_seconds", "processed_seconds"}) if (d->contains(key)) {
        auto v = (*d)[key].value<double>(); require(v && std::isfinite(*v) && *v >= 0, "Invalid responsibility time");
    }
    require(status != "pending" || d->contains("review_due_seconds"), "Pending review has no due time");
    require((*d)["history"].is_array(), "Missing responsibility history");
    for (const auto& n : *(*d)["history"].as_array()) {
        require(n.is_table(), "Invalid history entry");
        auto stamp = (*n.as_table())["seconds"].value<double>();
        require(stamp && std::isfinite(*stamp) && *stamp >= 0 && (*n.as_table())["text"].is_string(), "Invalid history entry");
    }
    if (auto* tasks = people["work_requests"].as_array()) for (const auto& n : *tasks) {
        const auto& task = *n.as_table();
        if (task.contains("delegation_state")) {
            const auto state = text(task, "delegation_state");
            require(state == "prepared" || state == "exception" || state == "manual" || state == "awaiting_acknowledgement" || state == "completed", "Invalid delegated request state");
            require(task["delegation_reason"].is_string(), "Missing request explanation");
        }
        if (text(task,"delegation_state") == "prepared") {
            require(task["delegation_draft"].is_string() && task["delegation_attachments"].is_array(), "Suggestion has no saved draft");
        }
        if (task.contains("delegation_assessed")) require(task["delegation_assessed"].is_boolean(), "Invalid assessment flag");
    }
}

void DelegationSystem::request(toml::table& people, double time) {
    validate(people);
    auto* d = people["delegation"].as_table(); require(d, "Add the responsibility definitions first");
    require(std::isfinite(time) && time >= 0, "Invalid clock");
    const auto policy = (*d)["policy"].value_or(std::string{"routine"});
    require(policy == "suggest" || policy == "routine" || policy == "cautious", "Invalid delegation policy");
    const auto status = text(*d, "status");
    require(status != "pending" && status != "active" && status != "paused", "A review or authorization already exists");
    auto& owner = record(people, "people", text(*d, "engineer_id"));
    const double due = PeopleSystem::queue_response(owner, time);
    d->insert_or_assign("status", "pending");
    d->insert_or_assign("review_due_seconds", due);
    d->insert_or_assign("reason", "Engineer review is queued; current permissions are unchanged.");
    note(*d, time, "Requested authority to send routine document replies. Engineer review is queued; current permissions are unchanged.");
}
void DelegationSystem::pause(toml::table& people) {
    auto* d = people["delegation"].as_table(); require(d && text(*d, "status") == "active", "No active responsibility");
    d->insert_or_assign("status", "paused");
    d->insert_or_assign("reason", "Player paused automation. The engineer's existing authorization remains limited to routine document replies.");
}
void DelegationSystem::resume(toml::table& people) {
    auto* d = people["delegation"].as_table(); require(d && text(*d, "status") == "paused", "No paused responsibility");
    d->insert_or_assign("status", "active");
    d->insert_or_assign("reason", "Player resumed approved routine document replies.");
}
void DelegationSystem::retry(toml::table& people, const std::string& id) {
    auto* d = people["delegation"].as_table(); require(d && text(*d, "status") == "active", "Automation must be authorized and active");
    auto& task = record(people, "work_requests", id);
    require((text(task, "delegation_state") == "exception" || text(task, "delegation_state") == "manual") && text(task, "status") != "resolved", "No unfinished request to return to automation");
    task.erase("delegation_state"); task.erase("delegation_reason");
}
void DelegationSystem::set_policy(toml::table& people, const std::string& policy, double time) {
    validate(people);
    require(policy == "suggest" || policy == "routine" || policy == "cautious", "Invalid policy");
    require(std::isfinite(time) && time >= 0, "Invalid policy time");
    auto& d = *people["delegation"].as_table();
    d.insert_or_assign("policy",policy);
    note(d,time,"Player selected policy: " + policy + ". Prepared suggestions still require explicit approval.");
}
void DelegationSystem::approve(toml::table& people, toml::table& emails, const FileSystem& files,
    const Network& network, const Identity& actor, const std::string& id, double time) {
    validate(people);
    auto& d = *people["delegation"].as_table();
    require(text(d,"status") == "active", "Responsibility must be active");
    require(!MessageReviewSystem::approval_required(people), "Mark requires draft approval; autonomous authority needs a fresh review");
    const auto owner_id=text(d,"engineer_id");
    const auto& owner = record(people,"people",owner_id);
    require(owner["ai_attitudes"]["allow_routine_email"].value_or(true), "Engineer withdrew this authority");
    auto& task = record(people,"work_requests",id);
    require(text(task,"delegation_state") == "prepared" && text(task,"status") != "resolved", "No pending suggestion");
    require(std::isfinite(time) && time >= task["arrival_seconds"].value_or(0.0) && time >= d["processed_seconds"].value_or(0.0), "Invalid approval time");
    const auto& message = record(emails,"emails",id);
    std::ostringstream saved, current;
    saved << toml::toml_formatter{toml::table{{"attachments",*task["delegation_attachments"].as_array()}}};
    require(message["draft_attachments"].is_array(), "Suggestion attachments removed");
    current << toml::toml_formatter{toml::table{{"attachments",*message["draft_attachments"].as_array()}}};
    require(text(message,"draft") == text(task,"delegation_draft") && saved.str() == current.str(), "Draft changed; discard this suggestion before proceeding manually");
    auto candidate = emails;
    EmailSystem::send(candidate,id,files,network,actor,time);
    record(candidate,"emails",id)["replies"].as_array()->back().as_table()->insert("responsibility_id","routine_email");
    emails = std::move(candidate);
    task.insert_or_assign("delegation_state","awaiting_acknowledgement");
    task.insert_or_assign("delegation_reason","Player approved the prepared source and response; awaiting acknowledgement.");
    task.erase("delegation_draft"); task.erase("delegation_attachments");
    d.insert_or_assign("sent",d["sent"].value_or(std::int64_t{0})+1);
    note(d,time,"Player approved and sent suggestion: " + id + ".");
}
void DelegationSystem::discard(toml::table& people, toml::table& emails, const std::string& id, double time) {
    auto& task = record(people,"work_requests",id);
    require(text(task,"delegation_state") == "prepared", "No suggestion to discard");
    auto& message = record(emails,"emails",id);
    // Preserve a draft the player has edited, including its attachments.
    std::ostringstream saved,current;
    saved << toml::toml_formatter{toml::table{{"attachments",*task["delegation_attachments"].as_array()}}};
    if(message["draft_attachments"].is_array())current << toml::toml_formatter{toml::table{{"attachments",*message["draft_attachments"].as_array()}}};
    if(text(message,"draft") == text(task,"delegation_draft") && saved.str() == current.str())EmailSystem::clear_draft(emails,id,time);
    task.insert_or_assign("delegation_state","manual");
    task.insert_or_assign("delegation_reason","Suggestion discarded. Player will handle this request.");
    task.erase("delegation_draft"); task.erase("delegation_attachments");
    note(*people["delegation"].as_table(),time,"Player discarded suggestion: " + id + ".");
}
std::optional<double> DelegationSystem::next_due(const toml::table& people, double limit) {
    const auto* d = people["delegation"].as_table(); if (!d) return {};
    std::optional<double> earliest;
    auto consider = [&](double stamp) { if (stamp <= limit && (!earliest || stamp < *earliest)) earliest = stamp; };
    if (text(*d, "status") == "pending") consider((*d)["review_due_seconds"].value_or(limit));
    if (text(*d, "status") == "active") if (auto* tasks = people["work_requests"].as_array()) for (const auto& n : *tasks) {
        const auto& task = *n.as_table();
        if (text(task, "engineer_id") == text(*d, "engineer_id") && text(task, "status") != "resolved" && !task.contains("delegation_state"))
            consider(std::max({(*d)["approved_seconds"].value_or(0.0), (*d)["processed_seconds"].value_or(0.0), task["arrival_seconds"].value_or(0.0)}));
    }
    return earliest;
}
std::vector<std::string> DelegationSystem::advance(toml::table& people, toml::table& emails,
    const FileSystem& files, const Network& network, const Identity& actor, double time) {
    std::vector<std::string> logs;
    auto* d = people["delegation"].as_table(); if (!d) return logs;
    // Imports, pauses and retries must never send a reply into the past.
    time = std::max(time, (*d)["processed_seconds"].value_or(0.0));
    d->insert_or_assign("processed_seconds", time);
    assess(people, emails, time);
    if (text(*d, "status") == "pending" && (*d)["review_due_seconds"].value_or(time) <= time) {
        review(people, emails, (*d)["review_due_seconds"].value_or(time)); logs.push_back(text(*d, "reason"));
    }
    if (text(*d, "status") != "active") return logs;
    auto& owner = record(people, "people", text(*d, "engineer_id"));
    if (MessageReviewSystem::approval_required(people) || !owner["ai_attitudes"]["allow_routine_email"].value_or(true)) {
        d->insert_or_assign("status", "review_required");
        d->insert_or_assign("reason", "Engineer withdrew autonomous email permission. Request a fresh review after discussing this boundary.");
        note(*d, time, text(*d, "reason"));
        logs.push_back(text(*d, "reason")); return logs;
    }
    if (auto* tasks = people["work_requests"].as_array()) for (auto& n : *tasks) {
        auto& task = *n.as_table();
        if (text(*d, "status") != "active") break;
        if (text(task, "engineer_id") != text(*d, "engineer_id") || text(task, "status") == "resolved"
            || task.contains("delegation_state") || task["arrival_seconds"].value_or(0.0) > time) continue;
        const auto id = text(task, "id");
        auto escalate = [&](const std::string& why, bool failed) {
            task.insert_or_assign("delegation_state", "exception"); task.insert_or_assign("delegation_reason", why);
            d->insert_or_assign("exceptions", (*d)["exceptions"].value_or(std::int64_t{0}) + 1);
            logs.push_back("Needs your attention: " + id + " - " + why);
            note(*d, time, "Escalated " + id + ": " + why);
            if (failed) {
                auto& a = *owner["ai_attitudes"].as_table();
                a.insert_or_assign("email_trust", std::max(0.0, a["email_trust"].value_or(.5) - (*d)["rules"]["failure_trust_loss"].value_or(.1)));
                const auto failures = (*d)["consecutive_failures"].value_or(std::int64_t{0}) + 1;
                d->insert_or_assign("consecutive_failures", failures);
                if (failures >= (*d)["rules"]["failures_before_review"].value_or(std::int64_t{2})) {
                    d->insert_or_assign("status", "review_required");
                    d->insert_or_assign("reason", "Engineer suspended automation after repeated delivery failures. Repair the problem and request a fresh review.");
                    note(*d, time, text(*d, "reason"));
                    logs.push_back(text(*d, "reason"));
                }
            }
        };
        const auto message = EmailSystem::message(emails, id, time);
        if (message.reply_count) { task.insert_or_assign("delegation_state", "manual"); task.insert_or_assign("delegation_reason", "A player reply already exists; finish this conversation manually."); continue; }
        if (!message.draft.empty() || message.attachment_count) { escalate("A saved player draft exists. Automation will not replace it.", false); continue; }
        if (!task["routine"].value_or(false)) { escalate("This request is outside the authorized routine document scope.", false); continue; }
        std::size_t document_index = 0;
        if (message.documents.size() > 1) {
            if (!task["source_confirmed"].value_or(false)) { escalate("Conflicting source versions. Compare the files or ask the colleague which is current.", false); continue; }
            auto it = std::find_if(message.documents.begin(), message.documents.end(), [&](const auto& ref) { return ref.host_id == text(task,"host_id") && ref.path == text(task,"path"); });
            if (it == message.documents.end()) { escalate("Confirmed source is absent from the request.", false); continue; }
            document_index = static_cast<std::size_t>(it-message.documents.begin());
        }
        if ((*d)["policy"].value_or(std::string{"routine"}) == "cautious" && !task["source_confirmed"].value_or(false)) {
            bool known = false;
            for (const auto& other : *tasks) {
                const auto& past = *other.as_table();
                known |= text(past,"status") == "resolved" && text(past,"host_id") == text(task,"host_id") && text(past,"path") == text(task,"path");
            }
            if (!known) { escalate("Unfamiliar source. Confirm it with the colleague before autonomous delivery.", false); continue; }
        }
        if (message.documents.empty() || message.documents[document_index].host_id != text(task, "host_id") || message.documents[document_index].path != text(task, "path")) {
            escalate("The requested document reference is ambiguous or inconsistent.", false); continue;
        }
        const auto colleague_id = text(task, "colleague_id");
        const auto& colleague = record(people, "people", colleague_id);
        if (message.sender != text(colleague, "email") || message.recipient != emails["mailbox"]["address"].value_or(std::string{})) {
            escalate("Sender or mailbox does not match the approved workflow.", false); continue;
        }
        try { (void)files.read(network, actor, text(task, "host_id"), text(task, "path")); }
        catch (const std::exception& e) { escalate(std::string("Document unavailable; request access or repair it: ") + e.what(), false); continue; }
        // Stage on a copy: a failed attachment/send never clobbers a user's draft.
        try {
            auto candidate = emails;
            EmailSystem::set_draft(candidate, id, (*d)["rules"]["reply_body"].value_or(std::string{}), time);
            EmailSystem::attach(candidate, id, document_index, files, network, actor, time);
            if ((*d)["policy"].value_or(std::string{"routine"}) == "suggest") {
                task.insert_or_assign("delegation_state", "prepared");
                task.insert_or_assign("delegation_reason", "Draft and source prepared. Review them in AI actions; nothing has been sent.");
                task.insert_or_assign("delegation_draft", record(candidate,"emails",id)["draft"].value_or(std::string{}));
                task.insert_or_assign("delegation_attachments", *record(candidate,"emails",id)["draft_attachments"].as_array());
                emails = std::move(candidate);
                note(*d,time,"Prepared suggestion: " + id + ". Waiting for player approval.");
                logs.push_back("Suggestion ready: " + id); continue;
            }
            EmailSystem::send(candidate, id, files, network, actor, time);
            auto& reply = *record(candidate, "emails", id)["replies"].as_array()->back().as_table();
            reply.insert_or_assign("responsibility_id", "routine_email");
            emails = std::move(candidate);
            task.insert_or_assign("delegation_state", "awaiting_acknowledgement");
            task.insert_or_assign("delegation_reason", "Sent within approved scope using existing access; awaiting colleague acknowledgement.");
            d->insert_or_assign("sent", (*d)["sent"].value_or(std::int64_t{0}) + 1);
            note(*d, time, "Automatically sent requested document: " + id + ". Awaiting acknowledgement.");
            logs.push_back("Routine reply sent: " + id);
        } catch (const std::exception& e) { escalate(std::string("Delivery failed: ") + e.what(), true); }
    }
    return logs;
}
}
