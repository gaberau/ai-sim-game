// Module: Mailbox access, message flags, drafts, attachments and validated delivery.
#pragma once
#include "ai_sim/Email.h"
#include "ai_sim/FileSystem.h"
#include <toml++/toml.hpp>

namespace ai_sim {
// Operates on the existing saved TOML document, retaining unrecognized fields.
class EmailSystem {
  public:
    // Reject invalid subsystem state before it is used or restored.
    static void validate(const toml::table& document);
    // Validate the mailbox's host/network access before transport actions.
    static void check_connection(const toml::table&, const Network&);
    // Return visible mailbox records at the supplied game time.
    static std::vector<Email> messages(const toml::table&, double time);
    // Resolve one eligible mailbox record by stable ID.
    static Email message(const toml::table&, const std::string& id, double time);
    // Save the transcript boundary the player has seen for this contact.
    static void mark_read(toml::table&, const std::string& id, double time);
    // Persist a supported message flag after resolving the message.
    static void set_flag(toml::table&, const std::string& id, const std::string& flag, bool value,
                         double time);
    // Save a working reply draft within the mailbox's configured size limit.
    static void set_draft(toml::table&, const std::string& id, const std::string& body,
                          double time);
    // Remove the selected saved draft without deleting its original message.
    static void clear_draft(toml::table&, const std::string& id, double time);
    // Add a validated file reference to a reply draft.
    static void attach(toml::table&, const std::string& id, std::size_t document_index,
                       const FileSystem&, const Network&, const Identity&, double time);
    // Remove a file reference from a reply draft.
    static void detach(toml::table&, const std::string& id, std::size_t attachment_index,
                       double time);
    // Validate and deliver the outgoing body to a known contact.
    static void send(toml::table&, const std::string& id, const FileSystem&, const Network&,
                     const Identity&, double time);
    // Merge missing authored messages without overwriting existing mailbox state.
    static int import_messages(toml::table& destination, const toml::table& source);
};
} // namespace ai_sim
