#include "ai_sim/gui/GameSession.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTimer>
#include <iostream>
#include <functional>

namespace {
// Repeater delegates have visual parents that can differ from their QObject owners.
QQuickItem* findItem(QQuickItem* item, const QString& name) {
    if (item->objectName() == name)
        return item;
    for (auto* child : item->childItems())
        if (auto* found = findItem(child, name))
            return found;
    return nullptr;
}
} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("AI Sim Workstation");
    try {
        std::filesystem::path scenario = "data/scenarios/hobbyist.toml";
        std::filesystem::path saves = "saves/gui-prototype";
        std::filesystem::path screenshot;
        bool fresh = false, smoke = false, smoke_workspaces = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--new")
                fresh = true;
            else if (arg == "--screenshot" && i + 1 < argc)
                screenshot = argv[++i];
            else if (arg == "--smoke-test")
                smoke = true;
            else if (arg == "--smoke-workspaces")
                smoke_workspaces = true;
            else if (arg == "--save-directory" && i + 1 < argc)
                saves = argv[++i];
            else if (arg == "--scenario" && i + 1 < argc)
                scenario = argv[++i];
            else
                throw std::runtime_error("Usage: ai_sim_gui [--new] [--scenario path] "
                                         "[--save-directory path] [--smoke-test]");
        }
        ai_sim::gui::GameSession session(scenario, saves, fresh);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("session", &session);
        bool qml_errors = false;
        QObject::connect(&engine, &QQmlEngine::warnings, &app, [&](const QList<QQmlError>& errors) {
            if (!errors.empty())
                qml_errors = true;
        });
        engine.load(QUrl("qrc:/gui/Main.qml"));
        if (engine.rootObjects().empty())
            return 1;
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &session,
                         &ai_sim::gui::GameSession::save);
        if (smoke_workspaces) {
            QStringList pages;
            for (const auto& entry : session.tabs())
                if (entry.toMap()["enabled"].toBool())
                    pages.push_back(entry.toMap()["id"].toString());
            pages << "exchange" << "infrastructure" << "agent_form" << "file_form" << "settings"
                  << "saves";
            int stage = 0;
            std::function<void()> inspect;
            inspect = [&] {
                if (stage >= pages.size()) {
                    std::cout << "Qt gameplay workspaces, maps, chart, action form and settings "
                                 "rendered without warnings.\n";
                    app.quit();
                    return;
                }
                const auto page = pages[stage++];
                if (page == "exchange" || page == "infrastructure") {
                    session.selectTab("web");
                    session.act("browse", {{"address", page == "exchange"
                                                           ? "https://exchange.sim"
                                                           : "https://city.sim/infrastructure"}});
                    session.selectItem(page == "exchange"
                                           ? "BYTE"
                                           : session.workspace().front().toMap()["id"].toString());
                } else if (page == "file_form") {
                    session.selectTab("files");
                } else if (page == "agent_form") {
                    session.selectTab("agents");
                    session.selectItem("autochat");
                } else if (page != "settings" && page != "saves")
                    session.selectTab(page);
                QTimer::singleShot(100, &app, [&, page] {
                    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
                    if (!window || window->grabWindow().isNull() || qml_errors) {
                        std::cerr << "Workspace smoke failed: " << page.toStdString() << '\n';
                        app.exit(1);
                        return;
                    }
                    if (page == "agent_form") {
                        auto* command =
                            findItem(window->contentItem(), "workspaceActionagent_configure");
                        auto* dialog =
                            engine.rootObjects().front()->findChild<QObject*>("actionDialog");
                        if (!command || !dialog || !QMetaObject::invokeMethod(command, "clicked") ||
                            !dialog->property("visible").toBool()) {
                            std::cerr << "Agent form failed.\n";
                            app.exit(1);
                            return;
                        }
                        QTimer::singleShot(100, &app, [&, dialog, window] {
                            auto* cpu = findItem(window->contentItem(), "actionFieldcpu");
                            auto* apply = findItem(window->contentItem(), "applyActionButton");
                            if (!cpu || !apply) {
                                app.exit(1);
                                return;
                            }
                            cpu->setProperty("text", "0.02");
                            QMetaObject::invokeMethod(apply, "clicked");
                            if (dialog->property("visible").toBool() ||
                                !session.detail()["body"].toString().contains("0.02") ||
                                qml_errors) {
                                std::cerr << "Agent form submission failed.\n";
                                app.exit(1);
                                return;
                            }
                            inspect();
                        });
                        return;
                    }
                    if (page == "file_form") {
                        auto* command = findItem(window->contentItem(), "workspaceActionfile_host");
                        auto* dialog =
                            engine.rootObjects().front()->findChild<QObject*>("actionDialog");
                        if (!command || !dialog || !QMetaObject::invokeMethod(command, "clicked")) {
                            app.exit(1);
                            return;
                        }
                        QTimer::singleShot(100, &app, [&, dialog] {
                            if (!dialog->property("visible").toBool() || qml_errors) {
                                app.exit(1);
                                return;
                            }
                            QMetaObject::invokeMethod(dialog, "close");
                            inspect();
                        });
                        return;
                    }
                    if (page == "settings" || page == "saves") {
                        auto* dialog = engine.rootObjects().front()->findChild<QObject*>(
                            page == "settings" ? "settingsDialog" : "saveDialog");
                        if (!dialog || !QMetaObject::invokeMethod(dialog, "open")) {
                            app.exit(1);
                            return;
                        }
                        QTimer::singleShot(100, &app, [&, dialog] {
                            if (!dialog->property("visible").toBool() || qml_errors) {
                                app.exit(1);
                                return;
                            }
                            QMetaObject::invokeMethod(dialog, "close");
                            inspect();
                        });
                        return;
                    }
                    if (!screenshot.empty()) {
                        if (page == "exchange")
                            window->grabWindow().save(QString::fromStdString(screenshot.string()));
                        else if (page == "network" || page == "agents" || page == "modifications" ||
                                 page == "files")
                            window->grabWindow().save(QString::fromStdString(screenshot.string()) +
                                                      "." + page + ".png");
                    }
                    inspect();
                });
            };
            QTimer::singleShot(300, &app, inspect);
            // The callback state must stay alive throughout the event loop.
            return app.exec();
        }
        if (smoke)
            QTimer::singleShot(1000, &app, [&] {
                QObject* root = engine.rootObjects().front();
                auto* window = qobject_cast<QQuickWindow*>(root);
                if (!window || window->grabWindow().isNull() || qml_errors) {
                    std::cerr << "Smoke: window/frame or QML warnings failed.\n";
                    app.exit(1);
                    return;
                }
                if (!screenshot.empty() &&
                    !window->grabWindow().save(QString::fromStdString(screenshot.string()))) {
                    app.exit(1);
                    return;
                }
                auto* menu_button = root->findChild<QObject*>("menuButton");
                auto* menu = root->findChild<QObject*>("gameMenu");
                if (!menu_button || !menu || !QMetaObject::invokeMethod(menu_button, "clicked") ||
                    !menu->property("visible").toBool()) {
                    std::cerr << "Smoke: menu interaction failed.\n";
                    app.exit(1);
                    return;
                }
                QMetaObject::invokeMethod(menu, "close");
                auto* response = findItem(window->contentItem(), "responseButton");
                if (!response || !QMetaObject::invokeMethod(response, "clicked") ||
                    session.messages().size() < 2) {
                    std::cerr << "Smoke: response interaction failed.\n";
                    app.exit(1);
                    return;
                }
                auto* resources = findItem(window->contentItem(), "tabButtonresources");
                if (!resources || resources->property("enabled").toBool() ||
                    session.selectedTab() != "messages" ||
                    findItem(window->contentItem(), "tabButtonactivity")) {
                    std::cerr << "Smoke: opening resource lock or activity removal failed.\n";
                    app.exit(1);
                    return;
                }
                QTimer::singleShot(250, &app, [&] {
                    if (qml_errors)
                        app.exit(1);
                    else {
                        std::cout << "Qt window, menu, response button, opening resource lock and "
                                     "activity removal passed.\n";
                        app.quit();
                    }
                });
            });
        return app.exec();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
