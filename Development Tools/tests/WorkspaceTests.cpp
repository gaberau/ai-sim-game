// Verify: workspace files and migration. Fixtures isolate these behaviors from live player saves.
#include "ai_sim/SaveSystem.h"
#include "ai_sim/TomlIO.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace ai_sim;
namespace {
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
} // namespace
int main(int argc, char** argv) {
    const auto temp = std::filesystem::temp_directory_path() /
                      ("ai-workspace-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        check(argc == 2, "Root required");
        std::filesystem::create_directories(temp);
        auto world =
            SaveSystem::new_game(std::filesystem::path(argv[1]) / "data/scenarios/hobbyist.toml");
        const Identity actor{world.computer.player_user(), world.computer.player_groups()};
        const auto source = world.network.hosts()[world.network.local_host_index()].id;
        for (const auto* folder : {"agents", "jobs", "products", "documents", "backups"})
            world.files.enter(world.network, actor, source, world.files.workspace_path(folder));
        world.files.save_snapshot(temp / "files.toml");
        auto config = toml::parse_file((temp / "files.toml").string());
        config["filesystem"].as_table()->insert_or_assign("workspace_root",
                                                          "/home/engineer/custom-ai");
        write_toml(temp / "custom.toml", config);
        auto files = FileSystem::load(temp / "custom.toml", world.network);
        files.ensure_workspace(world.network, actor, source, 0);
        check(files.agent_path("test") == "/home/engineer/custom-ai/agents/agent-test.allocation",
              "Agent path ignored configuration");
        const auto managed = files.agent_path("test");
        files.reserve_service_file(world.network, actor, source, managed, 3000000, 0, "agent");
        const auto service_id = files.inspect(world.network, actor, source, managed).id;
        rejects([&] {
            files.write(world.network, Identity{"root"}, source, managed, "shrunk", 1);
        });
        rejects([&] {
            files.remove(world.network, Identity{"root"}, source, managed, 1);
        });
        rejects([&] {
            files.rename(world.network, actor, source, files.workspace_root(),
                         "/home/engineer/moved", 1);
        });
        rejects([&] {
            files.copy(world.network, actor, source, managed,
                       files.workspace_path("documents") + "/fake", 1);
        });
        rejects([&] {
            files.chmod(world.network, actor, source, managed, 0777, 1);
        });
        const auto document = files.workspace_path("documents") + "/project.txt";
        files.write(world.network, actor, source, document, "project notes", 1);
        files.chmod(world.network, actor, source, document, 0640, 1);
        files.set_category(world.network, actor, source, document, "personal");
        const auto document_id = files.inspect(world.network, actor, source, document).id;
        files.mkdir(world.network, actor, source, files.workspace_path("jobs") + "/client", 1);
        files.write(world.network, actor, source,
                    files.workspace_path("jobs") + "/client/output.txt", "job output", 1);
        check(files.search(world.network, actor, source, files.workspace_path("jobs"), "OUTPUT")
                      .size() == 1,
              "Scoped search missed nested output");
        check(
            files.search(world.network, actor, source, files.workspace_path("documents"), "output")
                .empty(),
            "Search escaped its folder");
        NetworkHost server;
        server.id = "workspace-server";
        server.name = "Workspace server";
        server.kind = "server";
        server.address = "server.sim";
        server.known = server.online = server.access_granted = true;
        server.storage_capacity_bytes = 10000000;
        world.network.add_owned_host(server, source);
        files.mount_host(world.network, server.id, actor.user, 1);
        auto small_server = server;
        small_server.id = "small-server";
        small_server.storage_capacity_bytes = 1;
        world.network.add_owned_host(small_server, source);
        files.mount_host(world.network, small_server.id, actor.user, 1);
        rejects([&] {
            files.transfer_workspace(world.network, actor, source, small_server.id, 1);
        });
        check(files.read(world.network, actor, source, document) == "project notes" &&
                  files.storage_used(small_server.id) == 0,
              "Storage shortage partially moved workspace files");
        auto conflicted = files;
        conflicted.ensure_workspace(world.network, actor, server.id, 1);
        conflicted.write(world.network, actor, server.id, document, "existing server document", 1);
        rejects([&] {
            conflicted.transfer_workspace(world.network, actor, source, server.id, 1);
        });
        check(conflicted.read(world.network, actor, source, document) == "project notes" &&
                  conflicted.read(world.network, actor, server.id, document) ==
                      "existing server document",
              "Failed transfer changed live files");
        files.transfer_workspace(world.network, actor, source, server.id, 1);
        const auto& moved = files.inspect(world.network, actor, server.id, document);
        check(moved.id == document_id && moved.content == "project notes" && moved.mode == 0640 &&
                  moved.category == "personal",
              "Transfer lost document metadata");
        check(files.inspect(world.network, actor, server.id, managed).id == service_id &&
                  files.inspect(world.network, actor, server.id, managed).size_bytes == 3000000,
              "Transfer lost service ID or allocation");
        rejects([&] {
            files.inspect(world.network, actor, source, document);
        });
        files.save_snapshot(temp / "transferred.toml");
        files = FileSystem::load(temp / "transferred.toml", world.network);
        check(files.workspace_root() == "/home/engineer/custom-ai",
              "Save lost configured workspace");
        files.release_service_file(world.network, actor, server.id, managed, 1);
        // Legacy untyped allocations migrate once, preserving their identity and size.
        auto legacy = world.files;
        const auto old_path = legacy.workspace_root() + "/agent-legacy.allocation";
        legacy.reserve_file(world.network, actor, source, old_path, 123456, 1);
        const auto old_id = legacy.inspect(world.network, actor, source, old_path).id;
        legacy.save_snapshot(temp / "legacy.toml");
        auto migrated = FileSystem::load(temp / "legacy.toml", world.network);
        const auto& converted =
            migrated.inspect(world.network, actor, source, migrated.agent_path("legacy"));
        check(converted.id == old_id && converted.size_bytes == 123456 &&
                  converted.service_kind == "agent",
              "Legacy allocation migration lost metadata");
        std::filesystem::remove_all(temp);
        std::cout << "Workspace configuration, protection, transfer, search and migration passed\n";
    } catch (const std::exception& error) {
        std::filesystem::remove_all(temp);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
