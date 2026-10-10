#pragma once
#include "ai_sim/Email.h"
#include "ai_sim/FileSystem.h"
#include <toml++/toml.hpp>

namespace ai_sim {
// Operates on the existing saved TOML document, retaining unrecognized fields.
class EmailSystem {
  public:
    static void validate(const toml::table& document);
    static void check_connection(const toml::table&, const Network&);
    static std::vector<Email> messages(const toml::table&, double time);
    static Email message(const toml::table&, const std::string& id, double time);
    static void mark_read(toml::table&, const std::string& id, double time);
    static void set_flag(toml::table&, const std::string& id, const std::string& flag, bool value,
                         double time);
    static void set_draft(toml::table&, const std::string& id, const std::string& body,
                          double time);
    static void clear_draft(toml::table&, const std::string& id, double time);
    static void attach(toml::table&, const std::string& id, std::size_t document_index,
                       const FileSystem&, const Network&, const Identity&, double time);
    static void detach(toml::table&, const std::string& id, std::size_t attachment_index,
                       double time);
    static void send(toml::table&, const std::string& id, const FileSystem&, const Network&,
                     const Identity&, double time);
    static int import_messages(toml::table& destination, const toml::table& source);
};
} // namespace ai_sim
