// Module: Host-scoped files, Unix-style access checks, workspace storage and managed allocations.
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
    // Initialize this object from the supplied configuration/identity.
    Identity(std::string name, std::vector<std::string> memberships = {})
        : user(std::move(name)), groups(std::move(memberships)) {
    }
    // Initialize this object from the supplied configuration/identity.
    Identity(const char* name) : Identity(std::string(name)) {
    }
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
    std::string category;     // work, personal, hidden; empty uses legacy path inference
    std::string service_kind; // agent, backup, or work; managed through the associated tool
    std::uint64_t size_bytes = 0;
    double modified_at_seconds = 0.0;
};
class FileSystem {
  public:
    // Read authored TOML definitions and reject malformed values.
    static FileSystem load(const std::filesystem::path& path, const Network& network);
    // Serialize current state for a save snapshot; does not publish a save-slot manifest.
    void save_snapshot(const std::filesystem::path& path) const;
    // Canonicalize an absolute in-game path, rejecting traversal above its root.
    static std::string normalize(const std::string& path);
    // Return the canonical parent of an in-game path.
    static std::string parent(const std::string& path);
    // Decode an authored octal permission string.
    static unsigned parse_mode(const std::string& text);
    // Format the permission bits as an octal mode.
    static std::string mode_text(unsigned mode);
    // Format the Unix-style readable permission flags.
    static std::string permissions(unsigned mode);
    // Return an explicit category or infer it from supported legacy paths.
    static std::string category_of(const FileEntry&);
    // Find accessible files belonging to the requested category.
    std::vector<const FileEntry*> category_files(const Network&, const Identity&,
                                                 const std::string&) const;
    // Change an owned entry's category after validating access.
    void set_category(const Network&, const Identity&, const std::string& host,
                      const std::string& path, const std::string& category);
    // Return installed file/allocation bytes on the selected host.
    std::uint64_t storage_used(const std::string& host) const;
    // List visible directory entries after checking connectivity and traversal permissions.
    std::vector<const FileEntry*> list(const Network&, const Identity&, const std::string& host,
                                       const std::string& directory) const;
    // Read entry metadata after validating access to its host/path.
    const FileEntry& inspect(const Network&, const Identity&, const std::string& host,
                             const std::string& path) const;
    // Read file content after validating reachability and read permissions.
    const std::string& read(const Network&, const Identity&, const std::string& host,
                            const std::string& path) const;
    // Validate that the selected directory can be traversed by the supplied identity.
    void enter(const Network&, const Identity&, const std::string& host,
               const std::string& directory) const;
    // Check write access without mutating the filesystem.
    void validate_write(const Network&, const Identity&, const std::string& host,
                        const std::string& path) const;
    // Create/update unmanaged content after access and capacity checks; time is in game seconds.
    void write(const Network&, const Identity&, const std::string& host, const std::string& path,
               const std::string& content, double time);
    // Create an accessible directory using the configured default mode.
    void mkdir(const Network&, const Identity&, const std::string& host, const std::string& path,
               double time);
    // Delete an unmanaged entry only after validating parent access.
    void remove(const Network&, const Identity&, const std::string& host, const std::string& path,
                double time);
    // Move an unmanaged entry to a validated destination on the selected host.
    void rename(const Network&, const Identity&, const std::string& host, const std::string& source,
                const std::string& destination, double time);
    // Copy an unmanaged entry after validating source, destination and storage capacity.
    void copy(const Network&, const Identity&, const std::string& host, const std::string& source,
              const std::string& destination, double time);
    // Change permissions only when the supplied identity owns or administers the entry.
    void chmod(const Network&, const Identity&, const std::string& host, const std::string& path,
               unsigned mode, double time);
    // Scenario import only: never replaces an existing entry.
    void reserve_file(const Network&, const Identity&, const std::string& host,
                      const std::string& path, std::uint64_t bytes, double time);
    // Import one missing authored entry; return whether an entry was added.
    bool import_file(const Network&, const FileEntry&, double time);
    // Initialize filesystem roots for an accessible host.
    void mount_host(const Network&, const std::string& host, const std::string& owner, double time);
    // Return the canonical saved workspace root.
    const std::string& workspace_root() const {
        return workspace_root_;
    }
    // Resolve a workspace subfolder below the canonical root.
    std::string workspace_path(const std::string& folder) const;
    // Resolve the managed allocation path for an installed agent.
    std::string agent_path(const std::string& id) const;
    // Resolve the managed allocation path used for backup storage.
    std::string backup_path() const;
    // Create missing workspace folders without replacing player content.
    void ensure_workspace(const Network&, const Identity&, const std::string& host, double time);
    // Reserve storage controlled by an agent/work/backup lifecycle.
    void reserve_service_file(const Network&, const Identity&, const std::string& host,
                              const std::string& path, std::uint64_t bytes, double time,
                              const std::string& kind);
    // Release an allocation through its owning service rather than ordinary file deletion.
    void release_service_file(const Network&, const Identity&, const std::string& host,
                              const std::string& path, double time);
    // Move workspace state between accessible hosts during relocation.
    void transfer_workspace(const Network&, const Identity&, const std::string& source_host,
                            const std::string& destination_host, double time);
    // Search accessible entries below a directory without revealing blocked content.
    std::vector<const FileEntry*> search(const Network&, const Identity&, const std::string& host,
                                         const std::string& directory,
                                         const std::string& query) const;

  private:
    std::vector<FileEntry> entries_;
    unsigned default_file_mode_ = 0600;
    unsigned default_directory_mode_ = 0700;
    std::uint64_t next_id_ = 1;
    std::string workspace_root_ = "/home/engineer/assistant";
    void migrate_service_files();
    void require_unmanaged(const std::string& host, const std::string& path) const;
    const FileEntry* lookup(const std::string& host, const std::string& path) const;
    const FileEntry& find(const std::string& host, const std::string& path) const;
    void check_host(const Network&, const std::string& host) const;
    const FileEntry& listing_directory(const Network&, const Identity&, const std::string& host,
                                       const std::string& directory) const;
    void traverse(const Identity&, const std::string& host, const std::string& path) const;
    void check_parent_write(const Network&, const Identity&, const std::string& host,
                            const std::string& path) const;
    void create(const Identity&, const std::string& host, const std::string& path, bool directory,
                const std::string& content, double time);
};
} // namespace ai_sim
