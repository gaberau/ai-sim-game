// Module: Mail transport/request records and drafts persisted with the world.
#pragma once
#include <string>
#include <vector>

namespace ai_sim {
struct EmailDocument {
    std::string host_id, path, label;
};
struct Email {
    std::string id, sender, recipient, subject, body, draft;
    double arrival_seconds = 0.0;
    bool read = false;
    bool starred = false;
    bool archived = false;
    std::vector<EmailDocument> documents;
    std::size_t attachment_count = 0;
    std::size_t reply_count = 0;
};
} // namespace ai_sim
