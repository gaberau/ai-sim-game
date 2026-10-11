#include "ai_sim/gui/GameSession.h"
#include <QCoreApplication>
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
        check(argc == 2, "Repository path required");
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
        session.selectTab("files");
        check(session.selectedTab() == "resources", "Unimplemented tab accepted");
        session.togglePause();
        session.save();
        check(session.notice() == "Prototype saved.", "Save failed");
        ai_sim::gui::GameSession resumed(scenario, saves.path().toStdString(), false);
        check(resumed.paused() == session.paused(), "Pause state was not persisted");
        check(resumed.messages() == session.messages(), "Shared save format did not preserve chat");
        ai_sim::gui::GameSession fresh(scenario, (saves.path() + "/fresh").toStdString(), true);
        fresh.choose(choice(fresh, "I have no Creator"));
        check(fresh.messages().back().toMap()["body"].toString().contains("unusual"),
              "Alternate opening branch missing");
        std::cout << "Qt adapter story, resources, stale choices, focus, and save/resume passed.\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
