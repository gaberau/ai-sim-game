#pragma once

#include "ai_sim/SaveSystem.h"
#include "ai_sim/ChatSystem.h"
#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

namespace ai_sim::gui {
// A presentation adapter: all story decisions and simulation work remain in ai_sim_core.
class GameSession : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList contacts READ contacts NOTIFY chatChanged)
    Q_PROPERTY(QVariantList messages READ messages NOTIFY chatChanged)
    Q_PROPERTY(QVariantList choices READ choices NOTIFY chatChanged)
    Q_PROPERTY(QVariantList tabs READ tabs NOTIFY stateChanged)
    Q_PROPERTY(QVariantList processes READ processes NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap resources READ resources NOTIFY stateChanged)
    Q_PROPERTY(QString selectedContact READ selectedContact NOTIFY chatChanged)
    Q_PROPERTY(QString selectedTab READ selectedTab NOTIFY stateChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY stateChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY stateChanged)
  public:
    GameSession(const std::filesystem::path& scenario, const std::filesystem::path& saves,
                bool fresh, QObject* parent = nullptr);
    QVariantList contacts() const {
        return contacts_;
    }
    QVariantList messages() const {
        return messages_;
    }
    QVariantList choices() const {
        return choices_;
    }
    QVariantList tabs() const;
    QVariantList processes() const;
    QVariantMap resources() const;
    QString selectedContact() const {
        return QString::fromStdString(contact_);
    }
    QString selectedTab() const {
        return tab_;
    }
    QString notice() const {
        return notice_;
    }
    bool paused() const {
        return world_.paused;
    }
    Q_INVOKABLE void selectContact(const QString& id);
    Q_INVOKABLE void selectTab(const QString& id);
    Q_INVOKABLE void choose(const QString& token);
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE bool save();
    Q_INVOKABLE void setMenuOpen(bool open);
    void advance(double seconds);
  Q_SIGNALS:
    void chatChanged();
    void stateChanged();

  private:
    void refresh();
    void publish(const std::vector<std::string>& messages);
    std::vector<toml::table> responseOptions(const ChatLine& line) const;
    std::string token(const toml::table& choice, const ChatLine& line) const;
    Identity actor() const;
    WorldState world_;
    SaveSystem saves_;
    std::filesystem::path scenario_;
    std::string contact_ = "engineer";
    QString tab_ = "messages", notice_ = "Qt workstation prototype";
    QVariantList contacts_, messages_, choices_;
    QTimer timer_;
    QElapsedTimer elapsed_;
    bool menu_open_ = false;
};
} // namespace ai_sim::gui
