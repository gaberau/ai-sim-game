// Module: Serialize and validate topology snapshots without depending on terminal geometry.
#include "ai_sim/Network.h"
#include "ai_sim/TomlIO.h"

namespace ai_sim {
void Network::save_snapshot(const std::filesystem::path& path) const {
    toml::table data{{"schema_version", 1},
                     {"network", toml::table{{"local_host", hosts_[local_host_index_].id}}}};
    toml::array hosts;
    for (const auto& host : hosts_) {
        toml::array services;
        for (const auto& service : host.services)
            services.push_back(service);
        hosts.push_back(toml::table{
            {"id", host.id},
            {"name", host.name},
            {"address", host.address},
            {"kind", host.kind},
            {"description", host.description},
            {"x", host.x},
            {"y", host.y},
            {"known", host.known},
            {"online", host.online},
            {"access_granted", host.access_granted},
            {"ai_present", host.ai_present},
            {"services", std::move(services)},
            {"storage_capacity_bytes", static_cast<std::int64_t>(host.storage_capacity_bytes)}});
    }
    toml::array links;
    for (const auto& link : links_) {
        links.push_back(toml::table{{"from", hosts_[link.from].id},
                                    {"to", hosts_[link.to].id},
                                    {"known", link.known},
                                    {"up", link.up},
                                    {"capacity_bits_per_second", link.capacity_bits_per_second},
                                    {"forward_bits_per_second", link.forward_bits_per_second},
                                    {"reverse_bits_per_second", link.reverse_bits_per_second},
                                    {"latency_milliseconds", link.latency_milliseconds},
                                    {"packet_loss_percent", link.packet_loss_percent}});
    }
    data.insert("hosts", std::move(hosts));
    data.insert("links", std::move(links));
    write_toml(path, data);
}
} // namespace ai_sim
