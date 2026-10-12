// Module: Known hosts, links, reachability and player access to the simulated network.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ai_sim {

struct NetworkHost {
    std::string id;
    std::string name;
    std::string address;
    std::string kind;
    std::string description;

    // Map coordinates, independent of the terminal window.
    double x = 0.0;
    double y = 0.0;

    bool known = true;
    bool online = true;
    bool access_granted = false;
    bool ai_present = false;
    std::uint64_t storage_capacity_bytes = 1099511627776ULL;

    std::vector<std::string> services;
};

struct NetworkLink {
    std::size_t from = 0;
    std::size_t to = 0;

    bool known = true;
    bool up = true;

    // Full-duplex capacity: this limit applies in each direction.
    double capacity_bits_per_second = 0.0;
    double forward_bits_per_second = 0.0;
    double reverse_bits_per_second = 0.0;

    double latency_milliseconds = 0.0;
    double packet_loss_percent = 0.0;
};

class Network {
  public:
    // Read authored TOML definitions and reject malformed values.
    static Network load(const std::filesystem::path& path);
    // Serialize current state for a save snapshot; does not publish a save-slot manifest.
    void save_snapshot(const std::filesystem::path& path) const;

    // Borrow the current host catalog; references remain owned by this network.
    const std::vector<NetworkHost>& hosts() const;
    // Borrow the current link catalog; endpoints index the host catalog.
    const std::vector<NetworkLink>& links() const;

    // Return the host index where the AI currently executes.
    std::size_t local_host_index() const;

    // Tests connectivity through known, operational links.
    bool reachable(std::size_t target) const;
    // Enable player access to the named known host.
    void grant_access(const std::string& id);
    // Mark the named host as known without granting credentials.
    void discover_host(const std::string& id);
    // Merge missing authored topology while preserving existing host state.
    void import_missing(const Network& source);
    // Add an owned host connected to the named uplink.
    void add_owned_host(NetworkHost host, const std::string& uplink);
    // Change a host's operational state without discarding its saved contents.
    void set_online(const std::string& id, bool online);
    // Move the AI execution location to the selected reachable host.
    void relocate(const std::string& id);

  private:
    std::vector<NetworkHost> hosts_;
    std::vector<NetworkLink> links_;
    std::size_t local_host_index_ = 0;
};

} // namespace ai_sim
