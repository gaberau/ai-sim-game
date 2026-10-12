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
    static Network load(const std::filesystem::path& path);
    void save_snapshot(const std::filesystem::path& path) const;

    const std::vector<NetworkHost>& hosts() const;
    const std::vector<NetworkLink>& links() const;

    std::size_t local_host_index() const;

    // Tests connectivity through known, operational links.
    bool reachable(std::size_t target) const;
    void grant_access(const std::string& id);
    void discover_host(const std::string& id);
    void import_missing(const Network& source);
    void add_owned_host(NetworkHost host, const std::string& uplink);
    void set_online(const std::string& id, bool online);
    void relocate(const std::string& id);

  private:
    std::vector<NetworkHost> hosts_;
    std::vector<NetworkLink> links_;
    std::size_t local_host_index_ = 0;
};

} // namespace ai_sim
