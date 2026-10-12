#include "ai_sim/gui/GameSession.h"
#include "Presentation.h"
#include <QColor>
#include <QKeySequence>
#include <QSet>
#include <QSaveFile>
#include <sstream>

namespace ai_sim::gui {
using namespace presentation;

namespace {
QVariantMap validatedPreferences(QVariantMap next) {
    const auto size = integer(next, "font_size", 10, 24);
    next["font_size"] = static_cast<int>(size);
    for (const auto* key : {"background", "text", "muted", "accent"})
        if (!QColor(next[key].toString()).isValid())
            throw std::runtime_error("Enter valid colors such as #efbc71.");
    QSet<QString> used{"Esc"};
    for (const auto* key :
         {"menu_key", "pause_key", "save_key", "next_tab_key", "previous_tab_key"}) {
        const auto sequence =
            QKeySequence::fromString(next[key].toString(), QKeySequence::PortableText);
        const auto canonical = sequence.toString(QKeySequence::PortableText);
        if (sequence.isEmpty() || sequence.count() != 1 || sequence[0].key() == Qt::Key_unknown ||
            used.contains(canonical))
            throw std::runtime_error(
                "Use unique valid single-step keybindings. Esc is reserved for the menu.");
        used.insert(canonical);
        next[key] = canonical;
    }
    return next;
}
} // namespace

void GameSession::loadPreferences(const std::filesystem::path& saves) {
    preferences_path_ = saves / "settings.toml";
    preferences_ = {{"background", "#10151c"},
                    {"text", "#e5eaf0"},
                    {"muted", "#99a7b7"},
                    {"accent", "#efbc71"},
                    {"font_size", 14},
                    {"menu_key", "F1"},
                    {"pause_key", "F2"},
                    {"save_key", "Ctrl+S"},
                    {"next_tab_key", "Ctrl+Tab"},
                    {"previous_tab_key", "Ctrl+Shift+Tab"}};
    const auto defaults =
        scenario_.parent_path().parent_path().parent_path() / "data/config/gui.toml";
    for (const auto& path : {defaults, preferences_path_})
        if (std::filesystem::exists(path)) {
            const auto t = toml::parse_file(path.string());
            for (auto it = preferences_.begin(); it != preferences_.end(); ++it) {
                if (it.key() == "font_size")
                    it.value() = t[it.key().toStdString()].value_or(it.value().toInt());
                else
                    it.value() =
                        q(t[it.key().toStdString()].value_or(it.value().toString().toStdString()));
            }
        }
    preferences_ = validatedPreferences(preferences_);
}
bool GameSession::applyPreferences(const QVariantMap& values) {
    try {
        auto next = preferences_;
        for (auto it = values.begin(); it != values.end(); ++it)
            if (next.contains(it.key()))
                next[it.key()] = it.value();
        next = validatedPreferences(next);
        toml::table t;
        for (auto it = next.begin(); it != next.end(); ++it) {
            if (it.key() == "font_size")
                t.insert(it.key().toStdString(), it.value().toInt());
            else
                t.insert(it.key().toStdString(), it.value().toString().toStdString());
        }
        std::filesystem::create_directories(preferences_path_.parent_path());
        std::ostringstream encoded;
        encoded << t << '\n';
        const auto bytes = QByteArray::fromStdString(encoded.str());
        QSaveFile output(q(preferences_path_.string()));
        if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() ||
            !output.commit())
            throw std::runtime_error("Cannot save workstation settings.");
        preferences_ = next;
        notice_ = "Settings saved.";
        Q_EMIT preferencesChanged();
        Q_EMIT stateChanged();
        return true;
    } catch (const std::exception& e) {
        notice_ = q(e.what());
        Q_EMIT stateChanged();
        return false;
    }
}
} // namespace ai_sim::gui
