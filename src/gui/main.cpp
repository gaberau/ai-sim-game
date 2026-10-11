#include "ai_sim/gui/GameSession.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTimer>
#include <iostream>

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
        bool fresh = false, smoke = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--new")
                fresh = true;
            else if (arg == "--screenshot" && i + 1 < argc)
                screenshot = argv[++i];
            else if (arg == "--smoke-test")
                smoke = true;
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
                if (!resources || !QMetaObject::invokeMethod(resources, "clicked") ||
                    session.selectedTab() != "resources") {
                    std::cerr << "Smoke: resource tab interaction failed.\n";
                    app.exit(1);
                    return;
                }
                QTimer::singleShot(250, &app, [&] {
                    if (qml_errors)
                        app.exit(1);
                    else {
                        std::cout << "Qt window, menu, response button, and resource tab passed.\n";
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
