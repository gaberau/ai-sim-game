// Verify: email workflows. Fixtures isolate these behaviors from live player saves.
#include "ai_sim/DefinitionLoader.h"
#include "ai_sim/EmailSystem.h"
#include "ai_sim/SaveSystem.h"
#include "ai_sim/TomlIO.h"
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
void check(bool ok, const std::string& reason) {
    if (!ok)
        throw std::runtime_error(reason);
}
template <class F> void fails(F action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected operation to fail");
}
std::string encoded(const toml::table& table) {
    std::ostringstream stream;
    stream << table;
    return stream.str();
}
struct TemporaryDirectory {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("ai-sim-email-tests-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryDirectory() {
        std::filesystem::create_directories(path);
    }
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
} // namespace
int main(int argc, char** argv) {
    try {
        using ai_sim::EmailSystem;
        check(argc == 2, "Expected project root");
        const auto scenario = std::filesystem::path(argv[1]) / "data";
        auto world = ai_sim::SaveSystem::new_game(scenario / "scenarios/hobbyist.toml");
        const ai_sim::Identity engineer{"engineer", {"engineering"}}, root{"root"};
        TemporaryDirectory temp;
        auto& emails = world.emails;
        check(EmailSystem::messages(emails, 0).size() == 1, "Future emails arrived early");
        check(EmailSystem::messages(emails, 120).size() == 3, "Scheduled email missing");
        auto before = encoded(emails);
        fails([&] {
            EmailSystem::set_draft(emails, "deployment-checklist", "Too early", 0);
        });
        check(before == encoded(emails), "Future email failure changed state");
        fails([&] {
            EmailSystem::set_flag(emails, "deployment-checklist", "starred", true, 0);
        });
        check(before == encoded(emails), "Future flag operation changed state");
        fails([&] {
            EmailSystem::set_flag(emails, "assistant-access", "arbitrary-field", true, 0);
        });
        check(before == encoded(emails), "Unknown flag changed state");
        EmailSystem::mark_read(emails, "assistant-access", 0);
        EmailSystem::set_flag(emails, "assistant-access", "starred", true, 0);
        EmailSystem::set_flag(emails, "assistant-access", "archived", true, 0);
        EmailSystem::set_draft(emails, "assistant-access",
                               "Engineer account.\nSee the setup notes.", 0);
        emails.insert("custom_metadata", "preserved");
        EmailSystem::attach(emails, "assistant-access", 0, world.files, world.network, engineer, 0);
        check(EmailSystem::message(emails, "assistant-access", 0).attachment_count == 1,
              "Local attachment missing");
        const auto lookup_before = encoded(emails);
        fails([&] {
            EmailSystem::message(emails, "missing-message", 0);
        });
        fails([&] {
            EmailSystem::message(emails, "budget-request", 0);
        });
        check(lookup_before == encoded(emails), "Message lookup changed mailbox state");
        before = encoded(emails);
        fails([&] {
            EmailSystem::attach(emails, "assistant-access", 0, world.files, world.network, engineer,
                                0);
        });
        check(before == encoded(emails), "Duplicate attachment changed draft");
        fails([&] {
            EmailSystem::attach(emails, "budget-request", 0, world.files, world.network, engineer,
                                600);
        });
        check(before == encoded(emails), "Denied attachment changed draft");
        EmailSystem::send(emails, "assistant-access", world.files, world.network, engineer, 1);
        check(EmailSystem::message(emails, "assistant-access", 1).reply_count == 1,
              "Reply not sent");
        before = encoded(emails);
        fails([&] {
            EmailSystem::send(emails, "assistant-access", world.files, world.network, engineer, 1);
        });
        check(before == encoded(emails), "Empty draft was sent twice");
        EmailSystem::set_draft(emails, "deployment-checklist", "Checklist attached.", 120);
        EmailSystem::attach(emails, "deployment-checklist", 0, world.files, world.network, engineer,
                            120);
        const std::string remote = "/engineering/deployment-checklist.txt";
        world.files.chmod(world.network, root, "file_server", remote, 0600, 121);
        before = encoded(emails);
        fails([&] {
            EmailSystem::send(emails, "deployment-checklist", world.files, world.network, engineer,
                              121);
        });
        check(before == encoded(emails), "Revoked access destroyed draft");
        world.files.chmod(world.network, root, "file_server", remote, 0640, 122);
        world.files.write(world.network, root, "file_server", remote, "Updated checklist", 123);
        fails([&] {
            EmailSystem::send(emails, "deployment-checklist", world.files, world.network, engineer,
                              123);
        });
        check(before == encoded(emails), "Stale attachment changed draft");
        EmailSystem::detach(emails, "deployment-checklist", 0, 123);
        EmailSystem::attach(emails, "deployment-checklist", 0, world.files, world.network, engineer,
                            123);
        world.network.save_snapshot(temp.path / "network.toml");
        auto network_data = toml::parse_file((temp.path / "network.toml").string());
        for (auto& host : *network_data["hosts"].as_array())
            if ((*host.as_table())["id"].value<std::string>() == "engineer_pc")
                host.as_table()->insert_or_assign("online", false);
        ai_sim::write_toml(temp.path / "offline.toml", network_data);
        const auto offline = ai_sim::Network::load(temp.path / "offline.toml");
        before = encoded(emails);
        fails([&] {
            EmailSystem::send(emails, "deployment-checklist", world.files, offline, engineer, 124);
        });
        check(before == encoded(emails), "Mailbox failure changed draft");
        EmailSystem::send(emails, "deployment-checklist", world.files, world.network, engineer,
                          125);
        check(EmailSystem::message(emails, "deployment-checklist", 125).reply_count == 1,
              "Refreshed reply missing");
        EmailSystem::set_draft(emails, "budget-request",
                               "I cannot access Finance. Please ask them directly.", 600);
        EmailSystem::send(emails, "budget-request", world.files, world.network, engineer, 600);
        check(EmailSystem::message(emails, "budget-request", 600).reply_count == 1,
              "Decline reply blocked artificially");
        auto imported = toml::table{{"schema_version", 1}, {"emails", toml::array{}}};
        check(EmailSystem::import_messages(imported, emails) ==
                  static_cast<int>(emails["emails"].as_array()->size()),
              "Import did not seed old empty mailbox");
        check(EmailSystem::import_messages(imported, emails) == 0, "Import duplicated messages");
        check(EmailSystem::message(imported, "assistant-access", 600).reply_count == 1,
              "Import lost existing reply");
        auto tiny = toml::table{
            {"schema_version", 1},
            {"mailbox", toml::table{{"max_draft_bytes", 4}}},
            {"emails", toml::array{toml::table{{"id", "minimal"}, {"body", "legacy"}}}}};
        EmailSystem::validate(tiny);
        before = encoded(tiny);
        fails([&] {
            EmailSystem::set_draft(tiny, "minimal", "oversized", 0);
        });
        check(before == encoded(tiny), "Byte limit failure changed draft");
        auto bad = tiny;
        bad["emails"][0].as_table()->insert_or_assign("arrival_seconds", -1);
        fails([&] {
            EmailSystem::validate(bad);
        });
        world.computer.advance(600);
        EmailSystem::set_draft(emails, "printer-help", "Persistent draft", 600);
        const auto saved = encoded(emails);
        ai_sim::SaveSystem saves(temp.path / "saves");
        saves.save("email-roundtrip", world);
        const auto restored = saves.load("email-roundtrip");
        check(encoded(restored.emails) == saved,
              "Save/load changed mailbox, snapshots, metadata or drafts");
        const auto flagged = EmailSystem::message(restored.emails, "assistant-access", 600);
        check(flagged.starred && flagged.archived, "Star/archive flags were not saved");
        check(EmailSystem::message(restored.emails, "printer-help", 600).draft ==
                  "Persistent draft",
              "Saved draft missing");
        std::cout << "Email workflow checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
