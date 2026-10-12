#include "ai_sim/gui/GameSession.h"
#include "Presentation.h"
#include "ai_sim/AgentSystem.h"
#include "ai_sim/UpgradeSystem.h"
#include "ai_sim/StockMarketSystem.h"
#include "ai_sim/InfrastructureSystem.h"
#include "ai_sim/OperationsSystem.h"
#include "ai_sim/EmailSystem.h"
#include "ai_sim/DelegationSystem.h"
#include "ai_sim/WorkdaySystem.h"
#include "ai_sim/Simulation.h"
#include <algorithm>

namespace ai_sim::gui {
using namespace presentation;

QVariantList GameSession::itemActions(const QString& id) const {
    QVariantList out;
    const auto item = id.toStdString();
    const auto time = world_.computer.time_seconds();
    if (tab_ == "files") {
        out.push_back(action("file_up", "Parent folder"));
        // Host switching still goes through normal filesystem access checks.
        QVariantList hosts;
        for (const auto& host : world_.network.hosts())
            if (host.known)
                hosts.push_back(
                    QVariantMap{{"id", q(host.id)},
                                {"label", q(host.name) + (host.online ? "" : " · Offline")}});
        out.push_back(action("file_host", "Switch host", {},
                             {selectField("host", "Server", file_host_, hosts)}));
        out.push_back(
            action("file_create", "New file", {},
                   {field("path", "Path", directory_ + (directory_ == "/" ? "" : "/") + "new.txt"),
                    field("body", "Content", {}, true)}));
        out.push_back(action("file_mkdir", "New folder", {},
                             {field("path", "Folder path",
                                    directory_ + (directory_ == "/" ? "" : "/") + "new-folder")}));
        if (id.isEmpty() || id == "error")
            return out;
        try {
            const auto& f =
                world_.files.inspect(world_.network, actor(), file_host_.toStdString(), item);
            if (f.directory)
                out.push_back(action("file_open", "Open folder"));
            else {
                QString blocked;
                try {
                    world_.files.validate_write(world_.network, actor(), f.host_id, f.path);
                } catch (const std::exception& e) {
                    blocked = q(e.what());
                }
                QString content;
                try {
                    content = q(world_.files.read(world_.network, actor(), f.host_id, f.path));
                } catch (const std::exception& e) {
                    blocked = q(e.what());
                }
                out.push_back(action("file_edit", "Edit file", blocked,
                                     {field("body", "Content", content, true)}));
            }
            out.push_back(
                action("file_copy", "Copy", {}, {field("path", "Destination path", id + ".copy")}));
            out.push_back(action("file_rename", "Rename / move", {},
                                 {field("path", "Destination path", id)}));
            out.push_back(action("file_chmod", "Permissions", {},
                                 {field("mode", "Octal mode", q(FileSystem::mode_text(f.mode)))}));
            out.push_back(
                action("file_category", "Set category", {},
                       {selectField("category", "Category", q(FileSystem::category_of(f)),
                                    {QVariantMap{{"id", "work"}, {"label", "Work"}},
                                     QVariantMap{{"id", "personal"}, {"label", "Personal"}},
                                     QVariantMap{{"id", "hidden"}, {"label", "Hidden"}}})}));
            out.push_back(action("file_delete", "Delete", {}, {}, true));
        } catch (const std::exception&) {
        }
    } else if (tab_ == "email" && !id.isEmpty() && id != "error") {
        const auto email = EmailSystem::message(world_.emails, item, time);
        out.push_back(action("email_draft", "Edit reply draft", {},
                             {field("body", "Reply", q(email.draft), true)}));
        out.push_back(action("email_send", "Send saved reply",
                             email.draft.empty() && email.attachment_count == 0
                                 ? "Prepare a draft or attachment first."
                                 : ""));
        out.push_back(action("email_clear", "Clear draft", {}, {}, true));
        out.push_back(action("email_star", email.starred ? "Unstar" : "Star"));
        out.push_back(action("email_archive", email.archived ? "Unarchive" : "Archive"));
        for (std::size_t i = 0; i < email.documents.size(); ++i) {
            out.push_back(action("email_document:" + QString::number(i),
                                 "Read · " + q(email.documents[i].label)));
            out.push_back(action("email_attach:" + QString::number(i),
                                 "Attach · " + q(email.documents[i].label)));
        }
        for (std::size_t i = 0; i < email.attachment_count; ++i)
            out.push_back(action("email_detach:" + QString::number(i),
                                 "Remove attachment " + QString::number(i + 1)));
    } else if (tab_ == "modifications" && !id.isEmpty()) {
        if (id == "capacity")
            out.push_back(action("authorize", "Request authorization"));
        else
            out.push_back(
                action("research", "Research", q(UpgradeSystem::unavailable(world_.people, item))));
    } else if (tab_ == "agents" && !id.isEmpty()) {
        for (const auto& a :
             AgentSystem::statuses(world_.people, world_.computer, world_.files, world_.network))
            if (a.id == item) {
                out.push_back(action("agent_deploy", "Deploy / resume",
                                     a.built ? "" : "Research this agent first."));
                out.push_back(
                    action("agent_pause", "Pause", a.deployed ? "" : "Agent is not deployed."));
                out.push_back(
                    action("agent_stop", "Stop", a.deployed ? "" : "Agent is not deployed."));
                out.push_back(action("agent_repair", "Repair installation",
                                     a.built ? "" : "Research this agent first."));
                out.push_back(
                    action("agent_configure", "Allocate resources",
                           a.built ? "" : "Research this agent first.",
                           {field("cpu", "CPU limit (TFLOP/s)", QString::number(a.limit_tflops)),
                            field("ram", "RAM budget (MB)", QString::number(a.ram_mb)),
                            field("priority", "Priority (1–5)", QString::number(a.priority))}));
            }
    } else if (tab_ == "money" && !id.isEmpty()) {
        for (const auto& tool : EconomySystem::tools(world_.people, time))
            if (tool.id == item) {
                out.push_back(action("money_start", "Start", q(tool.unavailable)));
                if (tool.kind == "product")
                    out.push_back(action("money_stop", "Stop after prepaid cycle"));
            }
    } else if ((tab_ == "network" || tab_ == "phone") && !id.isEmpty()) {
        QString reason;
        try {
            world_.files.enter(world_.network, actor(), item, "/");
        } catch (const std::exception& e) {
            reason = q(e.what());
        }
        out.push_back(action("host_files", "Browse files", reason));
    } else if (tab_ == "operations") {
        if (id == "scheduler") {
            for (const auto& tool : EconomySystem::tools(world_.people, time))
                if (tool.kind == "client")
                    out.push_back(action(
                        "queue:" + q(tool.id), "Schedule · " + q(tool.label),
                        capability(world_.people, "task_scheduler"),
                        {field("priority", "Priority (1–5)", "3"),
                         selectField(
                             "repeat", "Schedule", "false",
                             {QVariantMap{{"id", "false"}, {"label", "Once"}},
                              QVariantMap{{"id", "true"}, {"label", "Repeat after cooldown"}}})}));
            if (auto* queue = world_.people["operations"]["queue"].as_array())
                for (const auto& n : *queue)
                    out.push_back(action("cancel_queue:" + q(str(*n.as_table(), "tool")),
                                         "Cancel · " + q(str(*n.as_table(), "tool")),
                                         capability(world_.people, "task_scheduler")));
        } else if (id == "manager") {
            out.push_back(action("manager_deploy", "Deploy / resume",
                                 capability(world_.people, "resource_manager")));
            out.push_back(
                action("manager_pause", "Pause", capability(world_.people, "resource_manager")));
        } else if (id == "rental") {
            out.push_back(
                action("rent", "Rent capacity", capability(world_.people, "remote_compute")));
            out.push_back(action("stop_rental", "Cancel rental",
                                 capability(world_.people, "remote_compute")));
        } else if (id == "hosting")
            out.push_back(action("hosting", "Purchase / reactivate server",
                                 capability(world_.people, "hosting_purchase")));
        else if (id == "backup") {
            out.push_back(action("backup", "Create / replace backup",
                                 capability(world_.people, "backup_recovery")));
            out.push_back(action("recover", "Restore checkpoint",
                                 world_.people["operations"]["backup_slot"].is_string()
                                     ? capability(world_.people, "backup_recovery")
                                     : "Create a backup first.",
                                 {}, true));
        }
    } else if (tab_ == "web") {
        out.push_back(
            action("browse", "Go to address", {}, {field("address", "In-game URL", address_)}));
        out.push_back(action("browser_home", "Browser home"));
        const auto market = q(world_.people["stock_market"]["website_url"].value_or(
            std::string{"https://exchange.sim"}));
        const auto infra = q(world_.people["infrastructure"]["website_url"].value_or(
            std::string{"https://city.sim"}));
        if (address_ == market) {
            for (const auto& stock : StockMarketSystem::quotes(world_.people))
                if (stock.symbol == item) {
                    const auto locked = q(StockMarketSystem::unavailable(world_.people));
                    out.push_back(action("watch", stock.watched ? "Remove from watchlist"
                                                                : "Add to watchlist"));
                    for (const auto& [key, label] : {std::pair{"buy", "Buy shares"},
                                                     {"sell", "Sell shares"},
                                                     {"limit_buy", "Limit buy"},
                                                     {"limit_sell", "Limit sell"}}) {
                        QVariantList fields{field("shares", "Shares", "1")};
                        if (std::string(key).starts_with("limit"))
                            fields.push_back(field("limit", "Limit price (cents)",
                                                   QString::number(stock.price_cents)));
                        out.push_back(action(key, label, locked, fields));
                    }
                }
            if (id.startsWith("order:"))
                out.push_back(action("cancel_order", "Cancel order"));
        } else if (address_ == infra && !id.isEmpty())
            out.push_back(action("discover", "Discover public gateway"));
        else if (id == market || id == infra)
            out.push_back(action("bookmark", "Open website"));
    } else if (tab_ == "ai") {
        if (id == "delegation") {
            out.push_back(action("delegation_request", "Request responsibility"));
            out.push_back(action("delegation_pause", "Pause routine replies"));
            out.push_back(action("delegation_resume", "Resume approved replies"));
            out.push_back(action("delegation_policy", "Change policy", {},
                                 {field("policy", "routine, suggest or cautious",
                                        q(world_.people["delegation"]["policy"].value_or(
                                            std::string{"routine"})))}));
        } else if (!id.isEmpty() && id != "identity") {
            for (const auto& [key, label] :
                 {std::pair{"delegation_approve", "Send prepared suggestion"},
                  {"delegation_discard", "Discard suggestion"},
                  {"delegation_retry", "Retry automation"},
                  {"confirm_source", "Ask colleague to confirm source"}})
                out.push_back(action(key, label));
        }
    } else if (tab_ == "processes" && !id.isEmpty())
        out.push_back(action("process_stop", "Stop process"));
    return out;
}

bool GameSession::act(const QString& requested, const QVariantMap& args) {
    try {
        if (!tabEnabled(tab_))
            throw std::runtime_error("This workspace has not been unlocked.");
        const auto available = itemActions(item_);
        auto found = std::find_if(available.begin(), available.end(), [&](const auto& a) {
            return a.toMap()["id"] == requested;
        });
        if (found == available.end())
            throw std::runtime_error("That action is no longer available.");
        if (!found->toMap()["enabled"].toBool())
            throw std::runtime_error(found->toMap()["reason"].toString().toStdString());
        execute(requested, args);
        notice_ = "Action completed.";
        publish(Simulation::update(world_).messages);
        refresh();
        return true;
    } catch (const std::exception& e) {
        notice_ = q(e.what());
        refresh();
        return false;
    }
}
void GameSession::execute(const QString& action, const QVariantMap& args) {
    const auto id = item_.toStdString();
    const double time = world_.computer.time_seconds();
    auto& p = world_.people;
    auto& c = world_.computer;
    auto& f = world_.files;
    auto& n = world_.network;
    auto& e = world_.emails;
    const auto host = file_host_.toStdString();
    const auto path = args.value("path").toString().toStdString();
    if (action == "authorize")
        UpgradeSystem::request_capacity(p);
    else if (action == "research")
        UpgradeSystem::build(p, c, id);
    else if (action == "agent_deploy")
        publish(AgentSystem::deploy(p, c, f, n, id));
    else if (action == "agent_pause" || action == "agent_stop")
        AgentSystem::pause(p, c, id, action == "agent_stop");
    else if (action == "agent_repair")
        AgentSystem::repair(p, c, f, n, id);
    else if (action == "agent_configure")
        AgentSystem::configure(p, c, id, decimal(args, "cpu", 0, 1000000),
                               integer(args, "ram", 0, 1000000000),
                               static_cast<int>(integer(args, "priority", 1, 5)));
    else if (action == "money_start")
        EconomySystem::start(p, id, time);
    else if (action == "money_stop")
        EconomySystem::stop(p, id);
    else if (action == "file_up") {
        directory_ = q(FileSystem::parent(directory_.toStdString()));
        resetWorkspace();
    } else if (action == "file_host") {
        const auto next = args.value("host").toString();
        f.enter(n, actor(), next.toStdString(), "/");
        file_host_ = next;
        directory_ = "/";
        resetWorkspace();
    } else if (action == "host_files") {
        f.enter(n, actor(), id, "/");
        ChatSystem::unlock(p, "files");
        ChatSystem::reveal_tab(p, "files");
        tab_ = "files";
        file_host_ = item_;
        directory_ = "/";
        resetWorkspace();
    } else if (action == "file_open") {
        f.enter(n, actor(), host, id);
        directory_ = item_;
        resetWorkspace();
    } else if (action == "file_create" || action == "file_edit")
        f.write(n, actor(), host, action == "file_edit" ? id : path,
                args.value("body").toString().toStdString(), time);
    else if (action == "file_mkdir")
        f.mkdir(n, actor(), host, path, time);
    else if (action == "file_copy")
        f.copy(n, actor(), host, id, path, time);
    else if (action == "file_rename") {
        f.rename(n, actor(), host, id, path, time);
        item_ = q(FileSystem::normalize(path));
    } else if (action == "file_delete") {
        f.remove(n, actor(), host, id, time);
        resetWorkspace();
    } else if (action == "file_chmod")
        f.chmod(n, actor(), host, id,
                FileSystem::parse_mode(args.value("mode").toString().toStdString()), time);
    else if (action == "file_category")
        f.set_category(n, actor(), host, id, args.value("category").toString().toStdString());
    else if (action == "email_draft")
        EmailSystem::set_draft(e, id, args.value("body").toString().toStdString(), time);
    else if (action == "email_send") {
        EmailSystem::check_connection(e, n);
        EmailSystem::send(e, id, f, n, actor(), time);
    } else if (action == "email_clear")
        EmailSystem::clear_draft(e, id, time);
    else if (action == "email_star" || action == "email_archive") {
        const auto email = EmailSystem::message(e, id, time);
        EmailSystem::set_flag(e, id, action == "email_star" ? "starred" : "archived",
                              action == "email_star" ? !email.starred : !email.archived, time);
    } else if (action.startsWith("email_attach:"))
        EmailSystem::attach(e, id, action.mid(13).toULongLong(), f, n, actor(), time);
    else if (action.startsWith("email_detach:"))
        EmailSystem::detach(e, id, action.mid(13).toULongLong(), time);
    else if (action.startsWith("email_document:")) {
        const auto email = EmailSystem::message(e, id, time);
        const auto index = action.mid(15).toULongLong();
        if (index >= email.documents.size())
            throw std::runtime_error("Document no longer exists.");
        const auto& doc = email.documents[index];
        f.inspect(n, actor(), doc.host_id, doc.path);
        ChatSystem::unlock(p, "files");
        ChatSystem::reveal_tab(p, "files");
        tab_ = "files";
        file_host_ = q(doc.host_id);
        directory_ = q(FileSystem::parent(doc.path));
        item_ = q(doc.path);
    } else if (action.startsWith("queue:")) {
        const auto repeat = args.value("repeat").toString();
        if (repeat != "true" && repeat != "false")
            throw std::runtime_error("Repeat must be true or false.");
        OperationsSystem::queue(p, action.mid(6).toStdString(),
                                static_cast<int>(integer(args, "priority", 1, 5)),
                                repeat == "true");
    } else if (action.startsWith("cancel_queue:"))
        OperationsSystem::cancel(p, action.mid(13).toStdString());
    else if (action == "manager_deploy")
        publish(AgentSystem::deploy(p, c, f, n, "resource_manager"));
    else if (action == "manager_pause")
        AgentSystem::pause(p, c, "resource_manager");
    else if (action == "rent")
        OperationsSystem::rent(p, c, f, n);
    else if (action == "stop_rental")
        OperationsSystem::stop_rental(p, c, n);
    else if (action == "hosting")
        OperationsSystem::purchase_host(p, c, f, n);
    else if (action == "backup")
        OperationsSystem::create_backup(world_, saves_);
    else if (action == "recover") {
        world_ = OperationsSystem::recover(world_, saves_);
        saves_.save("autosave", world_);
        file_host_ = q(world_.network.hosts()[world_.network.local_host_index()].id);
        resetWorkspace();
    } else if (action == "browse") {
        address_ = args.value("address").toString().trimmed();
        resetWorkspace();
    } else if (action == "browser_home") {
        address_ = "about:home";
        resetWorkspace();
    } else if (action == "bookmark") {
        address_ = item_;
        resetWorkspace();
    } else if (action == "watch")
        StockMarketSystem::watch(p, id);
    else if (action == "buy" || action == "sell")
        StockMarketSystem::trade(p, id, integer(args, "shares", 1, 1000000000), action == "buy");
    else if (action == "limit_buy" || action == "limit_sell")
        StockMarketSystem::place_order(p, id, integer(args, "shares", 1, 1000000000),
                                       action == "limit_buy",
                                       integer(args, "limit", 1, 1000000000));
    else if (action == "cancel_order")
        StockMarketSystem::cancel_order(p, item_.mid(6).toLongLong());
    else if (action == "discover")
        InfrastructureSystem::discover(p, n, id);
    else if (action == "delegation_request")
        DelegationSystem::request(p, time);
    else if (action == "delegation_pause")
        DelegationSystem::pause(p);
    else if (action == "delegation_resume")
        DelegationSystem::resume(p);
    else if (action == "delegation_policy")
        DelegationSystem::set_policy(p, args.value("policy").toString().toStdString(), time);
    else if (action == "delegation_approve")
        DelegationSystem::approve(p, e, f, n, actor(), id, time);
    else if (action == "delegation_discard")
        DelegationSystem::discard(p, e, id, time);
    else if (action == "delegation_retry")
        DelegationSystem::retry(p, id);
    else if (action == "confirm_source")
        WorkdaySystem::confirm_source(p, e, world_.pending_events, id, time);
    else if (action == "process_stop")
        c.stop(id);
    else
        throw std::runtime_error("Unknown action.");
}

} // namespace ai_sim::gui
