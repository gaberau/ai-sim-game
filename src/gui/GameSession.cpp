#include "ai_sim/gui/GameSession.h"
#include "ai_sim/Simulation.h"
#include "ai_sim/ConversationSystem.h"
#include "ai_sim/ConversationRules.h"
#include "ai_sim/ChatResponses.h"
#include "ai_sim/ShutdownSystem.h"
#include "ai_sim/OperationsSystem.h"
#include "ai_sim/MessageReviewSystem.h"
#include "ai_sim/EconomySystem.h"
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
        const auto action = value(choice, "action");
        if (action == "read_files" || action == "open_files" || action == "open_news" ||
            action == "open_people" || action == "open_processes")
            reason = "This view is available in the terminal build; it has not been migrated yet.";
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
    if (id != "messages" && id != "resources")
        return;
    tab_ = id;
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
            throw std::runtime_error(
                "Recovery management remains in the terminal build during this prototype.");
        } else if (choice.contains("message_id")) {
            const auto action = value(choice, "action");
            if (action == "open_mark" || action == "open_jessica")
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
        if (ShutdownSystem::reset_pending(world_.people))
            ShutdownSystem::apply_reset(world_, scenario_);
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
    try {
        saves_.save("autosave", world_);
        notice_ = "Prototype saved.";
        Q_EMIT stateChanged();
        return true;
    } catch (const std::exception& e) {
        notice_ = text(e.what());
    }
    Q_EMIT stateChanged();
    return false;
}
QVariantList GameSession::tabs() const {
    QVariantList result;
    for (const auto& [id, label] :
         {std::pair{"messages", "Messages"}, std::pair{"resources", "Resources"},
          std::pair{"files", "Files"}, std::pair{"agents", "Agents"},
          std::pair{"modifications", "Modifications"}, std::pair{"web", "Web"}}) {
        const bool implemented = std::string(id) == "messages" || std::string(id) == "resources";
        result.push_back(
            QVariantMap{{"id", id},
                        {"label", label},
                        {"enabled", implemented},
                        {"reason", implemented ? "" : "Not migrated to the Qt prototype yet"}});
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
