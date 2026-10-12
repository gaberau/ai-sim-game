// Verify: core models. Fixtures isolate these behaviors from live player saves.
#include "ai_sim/DefinitionLoader.h"
#include "ai_sim/Computer.h"
#include "ai_sim/FileSystem.h"
#include "ai_sim/Network.h"

#include <toml++/toml.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>

namespace {

void check(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}

template <class F> void rejected(F action, const std::string& fragment) {
    try {
        action();
    } catch (const std::exception& error) {
        check(std::string(error.what()).find(fragment) != std::string::npos,
              "Wrong rejection: " + std::string(error.what()));
        return;
    }
    throw std::runtime_error("Expected rejection containing: " + fragment);
}

struct TemporaryDirectory {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("ai-sim-tests-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryDirectory() {
        std::filesystem::create_directories(path);
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

void save(const toml::table& document, const std::filesystem::path& path) {
    std::ofstream output(path);
    output << document;
    check(output.good(), "Could not write test fixture");
}

std::size_t host_index(const ai_sim::Network& network, const std::string& id) {
    for (std::size_t i = 0; i < network.hosts().size(); ++i) {
        if (network.hosts()[i].id == id)
            return i;
    }
    throw std::runtime_error("Missing test host: " + id);
}

toml::table& host_table(toml::table& document, const std::string& id) {
    for (auto& node : *document["hosts"].as_array()) {
        auto& table = *node.as_table();
        if (table["id"].value<std::string>() == id)
            return table;
    }
    throw std::runtime_error("Missing fixture host");
}

} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected project directory");
        const auto scenario = std::filesystem::path(argv[1]) / "data";
        TemporaryDirectory temporary;
        const auto fixture = temporary.path / "network.toml";

        auto computer = ai_sim::Computer::load(scenario / "computers/engineer_pc.toml");
        const auto rates = computer.cpu_allocations();
        check(std::accumulate(rates.begin(), rates.end(), 0.0) <= computer.cpu_cores() + 1e-9,
              "CPU allocation exceeds capacity");
        rejected(
            [&] {
                computer.stop("email_ai");
            },
            "Self-termination");

        const auto network = ai_sim::Network::load(scenario / "networks/office.toml");
        const auto files = ai_sim::FileSystem::load(scenario / "files/office.toml", network);
        const auto server = host_index(network, "file_server");
        check(network.reachable(server), "Server should have a route");
        check(!network.reachable(network.hosts().size()), "Invalid index reachable");
        check(
            !files.list(network, ai_sim::Identity{"engineer", {"engineering"}}, "engineer_pc", "/")
                 .empty(),
            "Local root should be listable");
        const auto& checklist = files.read(network, ai_sim::Identity{"engineer", {"engineering"}},
                                           "file_server", "/engineering/deployment-checklist.txt");
        check(checklist.find("maintenance window") != std::string::npos,
              "Wrong remote file content");
        check(files.inspect(network, ai_sim::Identity{"engineer", {"engineering"}}, "file_server",
                            "/engineering/deployment-checklist.txt")
                      .size_bytes == checklist.size(),
              "File size must match content bytes");
        rejected(
            [&] {
                files.read(network, ai_sim::Identity{"engineer", {"engineering"}}, "file_server",
                           "/finance/budget.txt");
            },
            "Permission denied: /finance");
        rejected(
            [&] {
                files.read(network, ai_sim::Identity{"engineer", {"engineering"}}, "engineer_pc",
                           "/root/admin-notes.txt");
            },
            "Permission denied: /root");
        check(!files.read(network, "root", "engineer_pc", "/root/admin-notes.txt").empty(),
              "Root read denied");
        rejected(
            [&] {
                files.list(network, "root", "engineer_phone", "/");
            },
            "No credentials");
        rejected(
            [&] {
                files.read(network, ai_sim::Identity{"engineer", {"engineering"}}, "engineer_pc",
                           "/home");
            },
            "Cannot read a directory");
        check(ai_sim::FileSystem::normalize("/home//engineer/../engineer/./notes.txt") ==
                  "/home/engineer/notes.txt",
              "Path normalization failed");
        rejected(
            [&] {
                (void)ai_sim::FileSystem::normalize("relative");
            },
            "must start with");

        auto document = ai_sim::DefinitionLoader::load(scenario / "networks/office.toml");
        for (auto& node : *document["links"].as_array()) {
            auto& table = *node.as_table();
            if (table["to"].value<std::string>() == "file_server") {
                table.insert_or_assign("up", false);
            }
        }
        save(document, fixture);
        const auto disconnected = ai_sim::Network::load(fixture);
        check(!disconnected.reachable(server), "Broken route still reachable");
        rejected(
            [&] {
                files.read(disconnected, ai_sim::Identity{"engineer", {"engineering"}},
                           "file_server", "/engineering/deployment-checklist.txt");
            },
            "No observed route");

        document = ai_sim::DefinitionLoader::load(scenario / "networks/office.toml");
        host_table(document, "file_server").insert_or_assign("access_granted", false);
        save(document, fixture);
        const auto no_credentials = ai_sim::Network::load(fixture);
        check(no_credentials.reachable(server), "Credentials changed connectivity");
        rejected(
            [&] {
                files.list(no_credentials, ai_sim::Identity{"engineer", {"engineering"}},
                           "file_server", "/");
            },
            "No credentials");

        document = ai_sim::DefinitionLoader::load(scenario / "networks/office.toml");
        host_table(document, "office_router").insert_or_assign("online", false);
        save(document, fixture);
        check(!ai_sim::Network::load(fixture).reachable(server),
              "Offline router should break path");

        document = ai_sim::DefinitionLoader::load(scenario / "networks/office.toml");
        host_table(document, "file_server").insert_or_assign("known", false);
        save(document, fixture);
        rejected(
            [&] {
                files.list(ai_sim::Network::load(fixture),
                           ai_sim::Identity{"engineer", {"engineering"}}, "file_server", "/");
            },
            "Unknown host");

        document = ai_sim::DefinitionLoader::load(scenario / "networks/office.toml");
        auto& link = *document["links"].as_array()->get(0)->as_table();
        link.insert_or_assign("forward_bits_per_second", 1e20);
        save(document, fixture);
        rejected(
            [&] {
                (void)ai_sim::Network::load(fixture);
            },
            "link capacity");

        auto file_document = ai_sim::DefinitionLoader::load(scenario / "files/office.toml");
        auto& entry = *file_document["entries"].as_array()->get(0)->as_table();
        entry.insert_or_assign("host_id", "missing_host");
        const auto file_fixture = temporary.path / "files.toml";
        save(file_document, file_fixture);
        rejected(
            [&] {
                (void)ai_sim::FileSystem::load(file_fixture, network);
            },
            "Unknown file host");

        // A readable file cannot bypass its protected ancestor directory.
        file_document = ai_sim::DefinitionLoader::load(scenario / "files/office.toml");
        for (auto& node : *file_document["entries"].as_array()) {
            auto& table = *node.as_table();
            if (table["path"].value<std::string>() == "/finance/budget.txt") {
                table.insert_or_assign("mode", "644");
            }
        }
        save(file_document, file_fixture);
        const auto protected_files = ai_sim::FileSystem::load(file_fixture, network);
        rejected(
            [&] {
                protected_files.read(network, ai_sim::Identity{"engineer", {"engineering"}},
                                     "file_server", "/finance/budget.txt");
            },
            "Permission denied: /finance");

        std::cout << "Core model tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }
}
