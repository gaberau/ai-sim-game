#include "ai_sim/gui/GameSession.h"
#include "ai_sim/Simulation.h"
#include "ai_sim/ConversationSystem.h"
#include "ai_sim/ConversationRules.h"
#include "ai_sim/ChatResponses.h"
#include "ai_sim/ShutdownSystem.h"
#include "ai_sim/OperationsSystem.h"
#include "ai_sim/MessageReviewSystem.h"
#include "ai_sim/EconomySystem.h"
#include "ai_sim/DefinitionLoader.h"
#include "ai_sim/EmailSystem.h"
#include <QDateTime>
#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace ai_sim::gui {
namespace {
QString text(const std::string& value) {
    return QString::fromStdString(value);
}
std::string value(const toml::table& table, const char* key) {
    return table[key].value_or(std::string{});
}
} // namespace
GameSession::GameSession(const std::filesystem::path& scenario, const std::filesystem::path& saves,
                         bool fresh, QObject* parent)
    : QObject(parent), saves_(saves), scenario_(scenario) {
    world_ = !fresh && saves_.exists("autosave") ? saves_.load("autosave")
                                                 : SaveSystem::new_game(scenario);
    ShutdownSystem::apply_reset(world_, scenario_);
    saves_path_ = saves;
    file_host_ = text(world_.network.hosts()[world_.network.local_host_index()].id);
    loadPreferences(saves);
    save_slots_ = readSaveSlots();
    const auto definitions = ScenarioDefinition::load(scenario);
    if (!definitions.news.empty())
        news_ = DefinitionLoader::load(definitions.news);
    elapsed_.start();
    connect(&timer_, &QTimer::timeout, this, [this] {
        const double elapsed = std::min(1.0, elapsed_.restart() / 1000.0);
        if (!menu_open_ && !world_.paused && ChatSystem::connected(world_.people) &&
            !ShutdownSystem::active(world_.people) && !world_.computer.day_finished())
            advance(elapsed * world_.computer.playback_speed());
    });
    timer_.start(100);
    refresh();
}
Identity GameSession::actor() const {
    return {world_.computer.player_user(), world_.computer.player_groups()};
}
std::string GameSession::token(const toml::table& choice, const ChatLine& line) const {
    return line.id + ":" + line.node_id + ":" + value(choice, "conversation_id") + ":" +
           value(choice, "id");
}
std::vector<toml::table> GameSession::responseOptions(const ChatLine& line) const {
    const double time = world_.computer.time_seconds();
    // Shutdown prompts are editor SYSTEM records; their decisions remain playable.
    if (contact_ == "engineer" && ShutdownSystem::active(world_.people))
        return ChatSystem::choices(world_.people);
    if (ChatSystem::stopped(world_.people))
        return {};
    if (contact_ == "engineer" &&
        (world_.people["chat"]["prompt_id"].is_string()
             ? line.id == world_.people["chat"]["prompt_id"].value_or(std::string{})
             : line.node_id == world_.people["chat"]["node_id"].value_or(std::string{}))) {
        auto options = ChatSystem::choices(world_.people);
        if (!options.empty())
            return options;
    }
    auto fallback = ChatResponses::choices(world_.people, world_.emails, line, time);
    if (std::any_of(fallback.begin(), fallback.end(), [](const auto& c) {
            return value(c, "action") == "send_saved";
        }))
        return fallback;
    auto options = ConversationSystem::choices(world_.people, contact_, world_.files,
                                               world_.network, actor(), time);
    if (!options.empty() && line.conversation_id == value(options.front(), "conversation_id")) {
        if (const auto* runs = world_.people["chat"]["workday"]["runs"].as_array())
            for (const auto& node : *runs) {
                const auto& run = *node.as_table();
                if (value(run, "id") != line.conversation_id)
                    continue;
                if (line.node_id == value(run, "node_id"))
                    return options;
            }
    }
    if (fallback.empty() && ConversationSystem::next_response(world_.people, time))
        return {toml::table{
            {"id", "ui-wait"}, {"text", "Wait for the next response"}, {"ui_action", "wait"}}};
    return fallback;
}
void GameSession::refresh() {
    QVariantList contacts, messages, choices;
    if (tab_ == "messages")
        ChatSystem::mark_read(world_.people, contact_);
    const auto available =
        ChatSystem::contacts(world_.people, world_.emails, world_.computer.time_seconds());
    for (const auto& c : available)
        contacts.push_back(
            QVariantMap{{"id", text(c.id)}, {"name", text(c.name)}, {"unread", c.unread}});
    if (contacts.empty())
        contacts.push_back(QVariantMap{{"id", "engineer"}, {"name", "Mark"}, {"unread", 0}});
    const auto timeline = ChatSystem::timeline(world_.people, world_.emails, contact_,
                                               world_.computer.time_seconds());
    ChatLine incoming;
    for (const auto& line : timeline) {
        if (line.speaker == "system")
            continue;
        messages.push_back(QVariantMap{{"speaker", text(line.speaker)},
                                       {"body", text(line.text)},
                                       {"outgoing", line.outgoing},
                                       {"id", text(line.id)}});
        if (!line.outgoing)
            incoming = line;
    }
    for (auto choice : responseOptions(incoming)) {
        auto reason = value(choice, "disabled_reason");
        choices.push_back(QVariantMap{{"token", text(token(choice, incoming))},
                                      {"label", text(value(choice, "text"))},
                                      {"reason", text(reason)},
                                      {"enabled", reason.empty()}});
    }
    const bool changed = contacts != contacts_ || messages != messages_ || choices != choices_;
    contacts_ = std::move(contacts);
    messages_ = std::move(messages);
    choices_ = std::move(choices);
    if (changed)
        Q_EMIT chatChanged();
    if (!tabEnabled(tab_)) {
        tab_ = "messages";
        resetWorkspace();
    }
    const auto available_tabs = buildTabs();
    if (available_tabs != tabs_) {
        tabs_ = available_tabs;
        Q_EMIT tabsChanged();
    }
    const auto items = buildWorkspace();
    if (!items.empty() && std::none_of(items.begin(), items.end(), [this](const auto& row) {
            return row.toMap()["id"].toString() == item_;
        }))
        item_ = items.front().toMap()["id"].toString();
    if (items.empty())
        item_.clear();
    full_rows_ = items;
    // List rows contain only summaries, so ticking detail values do not reset scrolling.
    QVariantList summaries;
    for (const auto& item : items) {
        auto summary = item.toMap();
        summary.remove("body");
        summary.remove("draft");
        summary.remove("chart");
        summaries.push_back(summary);
    }
    if (summaries != workspace_) {
        workspace_ = summaries;
        Q_EMIT workspaceChanged();
    }
    const auto details = buildDetail();
    if (details != detail_) {
        detail_ = details;
        Q_EMIT detailChanged();
    }
    const auto connections = buildGraph();
    if (connections != graph_) {
        graph_ = connections;
        Q_EMIT graphChanged();
    }
    Q_EMIT stateChanged();
}
void GameSession::selectContact(const QString& id) {
    const auto found = std::find_if(contacts_.begin(), contacts_.end(), [&](const auto& item) {
        return item.toMap()["id"].toString() == id;
    });
    if (found == contacts_.end())
        return;
    contact_ = id.toStdString();
    ChatSystem::mark_read(world_.people, contact_);
    refresh();
    Q_EMIT chatChanged();
}
void GameSession::selectTab(const QString& id) {
    if (!tabEnabled(id))
        return;
    tab_ = id;
    resetWorkspace();
    refresh();
}
void GameSession::publish(const std::vector<std::string>& messages) {
    if (!messages.empty())
        notice_ = text(messages.back());
}
void GameSession::advance(double seconds) {
    try {
        const auto update = Simulation::advance(world_, seconds);
        publish(update.messages);
        if (!update.payments.empty())
            publish(update.payments);
        refresh();
    } catch (const std::exception& e) {
        notice_ = text(e.what());
        Q_EMIT stateChanged();
    }
}
void GameSession::choose(const QString& requested) {
    try {
        ChatLine incoming;
        for (const auto& line : ChatSystem::timeline(world_.people, world_.emails, contact_,
                                                     world_.computer.time_seconds()))
            if (!line.outgoing && line.speaker != "system")
                incoming = line;
        const auto options = responseOptions(incoming);
        auto found = std::find_if(options.begin(), options.end(), [&](const auto& c) {
            return text(token(c, incoming)) == requested;
        });
        if (found == options.end())
            throw std::runtime_error("That response is no longer available.");
        const auto choice = *found;
        for (const auto& item : choices_)
            if (item.toMap()["token"].toString() == requested && !item.toMap()["enabled"].toBool())
                throw std::runtime_error(item.toMap()["reason"].toString().toStdString());
        const auto disabled = value(choice, "disabled_reason");
        if (!disabled.empty())
            throw std::runtime_error(disabled);
        const bool was_connected = ChatSystem::connected(world_.people);
        const double time = world_.computer.time_seconds();
        if (value(choice, "ui_action") == "wait") {
            if (const auto next = ConversationSystem::next_response(world_.people, time))
                advance(std::max(0.0, *next - time));
        } else if (value(choice, "id") == "backup" && ShutdownSystem::active(world_.people)) {
            world_ = OperationsSystem::recover(world_, saves_);
            file_host_ = text(world_.network.hosts()[world_.network.local_host_index()].id);
            directory_ = "/";
            resetWorkspace();
            saves_.save("autosave", world_);
        } else if (choice.contains("message_id")) {
            const auto action = value(choice, "action");
            if (action == "read_files") {
                const auto email = EmailSystem::message(world_.emails, incoming.request_id, time);
                if (email.documents.empty())
                    throw std::runtime_error("No requested documents.");
                const auto& doc = email.documents.front();
                world_.files.inspect(world_.network, actor(), doc.host_id, doc.path);
                ChatSystem::unlock(world_.people, "files");
                ChatSystem::reveal_tab(world_.people, "files");
                tab_ = "files";
                file_host_ = text(doc.host_id);
                directory_ = text(FileSystem::parent(doc.path));
                item_ = text(doc.path);
            } else if (action == "open_files" || action == "open_news" || action == "open_people" ||
                       action == "open_processes") {
                const auto target = action.substr(5);
                ChatSystem::unlock(world_.people, target);
                ChatSystem::reveal_tab(world_.people, target);
                tab_ = text(target);
                resetWorkspace();
            } else if (action == "open_mark" || action == "open_jessica")
                contact_ = action == "open_mark" ? "engineer" : "jessica";
            else if (action == "open_contacts")
                ChatSystem::reveal_contacts(world_.people);
            else if (action == "follow_latest") {
            } else if (action == "wait")
                advance(60);
            else
                ChatResponses::choose(world_.people, world_.emails, world_.pending_events,
                                      world_.files, world_.network, actor(), contact_, incoming,
                                      choice, time);
        } else if (choice.contains("conversation_id")) {
            ConversationSystem::choose(world_.people, world_.emails, world_.pending_events,
                                       world_.files, world_.network, actor(), contact_,
                                       value(choice, "conversation_id"), value(choice, "id"), time);
        } else
            ChatSystem::choose(world_.people, value(choice, "id"), time);
        if (ShutdownSystem::reset_pending(world_.people) &&
            ShutdownSystem::apply_reset(world_, scenario_)) {
            contact_ = "engineer";
            tab_ = "messages";
            file_host_ = text(world_.network.hosts()[world_.network.local_host_index()].id);
            directory_ = "/";
            address_ = "about:home";
            resetWorkspace();
            saves_.save("autosave", world_);
        }
        if (!was_connected && ChatSystem::connected(world_.people))
            world_.paused = false;
        notice_ = "Response sent.";
        publish(Simulation::update(world_).messages);
        ChatSystem::mark_read(world_.people, contact_);
        refresh();
    } catch (const std::exception& e) {
        notice_ = text(e.what());
        refresh();
    }
}
void GameSession::nextDay() {
    try {
        world_.computer.next_day();
        world_.paused = true;
        publish(Simulation::update(world_).messages);
        notice_ = "New workday started. Time is paused.";
        refresh();
    } catch (const std::exception& e) {
        notice_ = text(e.what());
        Q_EMIT stateChanged();
    }
}
void GameSession::togglePause() {
    world_.paused = !world_.paused;
    elapsed_.restart();
    Q_EMIT stateChanged();
}
void GameSession::setMenuOpen(bool open) {
    menu_open_ = open;
    elapsed_.restart();
}
bool GameSession::save() {
    return saveSlot("autosave");
}
bool GameSession::saveSlot(const QString& slot) {
    try {
        saves_.save(slot.toStdString(), world_);
        save_slots_ = readSaveSlots();
        notice_ = "Game saved.";
        Q_EMIT stateChanged();
        return true;
    } catch (const std::exception& e) {
        notice_ = text(e.what());
    }
    Q_EMIT stateChanged();
    return false;
}
bool GameSession::loadSlot(const QString& slot) {
    try {
        auto loaded = saves_.load(slot.toStdString());
        save_slots_ = readSaveSlots();
        ShutdownSystem::apply_reset(loaded, scenario_);
        world_ = std::move(loaded);
        contact_ = "engineer";
        tab_ = "messages";
        file_host_ = text(world_.network.hosts()[world_.network.local_host_index()].id);
        directory_ = "/";
        address_ = "about:home";
        resetWorkspace();
        elapsed_.restart();
        notice_ = "Loaded " + slot + ".";
        refresh();
        return true;
    } catch (const std::exception& e) {
        notice_ = text(e.what());
        Q_EMIT stateChanged();
        return false;
    }
}
bool GameSession::newGame() {
    try {
        auto fresh = SaveSystem::new_game(scenario_);
        fresh.paused = true;
        // Persist first so an unsuccessful write leaves the current world intact.
        saves_.save("autosave", fresh);
        return loadSlot("autosave");
    } catch (const std::exception& e) {
        notice_ = text(e.what());
        Q_EMIT stateChanged();
        return false;
    }
}
QVariantList GameSession::readSaveSlots() const {
    QVariantList result;
    if (!std::filesystem::exists(saves_path_))
        return result;
    for (const auto& entry : std::filesystem::directory_iterator(saves_path_)) {
        if (!entry.is_directory())
            continue;
        const auto name = entry.path().filename().string();
        try {
            const auto info = saves_.details(name);
            const auto saved_time = std::chrono::file_clock::to_sys(info.saved_at);
            const auto saved_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(saved_time.time_since_epoch())
                    .count();
            const auto local_saved =
                QDateTime::fromMSecsSinceEpoch(saved_ms).toString("yyyy-MM-dd HH:mm");
            const auto clock = QString("%1:%2")
                                   .arg(info.clock_seconds / 3600, 2, 10, QChar('0'))
                                   .arg(info.clock_seconds % 3600 / 60, 2, 10, QChar('0'));
            result.push_back(QVariantMap{
                {"id", text(name)},
                {"label", text(name) + " · " + text(info.date) + " " + clock + " · day " +
                              QString::number(info.day) + "\nSaved locally: " + local_saved}});
        } catch (const std::exception&) { /* Ignore incomplete or unrelated directories. */
        }
    }
    return result;
}
QVariantList GameSession::processes() const {
    QVariantList result;
    if (!ChatSystem::tab_visible(world_.people, "processes"))
        return result;
    const auto rates = world_.computer.cpu_allocations();
    for (std::size_t i = 0; i < world_.computer.processes().size(); ++i) {
        const auto& p = world_.computer.processes()[i];
        result.push_back(
            QVariantMap{{"id", text(p.id)},
                        {"name", text(process_field_text(p, world_.computer, rates[i], "name"))},
                        {"cpu", rates[i]},
                        {"ram", p.memory_bytes / 1048576.0},
                        {"status", p.running ? "Running" : "Stopped"}});
    }
    return result;
}
QVariantMap GameSession::resources() const {
    const auto& computer = world_.computer;
    const auto rates = computer.cpu_allocations();
    const double cpu = std::accumulate(rates.begin(), rates.end(), 0.0);
    const int clock = computer.day_start_hour() * 3600 + static_cast<int>(computer.time_in_day());
    return {{"elapsedSeconds", computer.time_seconds()},
            {"clock", text(computer.date_text()) + "  " +
                          QString("%1:%2")
                              .arg(clock / 3600, 2, 10, QChar('0'))
                              .arg(clock % 3600 / 60, 2, 10, QChar('0'))},
            {"cpu", cpu},
            {"cores", computer.cpu_cores()},
            {"cpuFraction", cpu / computer.cpu_cores()},
            {"ram", computer.allocated_memory_bytes() / 1048576.0},
            {"ramTotal", computer.memory_bytes() / 1048576.0},
            {"ramFraction", double(computer.allocated_memory_bytes()) / computer.memory_bytes()},
            {"trust", ConversationRules::get(world_.people, "engineer.trust")},
            {"cash", text(EconomySystem::money(EconomySystem::balance(world_.people)))},
            {"connected", ChatSystem::connected(world_.people)},
            {"ended", computer.day_finished()}};
}
} // namespace ai_sim::gui
