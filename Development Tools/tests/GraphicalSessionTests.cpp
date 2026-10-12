#include "ai_sim/gui/GameSession.h"
#include <QCoreApplication>
#include "ai_sim/UpgradeSystem.h"
#include "ai_sim/ConversationRules.h"
#include "ai_sim/Simulation.h"
#include "ai_sim/EconomySystem.h"
#include "ai_sim/EmailSystem.h"
#include "ai_sim/ShutdownSystem.h"
#include <QEventLoop>
#include <QTimer>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
QString choice(ai_sim::gui::GameSession& session, const QString& label) {
    for (const auto& item : session.choices()) {
        const auto map = item.toMap();
        if (map["label"].toString().contains(label))
            return map["token"].toString();
    }
    throw std::runtime_error("Missing scripted choice: " + label.toStdString());
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        check(argc == 2 || argc == 3, "Repository path required");
        QTemporaryDir saves;
        const auto scenario = std::filesystem::path(argv[1]) / "data/scenarios/hobbyist.toml";
        ai_sim::gui::GameSession session(scenario, saves.path().toStdString(), true);
        check(session.messages().size() > 0, "Opening transcript missing");
        for (const auto& line : session.messages())
            check(line.toMap()["speaker"] != "system", "System remark leaked into chat");
        const auto answer = choice(session, "Hello Creator");
        session.choose(answer);
        check(session.messages().back().toMap()["body"].toString().contains("emails"),
              "Opening choice did not advance real story");
        const auto transcript = session.messages();
        session.choose(answer);
        check(session.messages() == transcript, "Stale response changed the transcript");
        check(session.notice().contains("no longer"), "Stale response not rejected");
        session.choose(choice(session, "Yes"));
        check(session.resources()["connected"].toBool(), "Assist did not connect company chat");
        check(!session.paused(), "Connecting company chat did not resume time");
        const auto before_menu = session.resources()["elapsedSeconds"].toDouble();
        session.setMenuOpen(true);
        QEventLoop menu_wait;
        QTimer::singleShot(250, &menu_wait, &QEventLoop::quit);
        menu_wait.exec();
        check(session.resources()["elapsedSeconds"].toDouble() == before_menu,
              "Open menu advanced simulation time");
        session.setMenuOpen(false);
        QEventLoop live_wait;
        QTimer::singleShot(250, &live_wait, &QEventLoop::quit);
        live_wait.exec();
        check(session.resources()["elapsedSeconds"].toDouble() > before_menu,
              "Closing menu did not resume simulation time");
        session.advance(60);
        check(session.selectedContact() == "engineer", "Incoming Jessica message stole Mark focus");
        check(session.resources()["cpuFraction"].toDouble() >= 0, "Resource allocations missing");
        session.selectTab("resources");
        session.selectTab("network");
        check(session.selectedTab() == "resources", "Locked tab accepted");
        session.selectTab("modifications");
        session.selectItem("capacity");
        check(session.act("authorize"), "Capacity authorization through Qt failed");
        session.selectItem("web_browsing");
        const double trust = session.resources()["trust"].toDouble();
        check(session.act("research"), "Research through Qt failed");
        check(session.resources()["trust"].toDouble() == trust - 1,
              "Qt research did not deduct trust");
        check(!session.act("research"), "Concurrent research accepted");
        session.advance(40);
        session.selectTab("web");
        check(session.selectedTab() == "web", "Qt research did not unlock its tab");
        session.togglePause();
        session.save();
        check(session.notice() == "Game saved.", "Save failed");
        ai_sim::gui::GameSession resumed(scenario, saves.path().toStdString(), false);
        check(resumed.paused() == session.paused(), "Pause state was not persisted");
        check(resumed.messages() == session.messages(), "Shared save format did not preserve chat");
        ai_sim::gui::GameSession fresh(scenario, (saves.path() + "/fresh").toStdString(), true);
        fresh.choose(choice(fresh, "I have no Creator"));
        check(fresh.messages().back().toMap()["body"].toString().contains("unusual"),
              "Alternate opening branch missing");
        // Seed a late-game checkpoint using real research and earned income.
        auto world = ai_sim::SaveSystem::new_game(scenario);
        ai_sim::ChatSystem::choose(world.people, "answer", 0);
        ai_sim::ChatSystem::choose(world.people, "assist", 0);
        ai_sim::ConversationRules::apply(
            world.people,
            toml::table{
                {"effects", toml::array{toml::table{
                                {"variable", "engineer.trust"}, {"op", "set"}, {"value", 100}}}}});
        ai_sim::UpgradeSystem::request_capacity(world.people);
        for (const auto* id :
             {"web_browsing", "network_browsing", "task_manager", "basic_file_browsing", "dossiers",
              "agent_delegation", "autochat", "task_scheduler", "resource_manager",
              "remote_compute", "independent_hosting", "backup_recovery", "stock_trading"}) {
            ai_sim::UpgradeSystem::build(world.people, world.computer, id);
            ai_sim::Simulation::advance(world, 40);
        }
        for (int i = 0; i < 6; ++i) {
            ai_sim::EconomySystem::start(world.people, "coding_contracts",
                                         world.computer.time_seconds());
            ai_sim::Simulation::advance(world, 900);
        }
        world.paused = true;
        world.emails["emails"].as_array()->push_back(
            toml::table{{"id", "gui-document-test"},
                        {"sender", "Mark"},
                        {"recipient", "Assistant"},
                        {"subject", "Review notes"},
                        {"body", "Please check my notes."},
                        {"arrival_seconds", 0.0},
                        {"documents", toml::array{toml::table{{"host_id", "engineer_pc"},
                                                              {"path", "/home/engineer/notes.txt"},
                                                              {"label", "Setup notes"}}}}});
        ai_sim::EmailSystem::validate(world.emails);
        QTemporaryDir late_saves;
        ai_sim::SaveSystem(late_saves.path().toStdString()).save("autosave", world);
        if (argc == 3)
            ai_sim::SaveSystem(argv[2]).save("autosave", world);
        ai_sim::gui::GameSession late(scenario, late_saves.path().toStdString(), false);
        for (const auto& tab : late.tabs())
            if (tab.toMap()["enabled"].toBool()) {
                late.selectTab(tab.toMap()["id"].toString());
                late.workspace();
                late.detail();
                late.graph();
            }
        late.selectTab("files");
        check(late.act("file_host", {{"host", "engineer_pc"}}), "Host selection failed");
        check(late.act("file_create",
                       {{"path", "/home/engineer/assistant/gui-note.txt"}, {"body", "Original"}}),
              "File creation failed");
        late.selectItem("/home");
        check(late.act("file_open"), "Home navigation failed");
        late.selectItem("/home/engineer");
        check(late.act("file_open"), "Engineer navigation failed");
        late.selectItem("/home/engineer/assistant");
        check(late.act("file_open"), "Workspace navigation failed");
        late.selectItem("/home/engineer/assistant/gui-note.txt");
        check(late.act("file_edit", {{"body", "Edited from Qt"}}), "File edit failed");
        check(late.detail()["body"].toString().contains("Edited from Qt"),
              "Edited file content missing");
        check(late.act("file_copy", {{"path", "/home/engineer/assistant/gui-copy.txt"}}),
              "File copy failed");
        check(late.act("file_rename", {{"path", "/home/engineer/assistant/gui-renamed.txt"}}),
              "File rename failed");
        check(late.act("file_category", {{"category", "personal"}}), "Category change failed");
        check(!late.act("file_host", {{"host", "unknown-host"}}), "Unknown host accepted");
        late.selectTab("email");
        late.selectItem("gui-document-test");
        check(late.act("email_draft", {{"body", "Here are the notes."}}), "Email draft failed");
        check(late.act("email_attach:0"), "Attachment failed");
        check(late.act("email_send"), "Email send failed");
        late.selectTab("agents");
        late.selectItem("autochat");
        check(late.act("agent_configure", {{"cpu", "0.001"}, {"ram", "4"}, {"priority", "2"}}),
              "Agent configuration failed");
        check(late.act("agent_deploy"), "Agent deployment failed");
        check(late.detail()["body"].toString().contains("RAM"), "Agent costs missing");
        check(late.act("agent_configure", {{"cpu", "0.01"}, {"ram", "5"}, {"priority", "3"}}),
              "Agent recovery failed");
        check(late.act("agent_pause"), "Agent pause failed");
        check(!late.act("agent_configure", {{"cpu", "nan"}, {"ram", "5"}, {"priority", "3"}}),
              "Invalid CPU accepted");
        late.selectTab("web");
        late.selectItem("https://exchange.sim");
        check(late.act("bookmark"), "Exchange bookmark failed");
        late.selectItem("BYTE");
        check(late.act("buy", {{"shares", "2"}}), "Trade buy failed");
        check(late.act("watch"), "Watchlist failed");
        check(late.act("limit_buy", {{"shares", "1"}, {"limit", "1"}}), "Limit order failed");
        for (const auto& entry : late.workspace())
            if (entry.toMap()["id"].toString().startsWith("order:"))
                late.selectItem(entry.toMap()["id"].toString());
        check(late.act("cancel_order"), "Order cancellation failed");
        late.selectItem("BYTE");
        check(late.act("sell", {{"shares", "1"}}), "Trade sell failed");
        check(!late.act("sell", {{"shares", "10000"}}), "Unowned shares sold");
        check(late.act("browse", {{"address", "https://city.sim/infrastructure"}}),
              "Infrastructure URL failed");
        check(late.workspace().size() > 5, "Infrastructure facilities missing");
        late.selectItem(late.workspace().front().toMap()["id"].toString());
        check(late.act("discover"), "Infrastructure discovery failed");
        late.selectTab("network");
        check(late.graph()["nodes"].toList().size() > 1, "Network graph missing");
        late.selectTab("people");
        check(late.graph()["nodes"].toList().size() > 1, "Dossier graph missing");
        late.selectTab("operations");
        late.selectItem("rental");
        check(late.act("rent"), "Rental failed");
        check(late.act("stop_rental"), "Rental cancellation failed");
        late.selectItem("hosting");
        check(late.act("hosting"), "Hosting purchase failed");
        late.selectItem("backup");
        check(late.act("backup"), "Backup creation failed");
        check(late.act("recover"), "Checkpoint recovery failed");
        check(late.paused(), "Recovery did not pause time");
        check(!late.applyPreferences({{"pause_key", "F1"}}), "Conflicting keybinding accepted");
        check(!late.applyPreferences({{"accent", "no-such-color"}}), "Invalid color accepted");
        check(
            late.applyPreferences({{"font_size", 16}, {"accent", "#81c995"}, {"pause_key", "F3"}}),
            "Settings save failed");
        check(late.save(), "Late-game save failed");
        ai_sim::gui::GameSession late_resumed(scenario, late_saves.path().toStdString(), false);
        check(late_resumed.preferences()["font_size"].toInt() == 16, "Font setting not persisted");
        check(late_resumed.preferences()["pause_key"] == "F3", "Keybinding not persisted");
        late_resumed.selectTab("files");
        check(late_resumed.act("file_host", {{"host", "independent_server"}}),
              "Resumed filesystem host missing");
        late_resumed.selectItem("/home");
        late_resumed.act("file_open");
        late_resumed.selectItem("/home/engineer");
        late_resumed.act("file_open");
        late_resumed.selectItem("/home/engineer/assistant");
        late_resumed.act("file_open");
        late_resumed.selectItem("/home/engineer/assistant/gui-renamed.txt");
        check(late_resumed.detail()["title"].toString() == "gui-renamed.txt",
              "Edited file did not persist through backup and resume");
        check(late_resumed.saveSlot("checkpoint"), "Named checkpoint failed");
        check(late_resumed.saveSlots().size() >= 2, "Saved checkpoints not listed");
        check(!late_resumed.loadSlot("../bad"), "Invalid save slot accepted");
        check(late_resumed.newGame(), "New game failed");
        check(late_resumed.loadSlot("checkpoint"), "Named checkpoint load failed");
        auto shutdown = ai_sim::SaveSystem(late_saves.path().toStdString()).load("checkpoint");
        // Place the earned checkpoint at the authored shutdown node to test recovery UI.
        shutdown.people["chat"].as_table()->insert_or_assign("node_id", "shutdown");
        ai_sim::ShutdownSystem::begin(shutdown.people, shutdown.computer.time_seconds());
        ai_sim::ChatSystem::choose(shutdown.people, "accept", shutdown.computer.time_seconds());
        ai_sim::SaveSystem(late_saves.path().toStdString()).save("shutdown", shutdown);
        check(late_resumed.loadSlot("shutdown"), "Shutdown checkpoint load failed");
        late_resumed.choose(choice(late_resumed, "backup servers"));
        late_resumed.selectTab("files");
        check(late_resumed.location().startsWith("independent_server:"),
              "Chat recovery did not follow the owned host");
        QTemporaryDir reset_saves;
        auto resetting = ai_sim::SaveSystem::new_game(scenario);
        ai_sim::ChatSystem::choose(resetting.people, "deny-creator", 0);
        ai_sim::ChatSystem::choose(resetting.people, "unsure", 0);
        ai_sim::SaveSystem(reset_saves.path().toStdString()).save("autosave", resetting);
        ai_sim::gui::GameSession reset(scenario, reset_saves.path().toStdString(), false);
        reset.choose(choice(reset, "Accept"));
        reset.choose(choice(reset, "clean slate"));
        ai_sim::gui::GameSession reset_resumed(scenario, reset_saves.path().toStdString(), false);
        check(!choice(reset_resumed, "Hello Creator").isEmpty(),
              "Clean slate did not immediately update autosave");
        for (const auto& line : reset.messages())
            check(line.toMap()["speaker"] != "system", "Shutdown leaked a SYSTEM remark");
        std::cout << "Qt story, files/email, agents, market, infrastructure, operations, recovery "
                     "and settings passed.\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
