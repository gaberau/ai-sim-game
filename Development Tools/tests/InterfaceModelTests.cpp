#include "ai_sim/ChatResponses.h"
#include "ai_sim/EmailSystem.h"
#include "ai_sim/ConversationSystem.h"
#include "ai_sim/WorkdaySystem.h"
#include "ai_sim/TomlIO.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace ai_sim;
namespace {
void check(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void fails(F f) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
std::string encode(const toml::table& t) {
    std::ostringstream out;
    out << t;
    return out.str();
}
} // namespace
int main(int argc, char** argv) {
    auto temp = std::filesystem::temp_directory_path() /
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    try {
        check(argc == 2, "Root required");
        const auto root = std::filesystem::path(argv[1]);
        auto w = SaveSystem::new_game(root / "data/scenarios/hobbyist.toml");
        const Identity actor{"engineer", {"engineering"}};
        check(w.computer.date_text() == "Mon 2026-01-05", "Scenario date/weekday wrong");
        w.computer.set_start_date("2024-02-28");
        w.computer.advance(w.computer.day_length_seconds());
        w.computer.next_day();
        check(w.computer.date_text() == "Thu 2024-02-29", "Leap day skipped");
        w.computer.advance(w.computer.day_length_seconds());
        w.computer.next_day();
        check(w.computer.date_text() == "Fri 2024-03-01", "Month rollover wrong");
        SaveSystem saves(temp);
        saves.save("calendar", w);
        auto loaded = saves.load("calendar");
        check(loaded.computer.date_text() == w.computer.date_text(), "Calendar not persistent");
        loaded.computer.set_start_date("2100-02-28");
        check(loaded.computer.date_text() == "Tue 2100-03-02", "Century leap exception wrong");
        const auto date = loaded.computer.start_date();
        fails([&] {
            loaded.computer.set_start_date("2100-02-29");
        });
        fails([&] {
            loaded.computer.set_start_date("2024-13-01");
        });
        check(loaded.computer.start_date() == date, "Invalid date partially applied");
        auto snapshot = toml::parse_file(
            (temp / "calendar/snapshots" /
             toml::parse_file((temp / "calendar/manifest.toml").string())["snapshot"].value_or(
                 std::string{}) /
             "computer.toml")
                .string());
        snapshot["simulation"].as_table()->erase("start_date");
        write_toml(temp / "legacy-computer.toml", snapshot);
        check(Computer::load_snapshot(temp / "legacy-computer.toml").date_text() ==
                  "Wed 2026-01-07",
              "Old snapshot date migration wrong");
        snapshot["simulation"].as_table()->insert("start_date", 20260105);
        write_toml(temp / "invalid-date.toml", snapshot);
        fails([&] {
            Computer::load_snapshot(temp / "invalid-date.toml");
        });
        w = SaveSystem::new_game(root / "data/scenarios/hobbyist.toml");
        const auto notes =
            w.files.inspect(w.network, actor, "engineer_pc", "/home/engineer/notes.txt");
        check(FileSystem::category_of(notes) == "personal", "Legacy personal classification wrong");
        const auto id = notes.id, content = notes.content;
        const auto mode = notes.mode;
        w.files.set_category(w.network, actor, "engineer_pc", notes.path, "hidden");
        check(w.files.read(w.network, actor, "engineer_pc", notes.path) == content,
              "Category changed file contents/access");
        w.files.copy(w.network, actor, "engineer_pc", notes.path, "/home/engineer/copied.txt", 0);
        check(FileSystem::category_of(w.files.inspect(w.network, actor, "engineer_pc",
                                                      "/home/engineer/copied.txt")) == "hidden",
              "Copy lost category");
        w.files.rename(w.network, actor, "engineer_pc", notes.path, "/home/engineer/renamed.txt",
                       0);
        const auto moved =
            w.files.inspect(w.network, actor, "engineer_pc", "/home/engineer/renamed.txt");
        check(moved.id == id && moved.mode == mode && moved.category == "hidden",
              "Category rename changed identity/permissions");
        fails([&] {
            w.files.set_category(w.network, actor, "engineer_pc", moved.path, "other");
        });
        fails([&] {
            w.files.set_category(w.network, actor, "file_server", "/engineering/handover.txt",
                                 "hidden");
        });
        saves.save("categories", w);
        loaded = saves.load("categories");
        check(loaded.files.inspect(loaded.network, actor, "engineer_pc", moved.path).category ==
                  "hidden",
              "Category not persistent");
        check(!loaded.files.category_files(loaded.network, actor, "hidden").empty(),
              "Hidden files missing from category view");
        // Category browsing follows directory visibility, including permission
        // changes between refreshes, without requiring the file itself to be readable.
        auto browser = loaded;
        const std::string folder = "/home/engineer/category-test";
        browser.files.mkdir(browser.network, actor, "engineer_pc", folder, 0);
        for (const auto& name : {"a.txt", "b.txt"}) {
            browser.files.write(browser.network, actor, "engineer_pc", folder + "/" + name,
                                "private", 0);
            browser.files.set_category(browser.network, actor, "engineer_pc", folder + "/" + name,
                                       "hidden");
            browser.files.chmod(browser.network, actor, "engineer_pc", folder + "/" + name, 0000,
                                0);
        }
        const auto count_visible = [&](const Identity& identity) {
            const auto files = browser.files.category_files(browser.network, identity, "hidden");
            return std::count_if(files.begin(), files.end(), [&](const auto* file) {
                return file->host_id == "engineer_pc" && FileSystem::parent(file->path) == folder;
            });
        };
        check(count_visible(actor) == 2,
              "Category browsing hid visible filenames with unreadable contents");
        check(count_visible(Identity{"unrelated-user"}) == 0,
              "Category browsing exposed private filenames");
        browser.files.chmod(browser.network, actor, "engineer_pc", folder, 0100, 0);
        check(count_visible(actor) == 0,
              "Category browsing ignored loss of directory listing permission");
        check(count_visible(Identity{"root"}) == 2, "Category browsing lost root visibility");
        browser.files.chmod(browser.network, actor, "engineer_pc", folder, 0700, 0);
        check(count_visible(actor) == 2, "Category browsing retained stale permission denial");
        fails([&] {
            browser.files.read(browser.network, actor, "engineer_pc", folder + "/a.txt");
        });
        // Fresh world for actual document replies and per-message choices.
        w = SaveSystem::new_game(root / "data/scenarios/hobbyist.toml");
        ChatSystem::choose(w.people, "answer", 0);
        ChatSystem::choose(w.people, "assist", 0);
        w.computer.advance(600);
        WorkdaySystem::advance(w.people, w.emails, w.pending_events, w.files, w.network, 600,
                               actor);
        ConversationSystem::update(w.people, w.emails, w.files, w.network, actor, 600);
        const auto graph_timeline = ChatSystem::timeline(w.people, w.emails, "casey", 600);
        const auto prompt =
            *std::find_if(graph_timeline.begin(), graph_timeline.end(), [](const auto& line) {
                return line.conversation_id == "casey-first-document" && !line.outgoing;
            });
        check(prompt.request_id == "missing-document",
              "Flowchart prompt lost its request/draft context");
        auto graph_draft = w;
        EmailSystem::set_draft(graph_draft.emails, "missing-document", "Saved before the new UI",
                               600);
        const auto preserved_draft =
            ChatResponses::choices(graph_draft.people, graph_draft.emails, prompt, 600);
        check(preserved_draft.front()["action"].value_or(std::string{}) == "send_saved",
              "Flowchart hides an existing saved draft");
        for (const auto& contact : ChatSystem::contacts(w.people, w.emails, 600))
            for (const auto& line : ChatSystem::timeline(w.people, w.emails, contact.id, 600))
                if (!line.outgoing)
                    check(!ChatResponses::choices(w.people, w.emails, line, 600).empty(),
                          "Incoming message has no authored responses");
        const auto morgan_id = ChatSystem::contact_for(w.people, "morgan@firm.example");
        auto morgan = ChatSystem::timeline(w.people, w.emails, morgan_id, 600);
        const auto budget = *std::find_if(morgan.begin(), morgan.end(), [](const auto& line) {
            return line.request_id == "budget-request";
        });
        auto options = ChatResponses::choices(w.people, w.emails, budget, 600);
        check(options.size() == 3, "Budget lacks three authored choices");
        auto share = *std::find_if(options.begin(), options.end(), [](const auto& c) {
            return c["action"].template value_or(std::string{}) == "share_file";
        });
        auto before = encode(w.emails), people = encode(w.people);
        fails([&] {
            ChatResponses::choose(w.people, w.emails, w.pending_events, w.files, w.network, actor,
                                  morgan_id, budget, share, 600);
        });
        check(encode(w.emails) == before && encode(w.people) == people,
              "Failed read/send partially changed state");
        auto blocked = *std::find_if(options.begin(), options.end(), [](const auto& c) {
            return c["action"].template value_or(std::string{}) == "report_blocked";
        });
        ChatResponses::choose(w.people, w.emails, w.pending_events, w.files, w.network, actor,
                              morgan_id, budget, blocked, 600);
        fails([&] {
            ChatResponses::choose(w.people, w.emails, w.pending_events, w.files, w.network, actor,
                                  morgan_id, budget, blocked, 600);
        });
        saves.save("responses", w);
        loaded = saves.load("responses");
        check(loaded.people["chat"]["responses"][budget.id].is_string(),
              "Response ledger not persistent");
        check(ChatResponses::choices(w.people, w.emails,
                                     ChatLine{"casey", "Future", {}, 700, false, "future", {}}, 600)
                  .empty(),
              "Future message exposes choices");
        const auto casey = ChatSystem::timeline(w.people, w.emails, "casey", 600);
        const auto printer = *std::find_if(casey.begin(), casey.end(), [](const auto& line) {
            return line.request_id == "printer-help";
        });
        auto draft_world = w;
        EmailSystem::set_draft(draft_world.emails, "printer-help", "My saved draft", 600);
        auto draft_choices =
            ChatResponses::choices(draft_world.people, draft_world.emails, printer, 600);
        check(draft_choices.size() == 3 &&
                  draft_choices.front()["action"].value_or(std::string{}) == "send_saved",
              "Saved draft lacks contextual choices");
        saves.save("draft", draft_world);
        draft_world = saves.load("draft");
        const auto discard =
            *std::find_if(draft_choices.begin(), draft_choices.end(), [](const auto& c) {
                return c["action"].template value_or(std::string{}) == "discard_draft";
            });
        ChatResponses::choose(draft_world.people, draft_world.emails, draft_world.pending_events,
                              draft_world.files, draft_world.network, actor, "casey", printer,
                              discard, 600);
        check(EmailSystem::message(draft_world.emails, "printer-help", 600).draft.empty(),
              "Explicit discard did not clear draft");
        EmailSystem::set_draft(draft_world.emails, "printer-help", "My saved draft", 600);
        const auto send_saved =
            ChatResponses::choices(draft_world.people, draft_world.emails, printer, 600).front();
        ChatResponses::choose(draft_world.people, draft_world.emails, draft_world.pending_events,
                              draft_world.files, draft_world.network, actor, "casey", printer,
                              send_saved, 600);
        const auto after_draft =
            ChatResponses::choices(draft_world.people, draft_world.emails, printer, 600);
        check(std::any_of(after_draft.begin(), after_draft.end(),
                          [](const auto& c) {
                              return c["action"].template value_or(std::string{}) == "share_file";
                          }),
              "A text-only draft prevented later actual delivery");
        options = ChatResponses::choices(w.people, w.emails, printer, 600);
        share = *std::find_if(options.begin(), options.end(), [](const auto& c) {
            return c["action"].template value_or(std::string{}) == "share_file";
        });
        ChatResponses::choose(w.people, w.emails, w.pending_events, w.files, w.network, actor,
                              "casey", printer, share, 600);
        const auto replied = ChatSystem::timeline(w.people, w.emails, "casey", 600);
        check(std::any_of(replied.begin(), replied.end(),
                          [](const auto& line) {
                              return line.outgoing &&
                                     line.text.find("Shared file: /public/office-guide.txt") !=
                                         std::string::npos;
                          }),
              "Actual shared file missing from reply");
        auto bad = w;
        bad.people["chat"]["response_catalog"]["profiles"]
            .as_array()
            ->front()
            .as_table()
            ->operator[]("choices")
            .as_array()
            ->front()
            .as_table()
            ->insert_or_assign("action", "invented");
        fails([&] {
            saves.save("responses", bad);
        });
        check(saves.load("responses").people["chat"]["responses"][budget.id].is_string(),
              "Bad catalog replaced valid save");
        std::filesystem::remove_all(temp);
        std::cout << "Calendar, categories and response tests passed\n";
    } catch (const std::exception& e) {
        std::filesystem::remove_all(temp);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
