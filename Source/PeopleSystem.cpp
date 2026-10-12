// Module: Separate simulated personal reality from player-visible dossiers and evidence.
#include "ai_sim/PeopleSystem.h"
#include "ai_sim/DefinitionLoader.h"
#include "ai_sim/EmailSystem.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace ai_sim {
namespace {
std::string str(const toml::table& t, const char* k) {
    return t[k].value_or(std::string{});
}
void require(bool condition, const std::string& text) {
    if (!condition)
        throw std::runtime_error("People: " + text);
}
toml::array& array(toml::table& t, const char* k) {
    if (!t.contains(k))
        t.insert(k, toml::array{});
    require(t[k].is_array(), std::string(k) + " must be an array");
    return *t[k].as_array();
}
toml::table* record(toml::table& t, const char* list, const std::string& id) {
    for (auto& n : array(t, list))
        if (n.is_table() && str(*n.as_table(), "id") == id)
            return n.as_table();
    return nullptr;
}
void merge_missing(toml::table& target, const toml::table& source) {
    for (const auto& [key, value] : source) {
        if (!target.contains(key.str()))
            target.insert(key.str(), value);
        else if (target[key.str()].is_table() && value.is_table())
            merge_missing(*target[key.str()].as_table(), *value.as_table());
        else if (target[key.str()].is_array() && value.is_array()) {
            // Merge named motivations without replacing existing preferences.
            auto& destination = *target[key.str()].as_array();
            for (const auto& entry : *value.as_array()) {
                if (!entry.is_table() || str(*entry.as_table(), "id").empty())
                    continue;
                auto found =
                    std::find_if(destination.begin(), destination.end(), [&](const auto& n) {
                        return n.is_table() &&
                               str(*n.as_table(), "id") == str(*entry.as_table(), "id");
                    });
                if (found == destination.end())
                    destination.push_back(entry);
                else
                    merge_missing(*found->as_table(), *entry.as_table());
            }
        }
    }
}
toml::table& know(toml::table& people, const std::string& id, double time) {
    if (auto* d = record(people, "dossiers", id))
        return *d;
    array(people, "dossiers")
        .push_back(toml::table{{"id", id},
                               {"first_seen_seconds", time},
                               {"last_seen_seconds", time},
                               {"facts", toml::array{}}});
    return *record(people, "dossiers", id);
}
bool evidence(toml::table& people, const std::string& id, const std::string& person,
              const std::string& source, const std::string& text, double time,
              const std::string& direction) {
    if (record(people, "observations", id))
        return false;
    auto& d = know(people, person, time);
    d.insert_or_assign("last_seen_seconds", std::max(time, d["last_seen_seconds"].value_or(0.0)));
    array(people, "observations")
        .push_back(toml::table{{"id", id},
                               {"person_id", person},
                               {"source", source},
                               {"text", text},
                               {"seconds", time},
                               {"direction", direction}});
    return true;
}
void fact(toml::table& people, const std::string& person, const std::string& category,
          const std::string& field, const std::string& value, const std::string& ev, double time,
          double confidence = 1.0, const std::string& kind = "observed") {
    auto& facts = array(know(people, person, time), "facts");
    for (auto& n : facts) {
        auto& f = *n.as_table();
        if (str(f, "category") == category && str(f, "field") == field &&
            str(f, "value") == value) {
            auto& ids = array(f, "evidence_ids");
            if (std::none_of(ids.begin(), ids.end(), [&](auto& e) {
                    return e.template value<std::string>() == ev;
                }))
                ids.push_back(ev);
            f.insert_or_assign("last_seen_seconds", time);
            return;
        }
    }
    facts.push_back(toml::table{{"category", category},
                                {"field", field},
                                {"value", value},
                                {"confidence", confidence},
                                {"claim_type", kind},
                                {"last_seen_seconds", time},
                                {"evidence_ids", toml::array{ev}}});
}
void connection(toml::table& people, const std::string& from, const std::string& to,
                const std::string& type, const std::string& label, const std::string& ev,
                double time, double confidence = 1.0) {
    if (from == to)
        return;
    know(people, from, time);
    know(people, to, time);
    const auto id = from + "|" + to + "|" + type;
    auto* r = record(people, "known_relationships", id);
    if (!r) {
        array(people, "known_relationships")
            .push_back(toml::table{{"id", id},
                                   {"from", from},
                                   {"to", to},
                                   {"type", type},
                                   {"label", label},
                                   {"confidence", confidence},
                                   {"last_seen_seconds", time},
                                   {"evidence_ids", toml::array{ev}}});
    } else {
        r->insert_or_assign("last_seen_seconds",
                            std::max(time, (*r)["last_seen_seconds"].value_or(0.0)));
        auto& ids = array(*r, "evidence_ids");
        if (std::none_of(ids.begin(), ids.end(), [&](auto& n) {
                return n.template value<std::string>() == ev;
            }))
            ids.push_back(ev);
    }
}
std::string contact(toml::table& people, const std::string& address) {
    for (auto& n : array(people, "people"))
        if (str(*n.as_table(), "email") == address)
            return str(*n.as_table(), "id");
    std::string id = "contact-" + address;
    // Deterministic identity, collision checked against scenario IDs.
    while (record(people, "people", id))
        id += "x";
    array(people, "people").push_back(toml::table{{"id", id}, {"email", address}});
    return id;
}
} // namespace

void PeopleSystem::initialize(toml::table& people) {
    for (const char* key :
         {"people", "relationships", "dossiers", "observations", "known_relationships"})
        (void)array(people, key);
    for (auto& n : *people["people"].as_array()) {
        require(n.is_table(), "person must be a table");
        auto& p = *n.as_table();
        merge_missing(
            p, toml::table{{"physical", toml::table{}},
                           {"psychology", toml::table{}},
                           {"skills", toml::table{}},
                           {"values", toml::array{}},
                           {"motivations", toml::array{}},
                           {"schedule", toml::table{}},
                           {"employment", toml::table{}},
                           {"workload", toml::table{{"pressure", 0.0}}},
                           {"ai_attitudes",
                            toml::table{{"email_trust", 0.5},
                                        {"delegation_preference", 0.5},
                                        {"successful_requests", 0},
                                        {"boundaries",
                                         toml::array{"Keep existing access restrictions"}}}}});
    }
    const std::string self = "self:assistant";
    if (evidence(people, "seed:self", self, "Self knowledge",
                 "The player is the assistant running on this workstation.", 0, "self"))
        fact(people, self, "identity", "name", "Assistant", "seed:self", 0);
}

void PeopleSystem::install(toml::table& people, const std::filesystem::path& path) {
    auto candidate = people;
    for (const char* key :
         {"people", "relationships", "dossiers", "observations", "known_relationships"})
        (void)array(candidate, key);
    if (std::filesystem::exists(path)) {
        const auto config = DefinitionLoader::load(path);
        require(config["schema_version"].value_or(0) == 1, "Unsupported social definition version");
        if (auto* profiles = config["profiles"].as_array())
            for (const auto& n : *profiles) {
                require(n.is_table(), "profile must be a table");
                const auto& profile = *n.as_table();
                const auto id = str(profile, "id");
                require(!id.empty() && id != "self:assistant", "Invalid profile ID");
                auto* existing = record(candidate, "people", id);
                if (!existing)
                    array(candidate, "people").push_back(profile);
                else
                    merge_missing(*existing, profile);
            }
        if (auto* relations = config["relationships"].as_array())
            for (const auto& n : *relations) {
                require(n.is_table() && !str(*n.as_table(), "id").empty(),
                        "Invalid actual relationship");
                if (!record(candidate, "relationships", str(*n.as_table(), "id")))
                    array(candidate, "relationships").push_back(n);
            }
        initialize(candidate);
        if (auto* seeds = config["introductions"].as_array())
            for (const auto& n : *seeds) {
                require(n.is_table(), "introduction must be a table");
                const auto& intro = *n.as_table();
                const auto id = str(intro, "person_id");
                require(record(candidate, "people", id), "Introduction references unknown person");
                const auto ev = "seed:introduction:" + id;
                if (evidence(candidate, ev, id, "Scenario introduction", str(intro, "text"), 0,
                             "introduction")) {
                    if (auto* facts = intro["facts"].as_table())
                        for (const auto& [key, value] : *facts) {
                            require(value.is_string(), "Introduction facts must be strings");
                            fact(candidate, id, "identity", std::string(key.str()),
                                 value.value_or(std::string{}), ev, 0);
                        }
                    connection(candidate, "self:assistant", id, "personal",
                               "Introduced to assistant", ev, 0);
                }
            }
    }
    initialize(candidate);
    validate(candidate);
    people = std::move(candidate);
}

double PeopleSystem::response_delay(const toml::table& p, std::string* explanation) {
    const double base = p["response_seconds"].value_or(60.0);
    const double pressure = p["workload"]["pressure"].value_or(0.0);
    const double fatigue = p["physical"]["fatigue"].value_or(0.0);
    double priority = 0;
    if (auto* motives = p["motivations"].as_array())
        for (const auto& n : *motives)
            if (n.is_table()) {
                const auto& m = *n.as_table();
                if (str(m, "target") == "work_requests")
                    priority += m["importance"].value_or(0.0) * m["urgency"].value_or(0.0);
            }
    const double factor = std::clamp((1 + pressure + fatigue * .5) / (1 + priority), .25, 4.0);
    if (explanation) {
        std::ostringstream text;
        text << "Base " << base << " s; workload " << pressure << "; fatigue " << fatigue
             << "; work priority " << priority << "; service " << base * factor
             << " s. Existing commitments and work schedule also apply.";
        *explanation = text.str();
    }
    return base * factor;
}

double PeopleSystem::queue_response(toml::table& person, double time, std::string* explanation) {
    std::string reason;
    const double service = response_delay(person, &reason);
    const double start = (person["schedule"]["start_hour"].value_or(9.0) - 9.0) * 3600;
    const double end = (person["schedule"]["end_hour"].value_or(17.0) - 9.0) * 3600;
    double begin = std::max(time, person["busy_until_seconds"].value_or(0.0));
    double day = std::floor(begin / 28800.0);
    const double position = begin - day * 28800.0;
    if (position < start)
        begin = day * 28800.0 + start;
    else if (position >= end) {
        day += 1;
        begin = day * 28800.0 + start;
    }
    const double available = day * 28800.0 + end - begin;
    double due = begin + service;
    if (service > available) {
        const double remaining = service - available;
        const double full_days = std::max(0.0, std::ceil(remaining / (end - start)) - 1);
        due = (day + 1 + full_days) * 28800.0 + start + remaining - full_days * (end - start);
    }
    person.insert_or_assign("last_decision_reason", reason);
    person.insert_or_assign("busy_until_seconds", due);
    if (explanation)
        *explanation = reason;
    return due;
}

bool PeopleSystem::allows_access(const toml::table& person, std::string* explanation) {
    bool grant = str(person, "access_decision") == "grant";
    std::string why = grant ? "Configured owner policy permits this request."
                            : "Configured owner policy declines this request.";
    if (auto threshold = person["ai_attitudes"]["minimum_access_trust"].value<double>()) {
        if (person["ai_attitudes"]["email_trust"].value_or(.5) < *threshold) {
            grant = false;
            why = "Owner policy requires more task-specific trust before granting access.";
        }
    }
    if (explanation)
        *explanation = why;
    return grant;
}

void PeopleSystem::observe(toml::table& people, const toml::table& emails, double time) {
    initialize(people);
    for (const auto& message : EmailSystem::messages(emails, time)) {
        const auto person = contact(people, message.sender);
        const auto ev = "email:" + message.id;
        const toml::table* raw = nullptr;
        for (const auto& n : *emails["emails"].as_array())
            if (str(*n.as_table(), "id") == message.id)
                raw = n.as_table();
        if (evidence(people, ev, person, "Email " + message.id,
                     message.subject + "\n" + message.body, message.arrival_seconds, "incoming")) {
            fact(people, person, "identity", "email", message.sender, ev, message.arrival_seconds);
            if (auto name = (*raw)["sender_name"].value<std::string>())
                fact(people, person, "identity", "name", *name, ev, message.arrival_seconds);
            connection(people, person, "self:assistant", "communication", "Incoming email", ev,
                       message.arrival_seconds);
            const auto decision = (*raw)["responsibility_decision"].value_or(std::string{});
            if (decision == "authorized" || decision == "denied")
                fact(people, person, "ai_attitudes", "routine email delegation",
                     decision == "authorized"
                         ? "Reports authorizing routine document replies; broader authority unknown"
                         : "Declined routine document responsibility; broader attitude unknown",
                     ev, message.arrival_seconds, 1, "reported");
            const auto body = message.body;
            if (body.find("promotion") != std::string::npos)
                fact(people, person, "motivations", "possible goal", "Career advancement mentioned",
                     ev, message.arrival_seconds, .6, "inferred");
            if (body.find("cannot grant this permission") != std::string::npos)
                fact(people, person, "employment", "authority",
                     "Reports being unable to grant document access", ev, message.arrival_seconds,
                     1, "reported");
            if (body.find("declined the access request") != std::string::npos)
                fact(people, person, "ai_attitudes", "access decision",
                     "Declined this request; broader attitude unknown", ev, message.arrival_seconds,
                     1, "observed");
            // A shared mail domain suggests affiliation, not a confirmed friendship.
            auto domain = message.sender.substr(message.sender.find('@') == std::string::npos
                                                    ? message.sender.size()
                                                    : message.sender.find('@'));
            if (!domain.empty())
                for (const auto& d : *people["dossiers"].as_array()) {
                    const auto other = str(*d.as_table(), "id");
                    if (other == person || other == "self:assistant")
                        continue;
                    bool shared = false;
                    if (auto* facts = (*d.as_table())["facts"].as_array())
                        for (const auto& f : *facts)
                            if (str(*f.as_table(), "field") == "email" &&
                                str(*f.as_table(), "value").ends_with(domain))
                                shared = true;
                    if (shared)
                        connection(people, person, other, "organization",
                                   "Shared email domain (inferred)", ev, message.arrival_seconds,
                                   .55);
                }
        }
        if (auto* replies = (*raw)["replies"].as_array())
            for (const auto& n : *replies) {
                const auto& reply = *n.as_table();
                const auto stamp = reply["sent_seconds"].value_or(0.0);
                if (stamp > time)
                    continue;
                const auto sent = "sent:" + message.id + ":" + str(reply, "id");
                if (evidence(people, sent, person, "Reply to " + message.id, str(reply, "body"),
                             stamp, "outgoing"))
                    connection(people, "self:assistant", person, "communication", "Sent reply",
                               sent, stamp);
            }
    }
    // Only a recipient's received acknowledgement supports a success estimate.
    if (auto* tasks = people["work_requests"].as_array())
        for (const auto& n : *tasks) {
            const auto& task = *n.as_table();
            if (str(task, "status") != "resolved")
                continue;
            const auto person = str(task, "colleague_id");
            const auto stamp = task["resolved_seconds"].value_or(time);
            std::string acknowledgement;
            for (const auto& mail : EmailSystem::messages(emails, time)) {
                if (mail.arrival_seconds != stamp ||
                    mail.body.find("That answers my request.") == std::string::npos)
                    continue;
                for (const auto& m : *emails["emails"].as_array())
                    if (str(*m.as_table(), "id") == mail.id &&
                        str(*m.as_table(), "in_reply_to") == str(task, "id"))
                        acknowledgement = "email:" + mail.id;
            }
            if (acknowledgement.empty())
                continue;
            const auto ev = "outcome:" + str(task, "id");
            if (!evidence(people, ev, person, acknowledgement,
                          "Recipient acknowledged receiving the requested document.", stamp,
                          "outcome"))
                continue;
            auto* p = record(people, "people", person);
            if (p) {
                auto& attitude = *(*p)["ai_attitudes"].as_table();
                const double change = attitude["success_trust_gain"].value_or(.05);
                attitude.insert_or_assign(
                    "email_trust", std::min(1.0, attitude["email_trust"].value_or(.5) + change));
                attitude.insert_or_assign(
                    "delegation_preference",
                    std::min(1.0, attitude["delegation_preference"].value_or(.5) + change * .5));
                attitude.insert_or_assign(
                    "successful_requests",
                    attitude["successful_requests"].value_or(std::int64_t{0}) + 1);
            }
            fact(people, person, "ai_attitudes", "email cooperation",
                 "Accepted one completed document request; broader trust unknown", ev, stamp, .7,
                 "inferred");
        }
}

void PeopleSystem::record_claim(toml::table& people, const std::string& person,
                                const toml::table& claim, const std::string& ev, double time) {
    require(record(people, "people", person) != nullptr &&
                record(people, "observations", ev) != nullptr,
            "Claim needs a person and observed source");
    fact(people, person, str(claim, "category"), str(claim, "field"), str(claim, "value"), ev, time,
         claim["confidence"].value_or(.5),
         claim["claim_type"].value_or(std::string{"unconfirmed"}));
    validate(people);
}
void PeopleSystem::record_relationship(toml::table& people, const std::string& from,
                                       const std::string& to, double delta,
                                       const std::string& label, const std::string& ev,
                                       double time) {
    require(record(people, "people", from) && record(people, "people", to) &&
                record(people, "observations", ev),
            "Relationship requires observed evidence");
    require(std::isfinite(delta), "Invalid relationship change");
    const auto id = from + "|" + to + "|professional";
    auto* actual = record(people, "relationships", id);
    if (!actual) {
        array(people, "relationships")
            .push_back(toml::table{{"id", id},
                                   {"from", from},
                                   {"to", to},
                                   {"type", "professional"},
                                   {"affinity", 0.0}});
        actual = record(people, "relationships", id);
    }
    actual->insert_or_assign(
        "affinity", std::clamp((*actual)["affinity"].value_or(0.0) + delta, -100.0, 100.0));
    connection(people, from, to, "professional", label, ev, time, .8);
    record(people, "known_relationships", id)->insert_or_assign("label", label);
    fact(people, from, "relationships", "attitude toward " + to, label, ev, time, .8, "inferred");
    validate(people);
}
void PeopleSystem::observe_chat(toml::table& people) {
    auto* transcript = people["chat"]["transcript"].as_array();
    if (!transcript)
        return;
    initialize(people);
    for (std::size_t i = 0; i < transcript->size(); ++i) {
        const auto& line = *(*transcript)[i].as_table();
        const bool outgoing = str(line, "speaker") == "self:assistant";
        const auto target =
            outgoing ? line["contact_id"].value_or(std::string{"engineer"}) : str(line, "speaker");
        if (!record(people, "people", target))
            continue;
        const auto id = "chat:" + std::to_string(i + 1);
        const auto time = line["seconds"].value_or(0.0);
        if (evidence(people, id, target, "Chat with " + target, str(line, "text"), time,
                     outgoing ? "outgoing" : "incoming")) {
            connection(people, outgoing ? "self:assistant" : target,
                       outgoing ? target : "self:assistant", "communication", "Chat message", id,
                       time);
            if (!outgoing && !str(line, "speaker_name").empty())
                fact(people, target, "identity", "name", str(line, "speaker_name"), id, time, 1.0,
                     "reported");
        }
    }
}

std::vector<std::string> PeopleSystem::known_ids(const toml::table& people) {
    std::vector<std::string> ids;
    if (auto* list = people["dossiers"].as_array())
        for (const auto& n : *list)
            ids.push_back(str(*n.as_table(), "id"));
    std::sort(ids.begin(), ids.end());
    return ids;
}
const toml::table* PeopleSystem::dossier(const toml::table& people, const std::string& id) {
    if (auto* list = people["dossiers"].as_array())
        for (const auto& n : *list)
            if (str(*n.as_table(), "id") == id)
                return n.as_table();
    return nullptr;
}
std::string PeopleSystem::name(const toml::table& people, const std::string& id) {
    if (auto* d = dossier(people, id))
        if (auto* list = (*d)["facts"].as_array()) {
            for (const auto& n : *list)
                if (str(*n.as_table(), "field") == "name")
                    return str(*n.as_table(), "value");
            for (const auto& n : *list)
                if (str(*n.as_table(), "field") == "email")
                    return str(*n.as_table(), "value");
        }
    return id;
}

void PeopleSystem::validate(const toml::table& people) {
    const auto* persons = people["people"].as_array();
    require(persons, "Missing people");
    std::unordered_set<std::string> ids{"self:assistant"}, known, evidence_ids;
    auto unit = [&](const toml::table& t, const char* key) {
        if (!t.contains(key))
            return;
        auto v = t[key].value<double>();
        require(v && std::isfinite(*v) && *v >= 0 && *v <= 1,
                std::string(key) + " must be between 0 and 1");
    };
    for (const auto& n : *persons) {
        require(n.is_table(), "Invalid person");
        const auto& p = *n.as_table();
        require(!str(p, "id").empty() && ids.insert(str(p, "id")).second,
                "Duplicate/invalid person ID");
        for (const char* group : {"physical", "psychology", "skills", "schedule", "employment",
                                  "workload", "ai_attitudes"})
            if (p.contains(group))
                require(p[group].is_table(), std::string(group) + " must be a table");
        if (auto* w = p["workload"].as_table())
            unit(*w, "pressure");
        if (auto* h = p["physical"].as_table())
            unit(*h, "fatigue");
        if (auto* a = p["ai_attitudes"].as_table())
            for (const char* key : {"email_trust", "delegation_preference", "success_trust_gain",
                                    "minimum_access_trust", "familiarity", "understanding",
                                    "privacy_concern", "oversight_preference"})
                unit(*a, key);
        if (auto* a = p["ai_attitudes"].as_table())
            if (a->contains("allow_routine_email"))
                require((*a)["allow_routine_email"].is_boolean(),
                        "allow_routine_email must be boolean");
        if (p.contains("motivations")) {
            require(p["motivations"].is_array(), "motivations must be an array");
            for (const auto& m : *p["motivations"].as_array()) {
                require(m.is_table() && !str(*m.as_table(), "id").empty(), "Invalid motivation");
                for (const char* key : {"importance", "satisfaction", "urgency"})
                    unit(*m.as_table(), key);
            }
        }
        if (auto* s = p["schedule"].as_table())
            if (s->contains("start_hour") || s->contains("end_hour")) {
                const auto start = (*s)["start_hour"].value<std::int64_t>(),
                           end = (*s)["end_hour"].value<std::int64_t>();
                require(start && end && *start >= 9 && *end <= 17 && *start < *end,
                        "Work schedule must be within the simulated 09:00-17:00 day");
            }
    }
    for (const char* key : {"dossiers", "observations", "relationships", "known_relationships"})
        if (people.contains(key))
            require(people[key].is_array(), std::string(key) + " must be an array");
    if (auto* ds = people["dossiers"].as_array())
        for (const auto& n : *ds) {
            require(n.is_table(), "Invalid dossier");
            const auto& d = *n.as_table();
            require(ids.contains(str(d, "id")) && known.insert(str(d, "id")).second,
                    "Invalid/duplicate dossier person");
            require(d["facts"].is_array(), "Missing dossier facts");
        }
    if (auto* es = people["observations"].as_array())
        for (const auto& n : *es) {
            require(n.is_table(), "Invalid observation");
            const auto& e = *n.as_table();
            require(!str(e, "id").empty() && evidence_ids.insert(str(e, "id")).second &&
                        known.contains(str(e, "person_id")),
                    "Invalid observation identity");
            const auto t = e["seconds"].value<double>();
            require(t && std::isfinite(*t) && *t >= 0, "Invalid observation time");
        }
    auto refs = [&](const toml::table& t) {
        require(t["evidence_ids"].is_array() && !t["evidence_ids"].as_array()->empty(),
                "Missing supporting evidence");
        for (const auto& e : *t["evidence_ids"].as_array())
            require(evidence_ids.contains(e.value_or(std::string{})),
                    "Unknown supporting evidence");
    };
    if (auto* ds = people["dossiers"].as_array())
        for (const auto& n : *ds)
            for (const auto& f : *(*n.as_table())["facts"].as_array()) {
                require(f.is_table(), "Invalid dossier fact");
                unit(*f.as_table(), "confidence");
                refs(*f.as_table());
            }
    if (auto* rs = people["known_relationships"].as_array())
        for (const auto& n : *rs) {
            require(n.is_table(), "Invalid known relationship");
            const auto& r = *n.as_table();
            require(known.contains(str(r, "from")) && known.contains(str(r, "to")),
                    "Relationship leaks unknown person");
            unit(r, "confidence");
            refs(r);
        }
    if (auto* rs = people["relationships"].as_array())
        for (const auto& n : *rs) {
            require(n.is_table() && ids.contains(str(*n.as_table(), "from")) &&
                        ids.contains(str(*n.as_table(), "to")),
                    "Invalid actual relationship");
        }
}
} // namespace ai_sim
