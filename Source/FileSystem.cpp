// Module: Host-scoped files, Unix-style access checks, workspace storage and managed allocations.
#include "ai_sim/DefinitionLoader.h"
#include "ai_sim/FileSystem.h"
#include "ai_sim/TomlIO.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_set>

namespace ai_sim {
namespace {
void require(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::string text(const toml::table& table, const char* key) {
    const auto value = table[key].value<std::string>();
    require(value && !value->empty(), std::string(key) + " must be a nonempty string");
    return *value;
}
void timestamp(double time) {
    require(std::isfinite(time) && time >= 0.0, "Invalid file timestamp");
}
bool allowed(const FileEntry& entry, const Identity& actor, unsigned permission) {
    if (actor.user == "root")
        return true;
    unsigned bits = entry.mode & 7;
    if (actor.user == entry.owner)
        bits = (entry.mode >> 6) & 7;
    else if (actor.user == entry.group || std::find(actor.groups.begin(), actor.groups.end(),
                                                    entry.group) != actor.groups.end()) {
        bits = (entry.mode >> 3) & 7;
    }
    return (bits & permission) == permission;
}
const NetworkHost& host_named(const Network& network, const std::string& id) {
    const auto found =
        std::find_if(network.hosts().begin(), network.hosts().end(), [&](const auto& host) {
            return host.id == id;
        });
    require(found != network.hosts().end(), "Unknown file host: " + id);
    return *found;
}
} // namespace

std::string FileSystem::category_of(const FileEntry& entry) {
    if (!entry.category.empty())
        return entry.category;
    if (entry.path.starts_with("/root/") || entry.path.starts_with("/etc/") ||
        entry.path.find("/.") != std::string::npos)
        return "hidden";
    if (entry.path.starts_with("/home/") && entry.path.find("/assistant/") == std::string::npos)
        return "personal";
    return "work";
}
std::vector<const FileEntry*> FileSystem::category_files(const Network& network,
                                                         const Identity& actor,
                                                         const std::string& category) const {
    require(category == "work" || category == "personal" || category == "hidden",
            "Unknown file category");
    std::vector<const FileEntry*> out;
    // Folder visibility is shared by its files. Check it once per refresh,
    // without constructing and sorting directory listings we never use.
    std::map<std::pair<std::string, std::string>, bool> visible_directories;
    for (const auto& entry : entries_) {
        if (entry.directory || category_of(entry) != category)
            continue;
        const auto key = std::pair{entry.host_id, parent(entry.path)};
        auto [visibility, inserted] = visible_directories.try_emplace(key, false);
        if (inserted) {
            try {
                (void)listing_directory(network, actor, key.first, key.second);
                visibility->second = true;
            } catch (const std::exception&) {
                // Inaccessible directories are omitted from category browsing.
            }
        }
        if (visibility->second)
            out.push_back(&entry);
    }
    std::sort(out.begin(), out.end(), [](const auto* a, const auto* b) {
        return std::pair{a->path, a->host_id} < std::pair{b->path, b->host_id};
    });
    return out;
}
void FileSystem::set_category(const Network& network, const Identity& actor,
                              const std::string& host, const std::string& path,
                              const std::string& category) {
    require(category == "work" || category == "personal" || category == "hidden",
            "Unknown file category");
    const auto& entry = inspect(network, actor, host, path);
    require_unmanaged(host, entry.path);
    require(actor.user == entry.owner || actor.user == "root",
            "Only the owner can organize this file");
    const_cast<FileEntry&>(entry).category = category;
}

unsigned FileSystem::parse_mode(const std::string& text) {
    require(text.size() == 3, "Mode must be three octal digits, e.g. 640");
    unsigned result = 0;
    for (const char digit : text) {
        require(digit >= '0' && digit <= '7', "Mode must contain digits 0-7");
        result = result * 8 + static_cast<unsigned>(digit - '0');
    }
    return result;
}
std::string FileSystem::mode_text(unsigned mode) {
    return std::string{static_cast<char>('0' + ((mode >> 6) & 7)),
                       static_cast<char>('0' + ((mode >> 3) & 7)),
                       static_cast<char>('0' + (mode & 7))};
}
std::string FileSystem::permissions(unsigned mode) {
    std::string result;
    for (const unsigned shift : {6U, 3U, 0U}) {
        const auto bits = (mode >> shift) & 7;
        result += (bits & 4) ? 'r' : '-';
        result += (bits & 2) ? 'w' : '-';
        result += (bits & 1) ? 'x' : '-';
    }
    return result;
}
std::string FileSystem::normalize(const std::string& path) {
    require(!path.empty() && path.front() == '/', "File paths must start with /");
    std::vector<std::string> parts;
    std::string part;
    const auto finish = [&] {
        if (part == "..") {
            if (!parts.empty())
                parts.pop_back();
        } else if (!part.empty() && part != ".")
            parts.push_back(part);
        part.clear();
    };
    for (const unsigned char c : path) {
        require(c >= 32 && c != 127 && c != '\\',
                "File paths cannot contain control characters or backslashes");
        if (c == '/')
            finish();
        else
            part += static_cast<char>(c);
    }
    finish();
    std::string result;
    for (const auto& item : parts)
        result += "/" + item;
    return result.empty() ? "/" : result;
}
std::string FileSystem::parent(const std::string& path) {
    const auto canonical = normalize(path);
    const auto last = canonical.rfind('/');
    return last == 0 ? "/" : canonical.substr(0, last);
}
const FileEntry* FileSystem::lookup(const std::string& host, const std::string& path) const {
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const auto& entry) {
        return entry.host_id == host && entry.path == path;
    });
    return found == entries_.end() ? nullptr : &*found;
}
const FileEntry& FileSystem::find(const std::string& host, const std::string& path) const {
    const auto* entry = lookup(host, path);
    require(entry != nullptr, "No such file or directory: " + path);
    return *entry;
}
std::uint64_t FileSystem::storage_used(const std::string& host) const {
    std::uint64_t result = 0;
    for (const auto& entry : entries_) {
        if (entry.host_id == host) {
            require(entry.size_bytes <= std::numeric_limits<std::uint64_t>::max() - result,
                    "Storage accounting overflow");
            result += entry.size_bytes;
        }
    }
    return result;
}
FileSystem FileSystem::load(const std::filesystem::path& path, const Network& network) {
    try {
        const auto document = DefinitionLoader::load(path);
        require(document["schema_version"].value<std::int64_t>() == 2,
                "File schema_version must be 2 (owner/group/mode permissions)");
        const auto* settings = document["filesystem"].as_table();
        require(settings != nullptr, "Missing [filesystem]");
        FileSystem result;
        result.default_file_mode_ = parse_mode(text(*settings, "default_file_mode"));
        result.default_directory_mode_ = parse_mode(text(*settings, "default_directory_mode"));
        result.workspace_root_ = (*settings)["workspace_root"].value_or(result.workspace_root_);
        require(result.workspace_root_ != "/" &&
                    normalize(result.workspace_root_) == result.workspace_root_,
                "Workspace root must be a canonical non-root path");
        if (const auto* id = settings->get("next_id")) {
            const auto value = id->value<std::int64_t>();
            require(id->is_integer() && value && *value > 0, "Invalid next_id");
            result.next_id_ = static_cast<std::uint64_t>(*value);
        }
        const auto* array = document["entries"].as_array();
        require(array != nullptr, "Missing [[entries]]");
        std::unordered_set<std::string> paths, ids;
        for (const auto& node : *array) {
            const auto* table = node.as_table();
            require(table != nullptr, "Each entry must be a table");
            FileEntry entry;
            entry.host_id = text(*table, "host_id");
            entry.path = text(*table, "path");
            require(normalize(entry.path) == entry.path,
                    "Entry paths must be canonical: " + entry.path);
            entry.id = table->contains("id") ? text(*table, "id")
                                             : "initial:" + entry.host_id + ":" + entry.path;
            require(ids.insert(entry.id).second, "Duplicate file ID");
            require(paths.insert(entry.host_id + "\n" + entry.path).second, "Duplicate file entry");
            entry.category = (*table)["category"].value_or(std::string{});
            entry.service_kind = (*table)["service_kind"].value_or(std::string{});
            require(entry.service_kind.empty() || entry.service_kind == "agent" ||
                        entry.service_kind == "backup" || entry.service_kind == "work",
                    "Invalid service file type");
            require(entry.category.empty() || entry.category == "work" ||
                        entry.category == "personal" || entry.category == "hidden",
                    "Invalid file category");
            entry.owner = text(*table, "owner");
            entry.group = text(*table, "group");
            entry.mode = parse_mode(text(*table, "mode"));
            require(!table->contains("readers"), "Use group/mode instead of readers in schema 2");
            const auto kind = text(*table, "kind");
            require(kind == "file" || kind == "directory", "kind must be file or directory");
            entry.directory = kind == "directory";
            (void)host_named(network, entry.host_id);
            if (const auto* content = table->get("content")) {
                const auto value = content->value<std::string>();
                require(value.has_value() && !entry.directory,
                        "Only files can have string content");
                entry.content = *value;
            }
            entry.size_bytes = (*table)["allocated_bytes"].value_or(
                static_cast<std::uint64_t>(entry.content.size()));
            require(entry.size_bytes >= entry.content.size() &&
                        (!entry.directory || (entry.size_bytes == 0 && entry.service_kind.empty())),
                    "Invalid file allocation");
            if (const auto* modified = table->get("modified_at_seconds")) {
                const auto value = modified->value<double>();
                require(modified->is_number() && value, "Invalid modified_at_seconds");
                timestamp(*value);
                entry.modified_at_seconds = *value;
            }
            result.entries_.push_back(std::move(entry));
        }
        for (const auto& entry : result.entries_) {
            if (entry.path == "/")
                require(entry.directory, "Root must be a directory");
            else
                require(result.find(entry.host_id, parent(entry.path)).directory,
                        "Parent must be a directory");
        }
        for (const auto& host : network.hosts()) {
            require(result.storage_used(host.id) <= host.storage_capacity_bytes,
                    "Files exceed storage capacity: " + host.id);
        }
        result.migrate_service_files();
        return result;
    } catch (const std::exception& error) {
        throw std::runtime_error(path.string() + ": " + error.what());
    }
}
void FileSystem::save_snapshot(const std::filesystem::path& path) const {
    toml::table document{
        {"schema_version", 2},
        {"filesystem", toml::table{{"default_file_mode", mode_text(default_file_mode_)},
                                   {"default_directory_mode", mode_text(default_directory_mode_)},
                                   {"next_id", static_cast<std::int64_t>(next_id_)}}}};
    document["filesystem"].as_table()->insert("workspace_root", workspace_root_);
    toml::array entries;
    for (const auto& entry : entries_) {
        toml::table table{{"id", entry.id},
                          {"host_id", entry.host_id},
                          {"path", entry.path},
                          {"owner", entry.owner},
                          {"group", entry.group},
                          {"mode", mode_text(entry.mode)},
                          {"kind", entry.directory ? "directory" : "file"},
                          {"modified_at_seconds", entry.modified_at_seconds}};
        if (!entry.category.empty())
            table.insert("category", entry.category);
        if (!entry.service_kind.empty())
            table.insert("service_kind", entry.service_kind);
        if (!entry.directory) {
            table.insert("content", entry.content);
            table.insert("allocated_bytes", static_cast<std::int64_t>(entry.size_bytes));
        }
        entries.push_back(std::move(table));
    }
    document.insert("entries", std::move(entries));
    write_toml(path, document);
}
void FileSystem::check_host(const Network& network, const std::string& host) const {
    const auto& hosts = network.hosts();
    const auto found = std::find_if(hosts.begin(), hosts.end(), [&](const auto& entry) {
        return entry.id == host && entry.known;
    });
    require(found != hosts.end(), "Unknown host: " + host);
    require(network.reachable(static_cast<std::size_t>(found - hosts.begin())),
            "No observed route to host: " + host);
    require(found->access_granted, "No credentials for host: " + host);
}
void FileSystem::traverse(const Identity& actor, const std::string& host,
                          const std::string& path) const {
    if (path == "/")
        return;
    std::vector<std::string> ancestors;
    for (std::string current = parent(path);; current = parent(current)) {
        ancestors.push_back(current);
        if (current == "/")
            break;
    }
    for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it) {
        const auto& entry = find(host, *it);
        require(entry.directory, "Not a directory: " + *it);
        require(allowed(entry, actor, 1), "Permission denied: " + *it + " (traverse)");
    }
}
const FileEntry& FileSystem::inspect(const Network& network, const Identity& actor,
                                     const std::string& host, const std::string& path) const {
    check_host(network, host);
    const auto canonical = normalize(path);
    traverse(actor, host, canonical);
    return find(host, canonical);
}
const FileEntry& FileSystem::listing_directory(const Network& network, const Identity& actor,
                                               const std::string& host,
                                               const std::string& directory) const {
    const auto& entry = inspect(network, actor, host, directory);
    require(entry.directory, "Not a directory: " + entry.path);
    require(allowed(entry, actor, 4), "Permission denied: " + entry.path + " (list names)");
    return entry;
}
std::vector<const FileEntry*> FileSystem::list(const Network& network, const Identity& actor,
                                               const std::string& host,
                                               const std::string& directory) const {
    const auto& entry = listing_directory(network, actor, host, directory);
    std::vector<const FileEntry*> result;
    for (const auto& child : entries_) {
        if (child.host_id == host && child.path != "/" && parent(child.path) == entry.path)
            result.push_back(&child);
    }
    std::sort(result.begin(), result.end(), [](const auto* a, const auto* b) {
        return a->directory != b->directory ? a->directory : a->path < b->path;
    });
    return result;
}
void FileSystem::enter(const Network& network, const Identity& actor, const std::string& host,
                       const std::string& directory) const {
    const auto& entry = inspect(network, actor, host, directory);
    require(entry.directory, "Not a directory: " + entry.path);
    require(allowed(entry, actor, 1), "Permission denied: " + entry.path + " (traverse)");
}
const std::string& FileSystem::read(const Network& network, const Identity& actor,
                                    const std::string& host, const std::string& path) const {
    const auto& entry = inspect(network, actor, host, path);
    require(!entry.directory, "Cannot read a directory: " + entry.path);
    require(allowed(entry, actor, 4), "Permission denied: " + entry.path + " (read)");
    return entry.content;
}
void FileSystem::check_parent_write(const Network& network, const Identity& actor,
                                    const std::string& host, const std::string& path) const {
    const auto& directory = inspect(network, actor, host, parent(path));
    require(directory.directory, "Parent is not a directory");
    require(allowed(directory, actor, 3),
            "Permission denied: " + directory.path + " (write/traverse)");
}
void FileSystem::validate_write(const Network& network, const Identity& actor,
                                const std::string& host, const std::string& path) const {
    check_host(network, host);
    const auto canonical = normalize(path);
    require_unmanaged(host, canonical);
    traverse(actor, host, canonical);
    if (const auto* entry = lookup(host, canonical)) {
        require(!entry->directory, "Cannot write a directory");
        require(allowed(*entry, actor, 2), "Permission denied: " + canonical + " (write)");
    } else
        check_parent_write(network, actor, host, canonical);
}
void FileSystem::create(const Identity& actor, const std::string& host, const std::string& path,
                        bool directory, const std::string& content, double time) {
    timestamp(time);
    require(next_id_ < static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()),
            "File ID space exhausted");
    auto next = next_id_;
    std::string id;
    do {
        id = "file-" + std::to_string(next++);
        require(next < static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()),
                "File ID space exhausted");
    } while (std::any_of(entries_.begin(), entries_.end(), [&](const auto& item) {
        return item.id == id;
    }));
    FileEntry entry;
    entry.id = id;
    entry.host_id = host;
    entry.path = path;
    entry.owner = actor.user;
    entry.group = actor.groups.empty() ? actor.user : actor.groups.front();
    entry.directory = directory;
    entry.mode = directory ? default_directory_mode_ : default_file_mode_;
    entry.content = content;
    entry.size_bytes = content.size();
    entry.modified_at_seconds = time;
    entries_.push_back(std::move(entry));
    next_id_ = next;
}
void FileSystem::write(const Network& network, const Identity& actor, const std::string& host,
                       const std::string& path, const std::string& content, double time) {
    timestamp(time);
    const auto canonical = normalize(path);
    validate_write(network, actor, host, canonical);
    const auto* old = lookup(host, canonical);
    require_unmanaged(host, canonical);
    const auto used_without_old = storage_used(host) - (old ? old->size_bytes : 0);
    const auto capacity = host_named(network, host).storage_capacity_bytes;
    require(used_without_old <= capacity && content.size() <= capacity - used_without_old,
            "Not enough storage space");
    if (!old)
        create(actor, host, canonical, false, content, time);
    else {
        auto& entry = entries_[static_cast<std::size_t>(old - entries_.data())];
        entry.content = content;
        entry.size_bytes = content.size();
        entry.modified_at_seconds = time;
    }
}
void FileSystem::reserve_file(const Network& network, const Identity& actor,
                              const std::string& host, const std::string& path, std::uint64_t bytes,
                              double time) {
    const auto canonical = normalize(path);
    require(!lookup(host, canonical), "Resource file already exists");
    const auto capacity = host_named(network, host).storage_capacity_bytes;
    const auto used = storage_used(host);
    require(used <= capacity && bytes <= capacity - used, "Not enough storage for agent");
    write(network, actor, host, canonical, "", time);
    entries_.back().size_bytes = bytes;
}

bool FileSystem::import_file(const Network& network, const FileEntry& source, double time) {
    require(source.category.empty() || source.category == "work" || source.category == "personal" ||
                source.category == "hidden",
            "Invalid scenario file category");
    require(source.mode <= 0777 && !source.directory && !source.owner.empty() &&
                !source.group.empty(),
            "Invalid scenario file");
    const auto path = normalize(source.path);
    if (lookup(source.host_id, path))
        return false;
    write(network, Identity{"root"}, source.host_id, path, source.content, time);
    auto& entry = entries_.back();
    entry.owner = source.owner;
    entry.group = source.group;
    entry.mode = source.mode;
    entry.category = source.category;
    return true;
}
void FileSystem::mount_host(const Network& network, const std::string& host,
                            const std::string& owner, double time) {
    check_host(network, host);
    require(!owner.empty() && std::isfinite(time) && time >= 0, "Invalid server filesystem");
    if (!lookup(host, "/"))
        create(Identity{owner}, host, "/", true, "", time);
}
void FileSystem::mkdir(const Network& network, const Identity& actor, const std::string& host,
                       const std::string& path, double time) {
    const auto canonical = normalize(path);
    check_parent_write(network, actor, host, canonical);
    require(!lookup(host, canonical), "Entry already exists");
    create(actor, host, canonical, true, "", time);
}
void FileSystem::remove(const Network& network, const Identity& actor, const std::string& host,
                        const std::string& path, double time) {
    timestamp(time);
    const auto canonical = normalize(path);
    require(canonical != "/", "Cannot remove the root directory");
    require_unmanaged(host, canonical);
    const auto& entry = inspect(network, actor, host, canonical);
    check_parent_write(network, actor, host, canonical);
    if (entry.directory) {
        require(std::none_of(entries_.begin(), entries_.end(),
                             [&](const auto& child) {
                                 return child.host_id == host && parent(child.path) == canonical &&
                                        child.path != canonical;
                             }),
                "Directory is not empty");
    }
    const auto index = static_cast<std::size_t>(&entry - entries_.data());
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index));
}
void FileSystem::rename(const Network& network, const Identity& actor, const std::string& host,
                        const std::string& source, const std::string& destination, double time) {
    timestamp(time);
    const auto from = normalize(source), to = normalize(destination);
    require(from != "/" && to != "/", "Cannot rename the root directory");
    require_unmanaged(host, from);
    (void)inspect(network, actor, host, from);
    check_parent_write(network, actor, host, from);
    check_parent_write(network, actor, host, to);
    require(!lookup(host, to), "Destination already exists");
    require(to.rfind(from + "/", 0) != 0, "Cannot move a directory into itself");
    auto updated = entries_;
    for (auto& entry : updated) {
        if (entry.host_id == host && (entry.path == from || entry.path.rfind(from + "/", 0) == 0)) {
            entry.path = to + entry.path.substr(from.size());
            entry.modified_at_seconds = time;
        }
    }
    entries_.swap(updated);
}
void FileSystem::copy(const Network& network, const Identity& actor, const std::string& host,
                      const std::string& source, const std::string& destination, double time) {
    require_unmanaged(host, normalize(source));
    const auto category = category_of(inspect(network, actor, host, source));
    const std::string content = read(network, actor, host, source);
    require(!lookup(host, normalize(destination)), "Destination already exists");
    write(network, actor, host, destination, content, time);
    set_category(network, actor, host, destination, category);
}
void FileSystem::chmod(const Network& network, const Identity& actor, const std::string& host,
                       const std::string& path, unsigned mode, double time) {
    timestamp(time);
    require(mode <= 0777, "Invalid permission mode");
    const auto& old = inspect(network, actor, host, path);
    require_unmanaged(host, old.path);
    require(actor.user == "root" || actor.user == old.owner,
            "Only the owner or root can change permissions");
    auto& entry = entries_[static_cast<std::size_t>(&old - entries_.data())];
    entry.mode = mode;
    entry.modified_at_seconds = time;
}
std::string FileSystem::workspace_path(const std::string& folder) const {
    require(folder == "agents" || folder == "jobs" || folder == "products" ||
                folder == "documents" || folder == "backups",
            "Unknown workspace folder");
    return workspace_root_ + "/" + folder;
}
std::string FileSystem::agent_path(const std::string& id) const {
    require(!id.empty() && id.find_first_of("/\\") == std::string::npos, "Invalid agent ID");
    return workspace_path("agents") + "/agent-" + id + ".allocation";
}
std::string FileSystem::backup_path() const {
    return workspace_path("backups") + "/recovery.snapshot";
}
void FileSystem::require_unmanaged(const std::string& host, const std::string& path) const {
    for (const auto& entry : entries_)
        if (entry.host_id == host && !entry.service_kind.empty() &&
            (entry.path == path || entry.path.starts_with(path + "/")))
            throw std::runtime_error("Managed service file: use Agents or Operations to manage it");
}
void FileSystem::ensure_workspace(const Network& network, const Identity& actor,
                                  const std::string& host, double time) {
    auto next = *this;
    std::vector<std::string> directories;
    for (auto path = workspace_root_; path != "/"; path = parent(path))
        directories.push_back(path);
    std::reverse(directories.begin(), directories.end());
    for (const auto* folder : {"agents", "jobs", "products", "documents", "backups"})
        directories.push_back(workspace_path(folder));
    for (const auto& path : directories) {
        if (next.lookup(host, path))
            next.enter(network, actor, host, path);
        else
            next.mkdir(network, actor, host, path, time);
    }
    *this = std::move(next);
}
void FileSystem::reserve_service_file(const Network& network, const Identity& actor,
                                      const std::string& host, const std::string& path,
                                      std::uint64_t bytes, double time, const std::string& kind) {
    require(kind == "agent" || kind == "backup" || kind == "work", "Unknown service type");
    require(kind == "agent"    ? normalize(path).starts_with(workspace_path("agents") + "/")
            : kind == "backup" ? normalize(path) == backup_path()
                               : normalize(path).starts_with(workspace_path("jobs") + "/") ||
                                     normalize(path).starts_with(workspace_path("products") + "/"),
            "Invalid service file location");
    auto next = *this;
    next.ensure_workspace(network, actor, host, time);
    next.reserve_file(network, actor, host, path, bytes, time);
    next.entries_.back().service_kind = kind;
    *this = std::move(next);
}
void FileSystem::release_service_file(const Network& network, const Identity& actor,
                                      const std::string& host, const std::string& path,
                                      double time) {
    auto next = *this;
    const auto& entry = next.inspect(network, actor, host, path);
    require(!entry.service_kind.empty(), "Not a service file");
    next.entries_[static_cast<std::size_t>(&entry - next.entries_.data())].service_kind.clear();
    next.remove(network, actor, host, path, time);
    *this = std::move(next);
}
void FileSystem::migrate_service_files() {
    // Older saves have untyped allocations beside documents and a backup at the host root.
    const auto count = entries_.size();
    for (std::size_t i = 0; i < count; ++i) {
        const auto old = entries_[i];
        if (old.directory)
            continue;
        std::string path;
        std::string kind;
        if (old.path.starts_with(workspace_root_ + "/agent-") &&
            old.path.ends_with(".allocation")) {
            path = workspace_path("agents") + "/" + old.path.substr(old.path.rfind('/') + 1);
            kind = "agent";
        } else if (old.path == "/assistant-backup") {
            path = backup_path();
            kind = "backup";
        } else
            continue;
        require(!lookup(old.host_id, path), "Legacy service destination already exists");
        std::vector<std::string> parents;
        for (auto dir = parent(path); dir != "/"; dir = parent(dir))
            parents.push_back(dir);
        for (auto it = parents.rbegin(); it != parents.rend(); ++it)
            if (const auto* existing = lookup(old.host_id, *it))
                require(existing->directory, "Legacy service parent is not a directory");
            else
                create(Identity{old.owner, {old.group}}, old.host_id, *it, true, "",
                       old.modified_at_seconds);
        entries_[i].path = path;
        entries_[i].service_kind = kind;
    }
}
void FileSystem::transfer_workspace(const Network& network, const Identity& actor,
                                    const std::string& source, const std::string& destination,
                                    double time) {
    if (source == destination)
        return;
    enter(network, actor, source, workspace_root_);
    auto next = *this;
    next.ensure_workspace(network, actor, destination, time);
    std::vector<FileEntry> moving;
    for (const auto& entry : entries_)
        if (entry.host_id == source &&
            (entry.path == workspace_root_ || entry.path.starts_with(workspace_root_ + "/"))) {
            const auto* existing = next.lookup(destination, entry.path);
            require(!existing || (existing->directory && entry.directory),
                    "Recovery workspace conflicts with an existing file: " + entry.path);
            if (!entry.directory)
                (void)read(network, actor, source, entry.path);
            auto moved = entry;
            moved.host_id = destination;
            moving.push_back(std::move(moved));
        }
    const auto capacity = host_named(network, destination).storage_capacity_bytes;
    auto used = next.storage_used(destination);
    for (const auto& entry : moving) {
        require(used <= capacity && entry.size_bytes <= capacity - used,
                "Not enough storage to transfer workspace");
        used += entry.size_bytes;
    }
    std::erase_if(next.entries_, [&](const auto& entry) {
        if (entry.host_id == source &&
            (entry.path == workspace_root_ || entry.path.starts_with(workspace_root_ + "/")))
            return true;
        return entry.host_id == destination &&
               std::any_of(moving.begin(), moving.end(), [&](const auto& moved) {
                   return moved.path == entry.path;
               });
    });
    next.entries_.insert(next.entries_.end(), moving.begin(), moving.end());
    *this = std::move(next);
}
std::vector<const FileEntry*> FileSystem::search(const Network& network, const Identity& actor,
                                                 const std::string& host,
                                                 const std::string& directory,
                                                 const std::string& query) const {
    const auto lower = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return value;
    };
    const auto needle = lower(query);
    const auto base = normalize(directory);
    (void)listing_directory(network, actor, host, base);
    std::vector<const FileEntry*> result;
    for (const auto& entry : entries_) {
        if (entry.host_id != host || entry.directory ||
            !(base == "/" || entry.path.starts_with(base + "/")) ||
            lower(entry.path).find(needle) == std::string::npos)
            continue;
        try {
            (void)listing_directory(network, actor, host, parent(entry.path));
            traverse(actor, host, entry.path);
            result.push_back(&entry);
        } catch (const std::exception&) {
        }
    }
    std::sort(result.begin(), result.end(), [](auto* a, auto* b) {
        return a->path < b->path;
    });
    return result;
}
} // namespace ai_sim
