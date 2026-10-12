// Verify: saves and permissions. Fixtures isolate these behaviors from live player saves.
#include "ai_sim/DefinitionLoader.h"
#include "ai_sim/SaveSystem.h"
#include "ai_sim/TomlIO.h"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
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
    throw std::runtime_error("Expected rejection: " + fragment);
}
struct TemporaryDirectory {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("ai-sim-save-tests-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryDirectory() {
        std::filesystem::create_directories(path);
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};
} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected project directory");
        const auto scenario = std::filesystem::path(argv[1]) / "data";
        auto world = ai_sim::SaveSystem::new_game(scenario / "scenarios/hobbyist.toml");
        const ai_sim::Identity engineer{"engineer", {"engineering"}};
        const ai_sim::Identity colleague{"colleague", {"engineering", "engineer"}};
        const std::string host = "engineer_pc", home = "/home/engineer";
        auto& files = world.files;
        const auto& network = world.network;
        TemporaryDirectory temporary;

        files.write(network, engineer, host, home + "/new.txt", "first\nsecond\n", 10.0);
        const auto original_id = files.inspect(network, engineer, host, home + "/new.txt").id;
        check(files.inspect(network, engineer, host, home + "/new.txt").mode == 0600,
              "Wrong default file permissions");
        files.chmod(network, engineer, host, home + "/new.txt", 0640, 11.0);
        rejected(
            [&] {
                files.read(network, colleague, host, home + "/new.txt");
            },
            "traverse");
        files.chmod(network, engineer, host, home, 0750, 12.0);
        check(files.read(network, colleague, host, home + "/new.txt") == "first\nsecond\n",
              "Group read denied");
        rejected(
            [&] {
                files.write(network, colleague, host, home + "/new.txt", "bad", 13.0);
            },
            "write");
        // Owner bits take precedence over group bits.
        files.chmod(network, engineer, host, home + "/new.txt", 0460, 13.0);
        rejected(
            [&] {
                files.write(network, engineer, host, home + "/new.txt", "bad", 14.0);
            },
            "write");
        files.chmod(network, engineer, host, home + "/new.txt", 0640, 14.0);
        rejected(
            [&] {
                files.chmod(network, colleague, host, home + "/new.txt", 0777, 15.0);
            },
            "owner or root");
        files.copy(network, engineer, host, home + "/new.txt", home + "/copy.txt", 15.0);
        check(files.inspect(network, engineer, host, home + "/copy.txt").id != original_id,
              "Copy should have a new ID");
        files.rename(network, engineer, host, home + "/new.txt", home + "/renamed.txt", 16.0);
        check(files.inspect(network, engineer, host, home + "/renamed.txt").id == original_id,
              "Rename should preserve ID");
        rejected(
            [&] {
                files.rename(network, engineer, host, home + "/renamed.txt", "/etc/renamed.txt",
                             17.0);
            },
            "write");
        files.mkdir(network, engineer, host, home + "/folder", 17.0);
        files.write(network, engineer, host, home + "/folder/child.txt", "child", 18.0);
        rejected(
            [&] {
                files.remove(network, engineer, host, home + "/folder", 19.0);
            },
            "not empty");
        rejected(
            [&] {
                files.rename(network, engineer, host, home + "/folder", home + "/folder/nested",
                             19.0);
            },
            "into itself");
        files.rename(network, engineer, host, home + "/folder", home + "/moved", 20.0);
        check(files.read(network, engineer, host, home + "/moved/child.txt") == "child",
              "Subtree rename lost data");
        // Removing a read-only file depends on the directory, not file write bits.
        files.chmod(network, engineer, host, home + "/copy.txt", 0400, 21.0);
        files.remove(network, engineer, host, home + "/copy.txt", 22.0);
        rejected(
            [&] {
                files.inspect(network, engineer, host, home + "/copy.txt");
            },
            "No such");
        rejected(
            [&] {
                files.write(network, engineer, host, "/etc/new.txt", "bad", 23.0);
            },
            "write");
        files.mkdir(network, engineer, host, home + "/names-only", 23.0);
        files.write(network, engineer, host, home + "/names-only/visible.txt", "hidden content",
                    23.0);
        files.chmod(network, engineer, host, home + "/names-only", 0400, 23.0);
        check(files.list(network, engineer, host, home + "/names-only").size() == 1,
              "Directory read should expose names without traverse permission");
        rejected(
            [&] {
                files.enter(network, engineer, host, home + "/names-only");
            },
            "traverse");
        rejected(
            [&] {
                files.read(network, engineer, host, home + "/names-only/visible.txt");
            },
            "traverse");
        // Traverse-only directories permit known paths, but not name listing.
        files.chmod(network, engineer, host, home + "/names-only", 0100, 24.0);
        files.enter(network, engineer, host, home + "/names-only");
        rejected(
            [&] {
                files.list(network, engineer, host, home + "/names-only");
            },
            "list names");
        check(files.read(network, engineer, host, home + "/names-only/visible.txt") ==
                  "hidden content",
              "Known path should be readable through a traverse-only directory");

        // Capacity failure leaves the existing content and byte accounting intact.
        network.save_snapshot(temporary.path / "network.toml");
        auto net_document = toml::parse_file((temporary.path / "network.toml").string());
        for (auto& node : *net_document["hosts"].as_array()) {
            auto& table = *node.as_table();
            if (table["id"].value<std::string>() == host) {
                table.insert_or_assign("storage_capacity_bytes",
                                       static_cast<std::int64_t>(files.storage_used(host)));
            }
        }
        ai_sim::write_toml(temporary.path / "network.toml", net_document);
        const auto full_network = ai_sim::Network::load(temporary.path / "network.toml");
        const auto before_bytes = files.storage_used(host);
        rejected(
            [&] {
                files.write(full_network, engineer, host, home + "/renamed.txt",
                            std::string(10000, 'x'), 24.0);
            },
            "storage");
        check(files.storage_used(host) == before_bytes, "Failed write changed byte usage");
        check(files.read(network, engineer, host, home + "/renamed.txt") == "first\nsecond\n",
              "Failed write damaged content");

        world.computer.advance(11.25);
        world.computer.stop("compiler");
        world.computer.advance(5.5);
        world.paused = true;
        world.people["people"].as_array()->get(0)->as_table()->insert_or_assign(
            "notes", "persistent engineer state");
        world.emails["emails"].as_array()->push_back(
            toml::table{{"id", "email-1"}, {"body", "persistent email record"}});
        world.pending_events.push_back(toml::table{{"id", "event-1"}, {"at_seconds", 600.0}});
        ai_sim::SaveSystem saves(temporary.path / "saves");
        check(!saves.exists("first-run"), "Unexpected initial save");
        saves.save("first-run", world);
        auto restored = saves.load("first-run");
        check(restored.computer.time_seconds() == world.computer.time_seconds(),
              "Clock changed in save");
        check(restored.computer.player_groups() == world.computer.player_groups(), "Groups lost");
        check(restored.files.read(restored.network, engineer, host, home + "/renamed.txt") ==
                  "first\nsecond\n",
              "File content lost");
        check(restored.files.inspect(restored.network, engineer, host, home + "/renamed.txt").id ==
                  original_id,
              "Stable file ID lost");
        check(
            restored.files.inspect(restored.network, engineer, host, home + "/renamed.txt").mode ==
                0640,
            "Permissions lost");
        check(restored.random_state == world.random_state && restored.paused, "World state lost");
        check(restored.pending_events.size() == 1, "Pending event lost");
        check(restored.people["people"][0]["notes"].value<std::string>() ==
                  "persistent engineer state",
              "Person record lost");
        bool email_found = false;
        for (const auto& node : *restored.emails["emails"].as_array()) {
            const auto& record = *node.as_table();
            if (record["id"].value<std::string>() == "email-1")
                email_found = record["body"].value<std::string>() == "persistent email record";
        }
        check(email_found, "Email record lost");
        for (std::size_t i = 0; i < world.computer.processes().size(); ++i) {
            const auto& a = world.computer.processes()[i];
            const auto& b = restored.computer.processes()[i];
            check(a.running == b.running && a.stopped_at_seconds == b.stopped_at_seconds &&
                      a.remaining_cpu_seconds == b.remaining_cpu_seconds &&
                      a.consumed_cpu_seconds == b.consumed_cpu_seconds,
                  "Process state lost");
        }
        world.computer.advance(7.75);
        restored.computer.advance(7.75);
        check(world.computer.cpu_allocations() == restored.computer.cpu_allocations(),
              "Restored workload phase changed");
        for (std::size_t i = 0; i < world.computer.processes().size(); ++i) {
            check(std::abs(world.computer.processes()[i].consumed_cpu_seconds -
                           restored.computer.processes()[i].consumed_cpu_seconds) < 1e-9,
                  "Restored simulation diverged");
        }

        const auto pointer_path = temporary.path / "saves/first-run/manifest.toml";
        const auto before_pointer =
            toml::parse_file(pointer_path.string())["snapshot"].value<std::string>();
        auto invalid = world;
        invalid.scenario_id.clear();
        rejected(
            [&] {
                saves.save("first-run", invalid);
            },
            "scenario_id");
        check(toml::parse_file(pointer_path.string())["snapshot"].value<std::string>() ==
                  before_pointer,
              "Failed save replaced the committed manifest");
        check(saves.load("first-run").files.read(network, engineer, host, home + "/renamed.txt") ==
                  "first\nsecond\n",
              "Failed save damaged previous snapshot");
        saves.save("first-run", world);
        const auto after_pointer =
            toml::parse_file(pointer_path.string())["snapshot"].value<std::string>();
        check(before_pointer != after_pointer, "Save did not create a new generation");
        check(
            std::filesystem::exists(temporary.path / "saves/first-run/snapshots" / *before_pointer),
            "Previous snapshot removed");
        rejected(
            [&] {
                saves.save("../escape", world);
            },
            "Names may contain");
        const auto preview = saves.details("first-run");
        check(preview.day == world.computer.day_index() + 1 &&
                  preview.date == world.computer.date_text(),
              "Save preview date disagrees with saved game");
        check(preview.clock_seconds == world.computer.day_start_hour() * 3600 +
                                           static_cast<int>(world.computer.time_in_day()),
              "Save preview clock disagrees with saved game");
        rejected(
            [&] {
                (void)saves.details("../escape");
            },
            "Names may contain");
        const auto current_world_path =
            temporary.path / "saves/first-run/snapshots" / *after_pointer / "world.toml";
        auto bad_world = toml::parse_file(current_world_path.string());
        bad_world.insert_or_assign("time_seconds", 123456789.0);
        ai_sim::write_toml(current_world_path, bad_world);
        rejected(
            [&] {
                (void)saves.load("first-run");
            },
            "clock mismatch");

        std::cout << "Save and permission tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }
}
