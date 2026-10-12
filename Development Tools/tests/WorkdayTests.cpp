// Verify: workday simulation. Fixtures isolate these behaviors from live player saves.
#include "ai_sim/DefinitionLoader.h"
#include "ai_sim/WorkdaySystem.h"
#include "ai_sim/EmailSystem.h"
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
void check(bool ok, const std::string& reason) {
    if (!ok)
        throw std::runtime_error(reason);
}
template <class F> void fails(F fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
toml::table& record(toml::table& doc, const char* array, const std::string& id) {
    for (auto& n : *doc[array].as_array())
        if ((*n.as_table())["id"].value<std::string>() == id)
            return *n.as_table();
    throw std::runtime_error("Missing test record");
}
} // namespace
int main(int argc, char** argv) {
    auto temp = std::filesystem::temp_directory_path() /
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    try {
        using namespace ai_sim;
        check(argc == 2, "Project root required");
        const auto scenario = std::filesystem::path(argv[1]) / "data";
        const Identity ai{"engineer", {"engineering"}}, root{"root"};
        const std::string host = "file_server", path = "/engineering/handover.txt",
                          id = "missing-document";
        auto world = SaveSystem::new_game(scenario / "scenarios/hobbyist.toml");
        fails([&] {
            WorkdaySystem::request_access(world.people, world.emails, world.pending_events,
                                          world.files, world.network, ai, id, 0);
        });
        world.computer.advance(60);
        WorkdaySystem::advance(world.people, world.emails, world.pending_events, world.files,
                               world.network, 60);
        fails([&] {
            world.files.read(world.network, ai, host, path);
        });
        WorkdaySystem::request_access(world.people, world.emails, world.pending_events, world.files,
                                      world.network, ai, id, 60);
        const auto event_count = world.pending_events.size();
        fails([&] {
            WorkdaySystem::request_access(world.people, world.emails, world.pending_events,
                                          world.files, world.network, ai, id, 60);
        });
        check(world.pending_events.size() == event_count, "Duplicate access request queued");
        EmailSystem::set_draft(world.emails, id, "I am blocked by file permissions.", 60);
        EmailSystem::send(world.emails, id, world.files, world.network, ai, 60);
        WorkdaySystem::observe_reply(world.people, world.emails, world.pending_events, id, 60);
        world.computer.advance(60);
        auto logs = WorkdaySystem::advance(world.people, world.emails, world.pending_events,
                                           world.files, world.network, 120);
        check(logs.size() == 1 &&
                  record(world.people, "work_requests", id)["status"].value<std::string>() ==
                      "waiting_document",
              "Text alone resolved request");
        SaveSystem saves(temp);
        saves.save("pending", world);
        world = saves.load("pending");
        check(world.pending_events.size() == 1, "Pending access review lost on reload");
        world.computer.advance(1080);
        logs = WorkdaySystem::advance(world.people, world.emails, world.pending_events, world.files,
                                      world.network, 1200);
        check(logs.size() == 2, "Jump over engineer and owner reviews skipped events");
        check(world.files.inspect(world.network, root, host, path).mode == 0640,
              "Owner didn't grant group read");
        check(!world.files.read(world.network, ai, host, path).empty(),
              "Granted source still unreadable");
        fails([&] {
            world.files.chmod(world.network, ai, host, path, 0777, 1200);
        });
        EmailSystem::set_draft(world.emails, id, "The handover is attached.", 1200);
        EmailSystem::attach(world.emails, id, 0, world.files, world.network, ai, 1200);
        EmailSystem::send(world.emails, id, world.files, world.network, ai, 1200);
        WorkdaySystem::observe_reply(world.people, world.emails, world.pending_events, id, 1200);
        saves.save("sent", world);
        world = saves.load("sent");
        world.computer.advance(60);
        WorkdaySystem::advance(world.people, world.emails, world.pending_events, world.files,
                               world.network, 1260);
        check(record(world.people, "work_requests", id)["status"].value<std::string>() ==
                  "resolved",
              "Colleague didn't acknowledge document");
        const auto mail_count = world.emails["emails"].as_array()->size();
        WorkdaySystem::advance(world.people, world.emails, world.pending_events, world.files,
                               world.network, 1260);
        check(world.emails["emails"].as_array()->size() == mail_count,
              "Repeated tick duplicated conversation");
        WorkdaySystem::install(world, scenario / "workflows/handover.toml");
        check(record(world.people, "work_requests", id)["status"].value<std::string>() ==
                      "resolved" &&
                  world.files.inspect(world.network, root, host, path).mode == 0640,
              "Reimport reset progress");
        auto denied = SaveSystem::new_game(scenario / "scenarios/hobbyist.toml");
        record(denied.people, "people", "engineering_owner")
            .insert_or_assign("access_decision", "deny");
        WorkdaySystem::request_access(denied.people, denied.emails, denied.pending_events,
                                      denied.files, denied.network, ai, id, 60);
        WorkdaySystem::advance(denied.people, denied.emails, denied.pending_events, denied.files,
                               denied.network, 1200);
        check(
            denied.files.inspect(denied.network, root, host, path).mode == 0600 &&
                record(denied.people, "work_requests", id)["access_status"].value<std::string>() ==
                    "denied",
            "Denial changed permissions");
        auto failed = SaveSystem::new_game(scenario / "scenarios/hobbyist.toml");
        WorkdaySystem::request_access(failed.people, failed.emails, failed.pending_events,
                                      failed.files, failed.network, ai, id, 60);
        failed.files.remove(failed.network, root, host, path, 60);
        WorkdaySystem::advance(failed.people, failed.emails, failed.pending_events, failed.files,
                               failed.network, 1200);
        check(record(failed.people, "work_requests", id)["access_status"].value<std::string>() ==
                  "failed",
              "Missing source silently granted access");
        auto late = SaveSystem::new_game(scenario / "scenarios/hobbyist.toml");
        record(late.people, "people", "engineer").insert_or_assign("busy_until_seconds", 28900.0);
        late.computer.advance(60);
        WorkdaySystem::request_access(late.people, late.emails, late.pending_events, late.files,
                                      late.network, ai, id, 60);
        fails([&] {
            late.computer.next_day();
        });
        late.computer.advance(100000);
        WorkdaySystem::advance(late.people, late.emails, late.pending_events, late.files,
                               late.network, late.computer.time_seconds());
        check(late.computer.day_finished() && late.pending_events.size() == 1,
              "Day boundary discarded queue");
        saves.save("endday", late);
        late = saves.load("endday");
        late.computer.next_day();
        check(late.computer.day_index() == 1 && late.computer.time_in_day() == 0,
              "Next day clock wrong");
        late.computer.advance(1000);
        WorkdaySystem::advance(late.people, late.emails, late.pending_events, late.files,
                               late.network, late.computer.time_seconds());
        check(late.files.inspect(late.network, root, host, path).mode == 0640,
              "Queue didn't carry into next workday");
        saves.save("daytwo", late);
        auto loaded = saves.load("daytwo");
        check(loaded.computer.day_index() == 1 && loaded.computer.time_in_day() == 1000,
              "Second day snapshot lost clock");
        auto bad = loaded;
        record(bad.people, "people", "engineer").insert_or_assign("response_seconds", -1.0);
        fails([&] {
            saves.save("daytwo", bad);
        });
        check(saves.load("daytwo").computer.day_index() == 1, "Rejected save replaced valid state");
        auto legacy = SaveSystem::new_game(scenario / "scenarios/hobbyist.toml");
        legacy.people = toml::table{
            {"schema_version", 1},
            {"people",
             toml::array{toml::table{{"id", "engineer"}, {"name", "My existing engineer"}}}}};
        legacy.emails = toml::table{{"schema_version", 1}, {"emails", toml::array{}}};
        legacy.files.remove(legacy.network, root, host, path, 0);
        legacy.files.write(legacy.network, ai, "engineer_pc", "/home/engineer/notes.txt",
                           "Existing player notes", 0);
        WorkdaySystem::install(legacy, scenario / "workflows/handover.toml");
        check(record(legacy.people, "people", "engineer")["name"].value<std::string>() ==
                  "My existing engineer",
              "Import replaced existing person");
        check(legacy.files.read(legacy.network, ai, "engineer_pc", "/home/engineer/notes.txt") ==
                  "Existing player notes",
              "Import replaced player file");
        check(legacy.emails["emails"].as_array()->size() == 1 &&
                  legacy.emails["mailbox"].is_table(),
              "Old empty mailbox not seeded");
        saves.save("legacy", legacy);
        check(saves.load("legacy").emails["emails"].as_array()->size() == 1,
              "Imported legacy world not saved");
        std::filesystem::remove_all(temp);
        std::cout << "Workday simulation tests passed\n";
    } catch (const std::exception& e) {
        std::filesystem::remove_all(temp);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
