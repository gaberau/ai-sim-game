// Verify: definitions and migration. Fixtures isolate these behaviors from live player saves.
#include "ai_sim/DefinitionLoader.h"
#include "ai_sim/SaveSystem.h"
#include "ai_sim/TomlIO.h"
#include <chrono>
#include <iostream>
#include <fstream>
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
void write(const std::filesystem::path& p, const toml::table& data) {
    std::filesystem::create_directories(p.parent_path());
    ai_sim::write_toml(p, data);
}
std::string bytes(const std::filesystem::path& p) {
    std::ifstream f(p);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}
} // namespace
int main(int argc, char** argv) {
    const auto temp = std::filesystem::temp_directory_path() /
                      ("ai-sim-definition-tests-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        using namespace ai_sim;
        check(argc == 2, "Project root required");
        std::filesystem::create_directories(temp);
        const auto data = std::filesystem::path(argv[1]) / "data";
        const auto defs = ScenarioDefinition::load(data / "scenarios/hobbyist.toml");
        auto current = SaveSystem::new_game(defs.source);
        check(current.computer.processes().size() == 9, "Application startup list changed");
        const auto local = temp / "templates";
        std::filesystem::copy(data, local, std::filesystem::copy_options::recursive);
        auto computer = DefinitionLoader::load(local / "computers/engineer_pc.toml");
        auto& first = *(*computer["processes"].as_array())[0].as_table();
        first.insert_or_assign("details",
                               toml::table{{"name", "Custom display name"}, {"cpu_limit", 1.0}});
        write(local / "computers/engineer_pc.toml", computer);
        const auto expanded = DefinitionLoader::computer(local / "computers/engineer_pc.toml");
        const auto& instance = *(*expanded["processes"].as_array())[0].as_table();
        check(instance["details"]["name"].value<std::string>() == "Custom display name",
              "Instance override lost");
        check(instance["details"]["program_id"].value<std::string>() == "custom_email_assistant",
              "Template fields lost during nested override");
        check(!instance.contains("application"), "Runtime snapshot still needs a template file");
        write(temp / "cycle/a.toml",
              toml::table{{"schema_version", 1}, {"sources", toml::array{"b.toml"}}});
        write(temp / "cycle/b.toml",
              toml::table{{"schema_version", 1}, {"sources", toml::array{"a.toml"}}});
        fails([&] {
            DefinitionLoader::load(temp / "cycle/a.toml");
        });
        write(temp / "missing.toml",
              toml::table{{"schema_version", 1}, {"sources", toml::array{"absent.toml"}}});
        fails([&] {
            DefinitionLoader::load(temp / "missing.toml");
        });
        write(temp / "conflict/source.toml", toml::table{{"schema_version", 2}});
        write(temp / "conflict/root.toml",
              toml::table{{"schema_version", 1}, {"sources", toml::array{"source.toml"}}});
        fails([&] {
            DefinitionLoader::load(temp / "conflict/root.toml");
        });
        auto network = DefinitionLoader::load(defs.network);
        network["hosts"].as_array()->push_back((*network["hosts"].as_array())[0]);
        write(temp / "duplicates.toml", network);
        fails([&] {
            Network::load(temp / "duplicates.toml");
        });
        const auto project = temp / "migration";
        const auto old = project / "data/scenarios/hobbyist";
        auto oldcomputer = DefinitionLoader::computer(defs.computer);
        oldcomputer["computer"].as_table()->insert_or_assign("name", "My edited workstation");
        write(old / "computer.toml", oldcomputer);
        write(old / "network.toml", DefinitionLoader::load(defs.network));
        auto oldfiles = DefinitionLoader::load(defs.files);
        for (auto& n : *oldfiles["entries"].as_array())
            if ((*n.as_table())["path"].value<std::string>() == "/home/engineer/notes.txt")
                n.as_table()->insert_or_assign("content", "My custom notes");
        write(old / "files.toml", oldfiles);
        write(old / "people.toml", DefinitionLoader::load(defs.people));
        write(old / "emails.toml", DefinitionLoader::load(defs.emails));
        auto manifest = defs.manifest;
        manifest.erase("definitions");
        write(old / "scenario.toml", manifest);
        auto work = DefinitionLoader::load(defs.workday);
        const auto document = DefinitionLoader::load(defs.workday.parent_path() /
                                                     work["document"].value_or(std::string{}));
        work.insert_or_assign("document", *(*document["entries"].as_array())[0].as_table());
        write(old / "workday.toml", work);
        write(project / "data/news.toml", DefinitionLoader::load(defs.news));
        write(project / "data/custom/sentinel.toml", toml::table{{"user_edit", "retained"}});
        const auto original = bytes(old / "computer.toml");
        auto legacy = SaveSystem::new_game(old / "computer.toml");
        SaveSystem saves(project / "saves");
        saves.save("autosave", legacy);
        const auto saved_id = legacy.files
                                  .inspect(legacy.network, Identity{"root"}, "engineer_pc",
                                           "/home/engineer/notes.txt")
                                  .id;
        check(DefinitionLoader::migrate_legacy(project / "data"), "Legacy layout wasn't migrated");
        check(!std::filesystem::exists(old) && !std::filesystem::exists(project / "data/news.toml"),
              "Legacy clutter left active");
        check(bytes(project / "data/custom/sentinel.toml").find("retained") != std::string::npos,
              "Unrelated data file lost");
        const auto backup = *std::filesystem::directory_iterator(project / "backups");
        check(bytes(backup.path() / "scenarios/hobbyist/computer.toml") == original,
              "Original backup changed");
        const auto migrated = SaveSystem::new_game(project / "data/scenarios/hobbyist.toml");
        check(migrated.computer.name() == "My edited workstation", "Migration lost hardware edit");
        check(migrated.files.read(migrated.network, Identity{"root"}, "engineer_pc",
                                  "/home/engineer/notes.txt") == "My custom notes",
              "Migration lost file content");
        const auto restored = saves.load("autosave");
        check(restored.files
                      .inspect(restored.network, Identity{"root"}, "engineer_pc",
                               "/home/engineer/notes.txt")
                      .id == saved_id,
              "Migration changed saved identity");
        check(!DefinitionLoader::migrate_legacy(project / "data"), "Migration wasn't idempotent");
        const auto bad = temp / "bad/data/scenarios/hobbyist";
        std::filesystem::create_directories(bad.parent_path());
        std::filesystem::copy(backup.path() / "scenarios/hobbyist", bad,
                              std::filesystem::copy_options::recursive);
        auto broken = toml::parse_file((bad / "computer.toml").string());
        broken["computer"].as_table()->insert_or_assign("cpu_cores", -1);
        write(bad / "computer.toml", broken);
        const auto before = bytes(bad / "computer.toml");
        fails([&] {
            DefinitionLoader::migrate_legacy(temp / "bad/data");
        });
        check(bytes(bad / "computer.toml") == before &&
                  !std::filesystem::exists(temp / "bad/backups"),
              "Rejected migration altered legacy data");
        std::filesystem::remove_all(temp);
        std::cout << "Definition and migration tests passed\n";
    } catch (const std::exception& e) {
        std::filesystem::remove_all(temp);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
