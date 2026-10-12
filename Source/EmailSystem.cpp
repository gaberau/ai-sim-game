// Module: Mailbox access, message flags, drafts, attachments and validated delivery.
#include "ai_sim/EmailSystem.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace ai_sim {
namespace {
void require(bool ok, const std::string& message) {
    if (!ok)
        throw std::runtime_error("Email: " + message);
}
std::string string_value(const toml::table& table, const char* key,
                         const std::string& fallback = "") {
    if (!table.contains(key))
        return fallback;
    const auto value = table[key].value<std::string>();
    require(value.has_value(), std::string(key) + " must be a string");
    return *value;
}
double seconds(const toml::table& table, const char* key) {
    if (!table.contains(key))
        return 0.0;
    const auto value = table[key].value<double>();
    require(value && std::isfinite(*value) && *value >= 0.0,
            std::string(key) + " must be nonnegative seconds");
    return *value;
}
const toml::array* array_value(const toml::table& table, const char* key) {
    if (!table.contains(key))
        return nullptr;
    const auto* value = table[key].as_array();
    require(value != nullptr, std::string(key) + " must be an array");
    return value;
}
std::size_t limit(const toml::table& document) {
    if (document.contains("mailbox"))
        require(document["mailbox"].is_table(), "mailbox must be a table");
    if (!document["mailbox"]["max_draft_bytes"])
        return 1048576;
    const auto value = document["mailbox"]["max_draft_bytes"].value<std::int64_t>();
    require(value && *value > 0 && *value <= 67108864,
            "max_draft_bytes must be between 1 and 67108864");
    return static_cast<std::size_t>(*value);
}
void validate_document(const toml::table& table, bool attachment) {
    require(!string_value(table, "host_id").empty(), "Document host_id is required");
    const auto path = string_value(table, "path");
    require(!path.empty() && path.front() == '/' && FileSystem::normalize(path) == path,
            "Document path must be a normalized absolute path");
    (void)string_value(table, "label");
    if (attachment) {
        require(!string_value(table, "file_id").empty(), "Attachment file_id is required");
        require(table.contains("content"), "Attachment content is required");
        (void)string_value(table, "content");
    }
}
void validate_documents(const toml::table& table, const char* key, bool attachments) {
    if (const auto* values = array_value(table, key)) {
        for (const auto& node : *values) {
            require(node.is_table(), "Document entries must be tables");
            validate_document(*node.as_table(), attachments);
        }
    }
}
std::size_t payload_size(const toml::table& table, const char* body_key,
                         const char* attachment_key) {
    auto bytes = string_value(table, body_key).size();
    if (const auto* attachments = array_value(table, attachment_key))
        for (const auto& node : *attachments)
            bytes += string_value(*node.as_table(), "content").size();
    return bytes;
}
Email decode(const toml::table& table) {
    Email email;
    email.id = string_value(table, "id");
    email.sender = string_value(table, "sender", "Unknown sender");
    email.recipient = string_value(table, "recipient", "Engineer");
    email.subject = string_value(table, "subject", "(No subject)");
    email.body = string_value(table, "body");
    email.draft = string_value(table, "draft");
    email.arrival_seconds = seconds(table, "arrival_seconds");
    email.read = table["read"].value_or(false);
    email.starred = table["starred"].value_or(false);
    email.archived = table["archived"].value_or(false);
    if (const auto* values = array_value(table, "documents"))
        for (const auto& node : *values) {
            const auto& reference = *node.as_table();
            email.documents.push_back({string_value(reference, "host_id"),
                                       string_value(reference, "path"),
                                       string_value(reference, "label")});
        }
    if (const auto* values = array_value(table, "draft_attachments"))
        email.attachment_count = values->size();
    if (const auto* values = array_value(table, "replies"))
        email.reply_count = values->size();
    return email;
}
toml::table& record(toml::table& document, const std::string& id, double time) {
    require(std::isfinite(time) && time >= 0.0, "Invalid simulation clock");
    auto* values = document["emails"].as_array();
    require(values != nullptr, "Missing emails array");
    for (auto& node : *values) {
        auto& table = *node.as_table();
        if (string_value(table, "id") == id) {
            require(seconds(table, "arrival_seconds") <= time, "Message has not arrived yet");
            return table;
        }
    }
    throw std::runtime_error("Email: Unknown message: " + id);
}
void check_mailbox(const toml::table& document, const Network& network) {
    std::string host = network.hosts()[network.local_host_index()].id;
    if (const auto* table = document["mailbox"].as_table())
        host = string_value(*table, "host_id", host);
    const auto& hosts = network.hosts();
    const auto found = std::find_if(hosts.begin(), hosts.end(), [&](const auto& candidate) {
        return candidate.id == host;
    });
    require(found != hosts.end() && found->known, "Unknown mailbox host: " + host);
    require(network.reachable(static_cast<std::size_t>(found - hosts.begin())),
            "Mailbox host is offline or unreachable");
    require(found->access_granted, "No credentials for mailbox host");
}
} // namespace

void EmailSystem::check_connection(const toml::table& document, const Network& network) {
    check_mailbox(document, network);
}

void EmailSystem::validate(const toml::table& document) {
    require(document["schema_version"].value<int>() == 1, "Unsupported schema_version");
    const auto* values = document["emails"].as_array();
    require(values != nullptr, "Missing emails array");
    const auto max_bytes = limit(document);
    if (const auto* mailbox = document["mailbox"].as_table()) {
        if (mailbox->contains("host_id"))
            require(!string_value(*mailbox, "host_id").empty(), "Empty mailbox host_id");
        (void)string_value(*mailbox, "address");
    }
    std::unordered_set<std::string> ids;
    for (const auto& node : *values) {
        require(node.is_table(), "Message must be a table");
        const auto& table = *node.as_table();
        const auto id = string_value(table, "id");
        require(!id.empty() && ids.insert(id).second, "Empty or duplicate message ID");
        for (const auto* key : {"sender", "recipient", "subject", "body", "draft"})
            (void)string_value(table, key);
        (void)seconds(table, "arrival_seconds");
        for (const auto* flag : {"read", "starred", "archived"})
            if (table.contains(flag))
                require(table[flag].is_boolean(), std::string(flag) + " must be a bool");
        validate_documents(table, "documents", false);
        validate_documents(table, "draft_attachments", true);
        require(payload_size(table, "draft", "draft_attachments") <= max_bytes,
                "Draft exceeds mailbox byte limit");
        if (const auto* replies = array_value(table, "replies")) {
            std::unordered_set<std::string> reply_ids;
            for (const auto& reply : *replies) {
                require(reply.is_table(), "Reply must be a table");
                const auto& sent = *reply.as_table();
                const auto sent_id = string_value(sent, "id");
                require(!sent_id.empty() && reply_ids.insert(sent_id).second,
                        "Empty or duplicate reply ID");
                (void)string_value(sent, "body");
                (void)string_value(sent, "sender");
                (void)string_value(sent, "recipient");
                (void)seconds(sent, "sent_seconds");
                validate_documents(sent, "attachments", true);
                require(payload_size(sent, "body", "attachments") <= max_bytes,
                        "Sent reply exceeds mailbox byte limit");
            }
        }
    }
}

std::vector<Email> EmailSystem::messages(const toml::table& document, double time) {
    std::vector<Email> result;
    for (const auto& node : *document["emails"].as_array()) {
        const auto& table = *node.as_table();
        if (seconds(table, "arrival_seconds") <= time)
            result.push_back(decode(table));
    }
    std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.arrival_seconds < b.arrival_seconds;
    });
    return result;
}
Email EmailSystem::message(const toml::table& document, const std::string& id, double time) {
    // A single-message lookup does not need to decode and sort the mailbox.
    for (const auto& node : *document["emails"].as_array()) {
        const auto& table = *node.as_table();
        if (string_value(table, "id") == id && seconds(table, "arrival_seconds") <= time)
            return decode(table);
    }
    throw std::runtime_error("Email: Unknown or not-yet-arrived message: " + id);
}
void EmailSystem::mark_read(toml::table& document, const std::string& id, double time) {
    record(document, id, time).insert_or_assign("read", true);
}
void EmailSystem::set_flag(toml::table& document, const std::string& id, const std::string& flag,
                           bool value, double time) {
    require(flag == "read" || flag == "starred" || flag == "archived", "Unknown message flag");
    record(document, id, time).insert_or_assign(flag, value);
}
void EmailSystem::set_draft(toml::table& document, const std::string& id, const std::string& body,
                            double time) {
    auto& table = record(document, id, time);
    auto candidate = table;
    candidate.insert_or_assign("draft", body);
    require(payload_size(candidate, "draft", "draft_attachments") <= limit(document),
            "Draft exceeds mailbox byte limit");
    table.insert_or_assign("draft", body);
}
void EmailSystem::clear_draft(toml::table& document, const std::string& id, double time) {
    auto& table = record(document, id, time);
    table.insert_or_assign("draft", "");
    table.insert_or_assign("draft_attachments", toml::array{});
}
void EmailSystem::attach(toml::table& document, const std::string& id, std::size_t index,
                         const FileSystem& files, const Network& network, const Identity& actor,
                         double time) {
    auto& table = record(document, id, time);
    const auto* references = array_value(table, "documents");
    require(references && index < references->size(), "Invalid source document number");
    const auto& reference = *(*references)[index].as_table();
    const auto host = string_value(reference, "host_id"), path = string_value(reference, "path");
    const auto& content = files.read(network, actor, host, path);
    const auto& entry = files.inspect(network, actor, host, path);
    auto attachments = table["draft_attachments"].as_array()
                           ? *table["draft_attachments"].as_array()
                           : toml::array{};
    for (const auto& existing : attachments)
        require(string_value(*existing.as_table(), "file_id") != entry.id,
                "Document is already attached; detach it before refreshing");
    attachments.push_back(toml::table{{"host_id", host},
                                      {"path", path},
                                      {"file_id", entry.id},
                                      {"label", string_value(reference, "label")},
                                      {"content", content}});
    auto candidate = table;
    candidate.insert_or_assign("draft_attachments", attachments);
    require(payload_size(candidate, "draft", "draft_attachments") <= limit(document),
            "Attachments exceed mailbox byte limit");
    table.insert_or_assign("draft_attachments", std::move(attachments));
}
void EmailSystem::detach(toml::table& document, const std::string& id, std::size_t index,
                         double time) {
    auto& table = record(document, id, time);
    auto* attachments = table["draft_attachments"].as_array();
    require(attachments && index < attachments->size(), "Invalid attachment number");
    attachments->erase(attachments->begin() + static_cast<std::ptrdiff_t>(index));
}
void EmailSystem::send(toml::table& document, const std::string& id, const FileSystem& files,
                       const Network& network, const Identity& actor, double time) {
    auto& table = record(document, id, time);
    const auto body = string_value(table, "draft");
    const auto attachments = table["draft_attachments"].as_array()
                                 ? *table["draft_attachments"].as_array()
                                 : toml::array{};
    require(body.find_first_not_of(" \t\r\n") != std::string::npos || !attachments.empty(),
            "Reply draft is empty");
    check_mailbox(document, network);
    for (const auto& node : attachments) {
        const auto& attachment = *node.as_table();
        const auto host = string_value(attachment, "host_id"),
                   path = string_value(attachment, "path");
        const auto& content = files.read(network, actor, host, path);
        const auto& entry = files.inspect(network, actor, host, path);
        require(entry.id == string_value(attachment, "file_id") &&
                    content == string_value(attachment, "content"),
                "Attached document changed or was replaced; detach and attach it again");
    }
    require(payload_size(table, "draft", "draft_attachments") <= limit(document),
            "Reply exceeds mailbox byte limit");
    auto replies = table["replies"].as_array() ? *table["replies"].as_array() : toml::array{};
    std::size_t serial = replies.size() + 1;
    std::string reply_id;
    do {
        reply_id = id + ":reply:" + std::to_string(serial++);
    } while (std::any_of(replies.begin(), replies.end(), [&](const auto& node) {
        return string_value(*node.as_table(), "id") == reply_id;
    }));
    std::string sender = actor.user;
    if (const auto* mailbox = document["mailbox"].as_table())
        sender = string_value(*mailbox, "address", sender);
    replies.push_back(toml::table{{"id", reply_id},
                                  {"body", body},
                                  {"sent_seconds", time},
                                  {"sender", sender},
                                  {"recipient", string_value(table, "sender", "Unknown sender")},
                                  {"attachments", attachments}});
    // All prerequisites passed before any sent/draft state is changed.
    table.insert_or_assign("replies", std::move(replies));
    table.insert_or_assign("draft", "");
    table.insert_or_assign("draft_attachments", toml::array{});
    table.insert_or_assign("read", true);
}
int EmailSystem::import_messages(toml::table& destination, const toml::table& source) {
    validate(destination);
    validate(source);
    auto candidate = destination;
    auto* target = candidate["emails"].as_array();
    std::unordered_set<std::string> ids;
    for (const auto& node : *target)
        ids.insert(string_value(*node.as_table(), "id"));
    int imported = 0;
    for (const auto& node : *source["emails"].as_array())
        if (ids.insert(string_value(*node.as_table(), "id")).second) {
            target->push_back(node);
            ++imported;
        }
    if (!candidate.contains("mailbox") && source.contains("mailbox"))
        candidate.insert("mailbox", *source["mailbox"].as_table());
    validate(candidate);
    destination = std::move(candidate);
    return imported;
}
} // namespace ai_sim
