#include "ai_sim/DefinitionLoader.h"
#include "ai_sim/FileSystem.h"
#include "ai_sim/TomlIO.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace ai_sim {
namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
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
    if (actor.user == "root") return true;
    unsigned bits = entry.mode & 7;
    if (actor.user == entry.owner) bits = (entry.mode >> 6) & 7;
    else if (actor.user == entry.group
        || std::find(actor.groups.begin(), actor.groups.end(), entry.group) != actor.groups.end()) {
        bits = (entry.mode >> 3) & 7;
    }
    return (bits & permission) == permission;
}
const NetworkHost& host_named(const Network& network, const std::string& id) {
    const auto found = std::find_if(network.hosts().begin(), network.hosts().end(),
        [&](const auto& host) { return host.id == id; });
    require(found != network.hosts().end(), "Unknown file host: " + id);
    return *found;
}
}

std::string FileSystem::category_of(const FileEntry& entry) {
    if(!entry.category.empty())return entry.category;
    if(entry.path.starts_with("/root/") || entry.path.starts_with("/etc/") || entry.path.find("/.")!=std::string::npos)return "hidden";
    if(entry.path.starts_with("/home/") && entry.path.find("/assistant/")==std::string::npos)return "personal";
    return "work";
}
std::vector<const FileEntry*> FileSystem::category_files(const Network& network,const Identity& actor,const std::string& category) const {
    require(category=="work" || category=="personal" || category=="hidden","Unknown file category");
    std::vector<const FileEntry*> out;
    for(const auto& entry:entries_) {
        if(entry.directory || category_of(entry)!=category)continue;
        try {(void)list(network,actor,entry.host_id,parent(entry.path));out.push_back(&entry);}catch(const std::exception&){}
    }
    std::sort(out.begin(),out.end(),[](const auto* a,const auto* b){return std::pair{a->path,a->host_id}<std::pair{b->path,b->host_id};});
    return out;
}
void FileSystem::set_category(const Network& network,const Identity& actor,const std::string& host,const std::string& path,const std::string& category) {
    require(category=="work" || category=="personal" || category=="hidden","Unknown file category");
    const auto& entry=inspect(network,actor,host,path);
    require(actor.user==entry.owner || actor.user=="root","Only the owner can organize this file");
    const_cast<FileEntry&>(entry).category=category;
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
        static_cast<char>('0' + ((mode >> 3) & 7)), static_cast<char>('0' + (mode & 7))};
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
        if (part == "..") { if (!parts.empty()) parts.pop_back(); }
        else if (!part.empty() && part != ".") parts.push_back(part);
        part.clear();
    };
    for (const unsigned char c : path) {
        require(c >= 32 && c != 127 && c != '\\',
                "File paths cannot contain control characters or backslashes");
        if (c == '/') finish(); else part += static_cast<char>(c);
    }
    finish();
    std::string result;
    for (const auto& item : parts) result += "/" + item;
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
            require(normalize(entry.path) == entry.path, "Entry paths must be canonical: " + entry.path);
            entry.id = table->contains("id") ? text(*table, "id")
                : "initial:" + entry.host_id + ":" + entry.path;
            require(ids.insert(entry.id).second, "Duplicate file ID");
            require(paths.insert(entry.host_id + "\n" + entry.path).second, "Duplicate file entry");
            entry.category = (*table)["category"].value_or(std::string{});
            require(entry.category.empty() || entry.category=="work" || entry.category=="personal" || entry.category=="hidden","Invalid file category");
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
                require(value.has_value() && !entry.directory, "Only files can have string content");
                entry.content = *value;
            }
            entry.size_bytes = entry.content.size();
            if (const auto* modified = table->get("modified_at_seconds")) {
                const auto value = modified->value<double>();
                require(modified->is_number() && value, "Invalid modified_at_seconds");
                timestamp(*value);
                entry.modified_at_seconds = *value;
            }
            result.entries_.push_back(std::move(entry));
        }
        for (const auto& entry : result.entries_) {
            if (entry.path == "/") require(entry.directory, "Root must be a directory");
            else require(result.find(entry.host_id, parent(entry.path)).directory, "Parent must be a directory");
        }
        for (const auto& host : network.hosts()) {
            require(result.storage_used(host.id) <= host.storage_capacity_bytes,
                    "Files exceed storage capacity: " + host.id);
        }
        return result;
    }
    catch (const std::exception& error) {
        throw std::runtime_error(path.string() + ": " + error.what());
    }
}
void FileSystem::save_snapshot(const std::filesystem::path& path) const {
    toml::table document{{"schema_version", 2}, {"filesystem", toml::table{
        {"default_file_mode", mode_text(default_file_mode_)},
        {"default_directory_mode", mode_text(default_directory_mode_)},
        {"next_id", static_cast<std::int64_t>(next_id_)}}}};
    toml::array entries;
    for (const auto& entry : entries_) {
        toml::table table{{"id", entry.id}, {"host_id", entry.host_id}, {"path", entry.path},
            {"owner", entry.owner}, {"group", entry.group}, {"mode", mode_text(entry.mode)},
            {"kind", entry.directory ? "directory" : "file"},
            {"modified_at_seconds", entry.modified_at_seconds}};
        if (!entry.category.empty()) table.insert("category",entry.category);
        if (!entry.directory) table.insert("content", entry.content);
        entries.push_back(std::move(table));
    }
    document.insert("entries", std::move(entries));
    write_toml(path, document);
}
void FileSystem::check_host(const Network& network, const std::string& host) const {
    const auto& hosts = network.hosts();
    const auto found = std::find_if(hosts.begin(), hosts.end(),
        [&](const auto& entry) { return entry.id == host && entry.known; });
    require(found != hosts.end(), "Unknown host: " + host);
    require(network.reachable(static_cast<std::size_t>(found - hosts.begin())), "No observed route to host: " + host);
    require(found->access_granted, "No credentials for host: " + host);
}
void FileSystem::traverse(const Identity& actor, const std::string& host, const std::string& path) const {
    if (path == "/") return;
    std::vector<std::string> ancestors;
    for (std::string current = parent(path);; current = parent(current)) {
        ancestors.push_back(current);
        if (current == "/") break;
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
std::vector<const FileEntry*> FileSystem::list(const Network& network, const Identity& actor,
    const std::string& host, const std::string& directory) const {
    const auto& entry = inspect(network, actor, host, directory);
    require(entry.directory, "Not a directory: " + entry.path);
    require(allowed(entry, actor, 4), "Permission denied: " + entry.path + " (list names)");
    std::vector<const FileEntry*> result;
    for (const auto& child : entries_) {
        if (child.host_id == host && child.path != "/" && parent(child.path) == entry.path) result.push_back(&child);
    }
    std::sort(result.begin(), result.end(), [](const auto* a, const auto* b) {
        return a->directory != b->directory ? a->directory : a->path < b->path;
    });
    return result;
}
void FileSystem::enter(const Network& network, const Identity& actor,
    const std::string& host, const std::string& directory) const {
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
    require(allowed(directory, actor, 3), "Permission denied: " + directory.path + " (write/traverse)");
}
void FileSystem::validate_write(const Network& network, const Identity& actor,
    const std::string& host, const std::string& path) const {
    check_host(network, host);
    const auto canonical = normalize(path);
    traverse(actor, host, canonical);
    if (const auto* entry = lookup(host, canonical)) {
        require(!entry->directory, "Cannot write a directory");
        require(allowed(*entry, actor, 2), "Permission denied: " + canonical + " (write)");
    }
    else check_parent_write(network, actor, host, canonical);
}
void FileSystem::create(const Identity& actor, const std::string& host,
    const std::string& path, bool directory, const std::string& content, double time) {
    timestamp(time);
    require(next_id_ < static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()), "File ID space exhausted");
    auto next = next_id_;
    std::string id;
    do {
        id = "file-" + std::to_string(next++);
        require(next < static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()), "File ID space exhausted");
    } while (std::any_of(entries_.begin(), entries_.end(), [&](const auto& item) { return item.id == id; }));
    FileEntry entry;
    entry.id = id; entry.host_id = host; entry.path = path;
    entry.owner = actor.user;
    entry.group = actor.groups.empty() ? actor.user : actor.groups.front();
    entry.directory = directory;
    entry.mode = directory ? default_directory_mode_ : default_file_mode_;
    entry.content = content; entry.size_bytes = content.size(); entry.modified_at_seconds = time;
    entries_.push_back(std::move(entry));
    next_id_ = next;
}
void FileSystem::write(const Network& network, const Identity& actor, const std::string& host,
    const std::string& path, const std::string& content, double time) {
    timestamp(time);
    const auto canonical = normalize(path);
    validate_write(network, actor, host, canonical);
    const auto* old = lookup(host, canonical);
    const auto used_without_old = storage_used(host) - (old ? old->size_bytes : 0);
    const auto capacity = host_named(network, host).storage_capacity_bytes;
    require(used_without_old <= capacity && content.size() <= capacity - used_without_old, "Not enough storage space");
    if (!old) create(actor, host, canonical, false, content, time);
    else {
        auto& entry = entries_[static_cast<std::size_t>(old - entries_.data())];
        entry.content = content;
        entry.size_bytes = content.size(); entry.modified_at_seconds = time;
    }
}
bool FileSystem::import_file(const Network& network, const FileEntry& source, double time) {
    require(source.category.empty() || source.category=="work" || source.category=="personal" || source.category=="hidden","Invalid scenario file category");
    require(source.mode <= 0777 && !source.directory && !source.owner.empty()
        && !source.group.empty(), "Invalid scenario file");
    const auto path = normalize(source.path);
    if (lookup(source.host_id, path)) return false;
    write(network, Identity{"root"}, source.host_id, path, source.content, time);
    auto& entry = entries_.back();
    entry.owner = source.owner;
    entry.group = source.group;
    entry.mode = source.mode;
    entry.category = source.category;
    return true;
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
    const auto& entry = inspect(network, actor, host, canonical);
    check_parent_write(network, actor, host, canonical);
    if (entry.directory) {
        require(std::none_of(entries_.begin(), entries_.end(), [&](const auto& child) {
            return child.host_id == host && parent(child.path) == canonical && child.path != canonical;
        }), "Directory is not empty");
    }
    const auto index = static_cast<std::size_t>(&entry - entries_.data());
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index));
}
void FileSystem::rename(const Network& network, const Identity& actor, const std::string& host,
    const std::string& source, const std::string& destination, double time) {
    timestamp(time);
    const auto from = normalize(source), to = normalize(destination);
    require(from != "/" && to != "/", "Cannot rename the root directory");
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
    const auto category=category_of(inspect(network,actor,host,source));
    const std::string content = read(network, actor, host, source);
    require(!lookup(host, normalize(destination)), "Destination already exists");
    write(network, actor, host, destination, content, time);
    set_category(network,actor,host,destination,category);
}
void FileSystem::chmod(const Network& network, const Identity& actor, const std::string& host,
    const std::string& path, unsigned mode, double time) {
    timestamp(time);
    require(mode <= 0777, "Invalid permission mode");
    const auto& old = inspect(network, actor, host, path);
    require(actor.user == "root" || actor.user == old.owner, "Only the owner or root can change permissions");
    auto& entry = entries_[static_cast<std::size_t>(&old - entries_.data())];
    entry.mode = mode; entry.modified_at_seconds = time;
}
}
