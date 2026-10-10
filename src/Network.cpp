#include <algorithm>
#include "ai_sim/DefinitionLoader.h"
#include "ai_sim/Network.h"

#include <toml++/toml.hpp>

#include <cmath>
#include <cstdint>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace ai_sim {
namespace {

std::string required_text(const toml::table& table, const char* key) {
    const auto value = table[key].value<std::string>();

    if (!value || value->empty()) {
        throw std::runtime_error(std::string(key) + " must be a nonempty string");
    }

    return *value;
}

std::string optional_text(const toml::table& table, const char* key, const std::string& fallback) {
    const auto* node = table.get(key);

    if (!node) {
        return fallback;
    }

    const auto value = node->value<std::string>();

    if (!value) {
        throw std::runtime_error(std::string(key) + " must be a string");
    }

    return *value;
}

bool optional_bool(const toml::table& table, const char* key, bool fallback) {
    const auto* node = table.get(key);

    if (!node) {
        return fallback;
    }

    const auto value = node->value<bool>();

    if (!value) {
        throw std::runtime_error(std::string(key) + " must be a boolean");
    }

    return *value;
}

double optional_number(const toml::table& table, const char* key, double fallback) {
    const auto* node = table.get(key);

    if (!node) {
        return fallback;
    }

    if (!node->is_number()) {
        throw std::runtime_error(std::string(key) + " must be a number");
    }

    const auto value = node->value<double>();

    if (!value || !std::isfinite(*value)) {
        throw std::runtime_error(std::string(key) + " must be finite");
    }

    return *value;
}

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

Network Network::load(const std::filesystem::path& path) {
    try {
        const auto document = DefinitionLoader::load(path);

        const auto version = document["schema_version"].value<std::int64_t>();

        require(version && *version == 1, "schema_version must be 1");

        const auto* settings = document["network"].as_table();
        require(settings != nullptr, "Missing [network]");

        const std::string local_id = required_text(*settings, "local_host");

        const auto* host_array = document["hosts"].as_array();
        require(host_array != nullptr, "Missing [[hosts]]");

        Network result;
        std::unordered_map<std::string, std::size_t> indices;

        for (const auto& node : *host_array) {
            const auto* table = node.as_table();
            require(table != nullptr, "Each host must be a table");

            NetworkHost host;

            host.id = required_text(*table, "id");
            host.name = optional_text(*table, "name", host.id);
            host.address = optional_text(*table, "address", "unknown");
            host.kind = optional_text(*table, "kind", "computer");
            host.description = optional_text(*table, "description", "");

            host.x = optional_number(*table, "x", 0.0);
            host.y = optional_number(*table, "y", 0.0);

            require(std::abs(host.x) <= 10000.0 && std::abs(host.y) <= 10000.0,
                    "Host coordinates must be within -10000 to 10000");

            host.known = optional_bool(*table, "known", true);
            host.online = optional_bool(*table, "online", true);
            host.access_granted = optional_bool(*table, "access_granted", false);
            host.ai_present = optional_bool(*table, "ai_present", false);

            if (const auto* capacity_node = table->get("storage_capacity_bytes")) {
                const auto capacity = capacity_node->value<std::int64_t>();
                require(capacity_node->is_integer() && capacity && *capacity >= 0,
                        "storage_capacity_bytes must be a nonnegative integer");
                host.storage_capacity_bytes = static_cast<std::uint64_t>(*capacity);
            }

            if (const auto* services_node = table->get("services")) {
                const auto* services = services_node->as_array();

                require(services != nullptr, "services must be an array of strings");

                for (const auto& service : *services) {
                    const auto value = service.value<std::string>();

                    require(value.has_value(), "services must contain only strings");

                    host.services.push_back(*value);
                }
            }

            const auto index = result.hosts_.size();

            require(indices.emplace(host.id, index).second, "Duplicate host ID: " + host.id);

            result.hosts_.push_back(std::move(host));
        }

        const auto local = indices.find(local_id);

        require(local != indices.end(), "local_host does not name an existing host");

        result.local_host_index_ = local->second;

        require(result.hosts_[local->second].known, "The local host must be known");

        if (const auto* links_node = document.get("links")) {
            const auto* link_array = links_node->as_array();
            require(link_array != nullptr, "links must be an array");

            std::unordered_set<std::string> seen;

            for (const auto& node : *link_array) {
                const auto* table = node.as_table();
                require(table != nullptr, "Each link must be a table");

                const auto from_id = required_text(*table, "from");
                const auto to_id = required_text(*table, "to");

                const auto from = indices.find(from_id);
                const auto to = indices.find(to_id);

                require(from != indices.end() && to != indices.end(),
                        "Link references an unknown host");

                require(from_id != to_id, "A host cannot link to itself");

                const std::string pair =
                    from_id < to_id ? from_id + "\n" + to_id : to_id + "\n" + from_id;

                require(seen.insert(pair).second,
                        "Duplicate link between " + from_id + " and " + to_id);

                NetworkLink link;

                link.from = from->second;
                link.to = to->second;
                link.known = optional_bool(*table, "known", true);
                link.up = optional_bool(*table, "up", true);

                link.capacity_bits_per_second =
                    optional_number(*table, "capacity_bits_per_second", 0.0);

                link.forward_bits_per_second =
                    optional_number(*table, "forward_bits_per_second", 0.0);

                link.reverse_bits_per_second =
                    optional_number(*table, "reverse_bits_per_second", 0.0);

                link.latency_milliseconds = optional_number(*table, "latency_milliseconds", 0.0);

                link.packet_loss_percent = optional_number(*table, "packet_loss_percent", 0.0);

                require(link.capacity_bits_per_second > 0.0, "Link capacity must be positive");

                require(link.forward_bits_per_second >= 0.0 &&
                            link.reverse_bits_per_second >= 0.0 &&
                            link.forward_bits_per_second <= link.capacity_bits_per_second &&
                            link.reverse_bits_per_second <= link.capacity_bits_per_second,
                        "Traffic must be between zero and link capacity");

                require(link.latency_milliseconds >= 0.0, "Latency cannot be negative");

                require(link.packet_loss_percent >= 0.0 && link.packet_loss_percent <= 100.0,
                        "Packet loss must be between 0 and 100");

                result.links_.push_back(link);
            }
        }

        return result;
    } catch (const std::exception& error) {
        throw std::runtime_error(path.string() + ": " + error.what());
    }
}

const std::vector<NetworkHost>& Network::hosts() const {
    return hosts_;
}

const std::vector<NetworkLink>& Network::links() const {
    return links_;
}

std::size_t Network::local_host_index() const {
    return local_host_index_;
}

bool Network::reachable(std::size_t target) const {
    if (target >= hosts_.size() || !hosts_[target].known || !hosts_[target].online ||
        !hosts_[local_host_index_].online) {
        return false;
    }

    std::vector<bool> visited(hosts_.size(), false);
    std::queue<std::size_t> pending;

    visited[local_host_index_] = true;
    pending.push(local_host_index_);

    while (!pending.empty()) {
        const auto current = pending.front();
        pending.pop();

        if (current == target) {
            return true;
        }

        for (const auto& link : links_) {
            if (!link.known || !link.up) {
                continue;
            }

            std::size_t next;

            if (link.from == current) {
                next = link.to;
            } else if (link.to == current) {
                next = link.from;
            } else {
                continue;
            }

            if (!visited[next] && hosts_[next].known && hosts_[next].online) {
                visited[next] = true;
                pending.push(next);
            }
        }
    }

    return false;
}

void Network::grant_access(const std::string& id) {
    auto found = std::find_if(hosts_.begin(), hosts_.end(), [&](const NetworkHost& h) {
        return h.id == id;
    });
    require(found != hosts_.end(), "Unknown host: " + id);
    found->known = true;
    found->access_granted = true;
}

void Network::discover_host(const std::string& id) {
    std::size_t selected = hosts_.size();
    for (std::size_t index = 0; index < hosts_.size(); ++index)
        if (hosts_[index].id == id)
            selected = index;
    if (selected == hosts_.size())
        throw std::runtime_error("Unknown network host: " + id);
    hosts_[selected].known = true;
    for (auto& link : links_)
        if ((link.from == selected || link.to == selected) && hosts_[link.from].known &&
            hosts_[link.to].known)
            link.known = true;
}
void Network::import_missing(const Network& source) {
    std::unordered_map<std::string, std::size_t> indices;
    for (std::size_t index = 0; index < hosts_.size(); ++index)
        indices.emplace(hosts_[index].id, index);
    for (const auto& host : source.hosts_)
        if (!indices.contains(host.id)) {
            indices.emplace(host.id, hosts_.size());
            hosts_.push_back(host);
        }
    for (const auto& link : source.links_) {
        const auto from = indices.at(source.hosts_[link.from].id),
                   to = indices.at(source.hosts_[link.to].id);
        bool present = false;
        for (const auto& existing : links_)
            if ((existing.from == from && existing.to == to) ||
                (existing.from == to && existing.to == from))
                present = true;
        if (!present) {
            auto added = link;
            added.from = from;
            added.to = to;
            links_.push_back(added);
        }
    }
}
void Network::add_owned_host(NetworkHost host, const std::string& uplink) {
    if (host.id.empty() || host.name.empty() || hosts_.size() >= 1000)
        throw std::runtime_error("Invalid owned server");
    for (const auto& existing : hosts_)
        if (existing.id == host.id)
            throw std::runtime_error("Server already exists");
    auto from = std::find_if(hosts_.begin(), hosts_.end(), [&](const auto& existing) {
        return existing.id == uplink;
    });
    if (from == hosts_.end())
        throw std::runtime_error("Server uplink does not exist");
    const auto index = static_cast<std::size_t>(from - hosts_.begin());
    NetworkLink link;
    link.from = index;
    link.to = hosts_.size();
    link.capacity_bits_per_second = 100000000;
    link.latency_milliseconds = 20;
    hosts_.push_back(std::move(host));
    links_.push_back(link);
}
void Network::set_online(const std::string& id, bool online) {
    auto host = std::find_if(hosts_.begin(), hosts_.end(), [&](const auto& h) {
        return h.id == id;
    });
    if (host == hosts_.end())
        throw std::runtime_error("Unknown server");
    host->online = online;
}
void Network::relocate(const std::string& id) {
    auto host = std::find_if(hosts_.begin(), hosts_.end(), [&](const auto& h) {
        return h.id == id;
    });
    if (host == hosts_.end() || !host->online || !host->access_granted)
        throw std::runtime_error("Recovery server is unavailable");
    const auto target = static_cast<std::size_t>(host - hosts_.begin());
    if (target != local_host_index_) {
        hosts_[local_host_index_].ai_present = false;
        hosts_[local_host_index_].online = false;
        local_host_index_ = target;
    }
    host->ai_present = true;
}
} // namespace ai_sim
