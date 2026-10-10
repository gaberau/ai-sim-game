#pragma once
#include "ai_sim/Network.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ai_sim {
struct Identity {
    std::string user;
    std::vector<std::string> groups;
    Identity(std::string name, std::vector<std::string> memberships = {})
        : user(std::move(name)), groups(std::move(memberships)) {}
    Identity(const char* name) : Identity(std::string(name)) {}
};
struct FileEntry {
    std::string id;
    std::string host_id;
    std::string path;
    std::string owner;
    std::string group;
    unsigned mode = 0600;
    bool directory = false;
    std::string content;
    std::string category; // work, personal, hidden; empty uses legacy path inference
    std::uint64_t size_bytes = 0;
    double modified_at_seconds = 0.0;
};
class FileSystem {
public:
    static FileSystem load(const std::filesystem::path& path, const Network& network);
    void save_snapshot(const std::filesystem::path& path) const;
    static std::string normalize(const std::string& path);
    static std::string parent(const std::string& path);
    static unsigned parse_mode(const std::string& text);
    static std::string mode_text(unsigned mode);
    static std::string permissions(unsigned mode);
    static std::string category_of(const FileEntry&);
    std::vector<const FileEntry*> category_files(const Network&, const Identity&, const std::string&) const;
    void set_category(const Network&, const Identity&, const std::string& host, const std::string& path, const std::string& category);
    std::uint64_t storage_used(const std::string& host) const;
    std::vector<const FileEntry*> list(const Network&, const Identity&,
        const std::string& host, const std::string& directory) const;
    const FileEntry& inspect(const Network&, const Identity&,
        const std::string& host, const std::string& path) const;
    const std::string& read(const Network&, const Identity&,
        const std::string& host, const std::string& path) const;
    void enter(const Network&, const Identity&,
        const std::string& host, const std::string& directory) const;
    void validate_write(const Network&, const Identity&,
        const std::string& host, const std::string& path) const;
    void write(const Network&, const Identity&, const std::string& host,
        const std::string& path, const std::string& content, double time);
    void mkdir(const Network&, const Identity&, const std::string& host,
        const std::string& path, double time);
    void remove(const Network&, const Identity&, const std::string& host,
        const std::string& path, double time);
    void rename(const Network&, const Identity&, const std::string& host,
        const std::string& source, const std::string& destination, double time);
    void copy(const Network&, const Identity&, const std::string& host,
        const std::string& source, const std::string& destination, double time);
    void chmod(const Network&, const Identity&, const std::string& host,
        const std::string& path, unsigned mode, double time);
    // Scenario import only: never replaces an existing entry.
    bool import_file(const Network&, const FileEntry&, double time);
private:
    std::vector<FileEntry> entries_;
    unsigned default_file_mode_ = 0600;
    unsigned default_directory_mode_ = 0700;
    std::uint64_t next_id_ = 1;
    const FileEntry* lookup(const std::string& host, const std::string& path) const;
    const FileEntry& find(const std::string& host, const std::string& path) const;
    void check_host(const Network&, const std::string& host) const;
    void traverse(const Identity&, const std::string& host, const std::string& path) const;
    void check_parent_write(const Network&, const Identity&,
        const std::string& host, const std::string& path) const;
    void create(const Identity&, const std::string& host, const std::string& path,
        bool directory, const std::string& content, double time);
};
}
