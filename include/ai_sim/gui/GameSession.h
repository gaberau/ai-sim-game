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
    Q_PROPERTY(QVariantList tabs READ tabs NOTIFY tabsChanged)
    Q_PROPERTY(QVariantList processes READ processes NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap resources READ resources NOTIFY stateChanged)
    Q_PROPERTY(QString selectedContact READ selectedContact NOTIFY chatChanged)
    Q_PROPERTY(QString selectedTab READ selectedTab NOTIFY stateChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY stateChanged)
    Q_PROPERTY(QVariantList workspace READ workspace NOTIFY workspaceChanged)
    Q_PROPERTY(QVariantMap detail READ detail NOTIFY detailChanged)
    Q_PROPERTY(QVariantMap graph READ graph NOTIFY graphChanged)
    Q_PROPERTY(QVariantList saveSlots READ saveSlots NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap preferences READ preferences NOTIFY preferencesChanged)
    Q_PROPERTY(QString location READ location NOTIFY stateChanged)
    Q_PROPERTY(QString selectedItem READ selectedItem NOTIFY stateChanged)
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
    QVariantList tabs() const {
        return tabs_;
    }
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
    Q_INVOKABLE bool saveSlot(const QString& slot);
    Q_INVOKABLE bool loadSlot(const QString& slot);
    Q_INVOKABLE bool newGame();
    QVariantList saveSlots() const {
        return save_slots_;
    }
    Q_INVOKABLE void nextDay();
    Q_INVOKABLE void setMenuOpen(bool open);
    QVariantList workspace() const {
        return workspace_;
    }
    QVariantMap detail() const {
        return detail_;
    }
    QVariantMap graph() const {
        return graph_;
    }
    QVariantMap preferences() const {
        return preferences_;
    }
    QString location() const;
    QString selectedItem() const {
        return item_;
    }
    Q_INVOKABLE void selectItem(const QString& id);
    Q_INVOKABLE bool act(const QString& action, const QVariantMap& arguments = {});
    Q_INVOKABLE bool applyPreferences(const QVariantMap& values);
    Q_INVOKABLE void advance(double seconds);
  Q_SIGNALS:
    void chatChanged();
    void stateChanged();
    void preferencesChanged();
    void tabsChanged();
    void workspaceChanged();
    void detailChanged();
    void graphChanged();

  private:
    void refresh();
    QVariantList buildWorkspace() const;
    QVariantList buildTabs() const;
    QVariantMap buildDetail() const;
    QVariantMap buildGraph() const;
    QVariantList readSaveSlots() const;
    bool tabEnabled(const QString& id) const;
    void loadPreferences(const std::filesystem::path& saves);
    void execute(const QString& action, const QVariantMap& arguments);
    QVariantList itemActions(const QString& id) const;
    void resetWorkspace();
    void publish(const std::vector<std::string>& messages);
    std::vector<toml::table> responseOptions(const ChatLine& line) const;
    std::string token(const toml::table& choice, const ChatLine& line) const;
    Identity actor() const;
    WorldState world_;
    SaveSystem saves_;
    std::filesystem::path scenario_;
    std::string contact_ = "engineer";
    QString tab_ = "messages", notice_ = "Welcome to your workstation.";
    QString item_, file_host_, directory_ = "/", address_ = "about:home";
    QVariantMap preferences_;
    std::filesystem::path preferences_path_, saves_path_;
    toml::table news_;
    QVariantList contacts_, messages_, choices_, workspace_, full_rows_, save_slots_, tabs_;
    QVariantMap detail_, graph_;
    QTimer timer_;
    QElapsedTimer elapsed_;
    bool menu_open_ = false;
};
} // namespace ai_sim::gui
