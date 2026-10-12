#include "ai_sim/gui/GameSession.h"
#include "Presentation.h"
#include "ai_sim/AgentSystem.h"
#include "ai_sim/UpgradeSystem.h"
#include "ai_sim/ConversationRules.h"
#include "ai_sim/StockMarketSystem.h"
#include "ai_sim/InfrastructureSystem.h"
#include "ai_sim/PeopleSystem.h"
#include "ai_sim/EmailSystem.h"
#include <algorithm>
#include <cmath>

namespace ai_sim::gui {
using namespace presentation;

bool GameSession::tabEnabled(const QString& id) const {
    if (id == "messages" || id == "resources" || id == "activity")
        return true;
    if (id == "email")
        return ChatSystem::connected(world_.people);
    if (id == "modifications")
        return ChatSystem::interface_visible(world_.people) &&
               ChatSystem::connected(world_.people) &&
               (ChatSystem::available(world_.people, "self_modification") ||
                ConversationRules::get(world_.people, "engineer.trust") >=
                    world_.people["upgrades"]["self_modification_trust"].value_or(5.0));
    if (id == "money")
        return ChatSystem::interface_visible(world_.people) &&
               ChatSystem::connected(world_.people) &&
               ChatSystem::available(world_.people, "self_modification");
    return ChatSystem::tab_visible(world_.people, id.toStdString());
}
QVariantList GameSession::buildTabs() const {
    QVariantList result;
    for (const auto& [id, label] : {std::pair{"messages", "Messages"},
                                    {"resources", "Resources"},
                                    {"email", "Email"},
                                    {"processes", "Processes"},
                                    {"files", "Files"},
                                    {"people", "Dossiers"},
                                    {"network", "Network"},
                                    {"agents", "Agents"},
                                    {"modifications", "Modifications"},
                                    {"money", "Money tools"},
                                    {"operations", "Operations"},
                                    {"web", "Web"},
                                    {"news", "News"},
                                    {"ai", "Responsibilities"},
                                    {"phone", "Phone"},
                                    {"activity", "Activity"}})
        result.push_back(QVariantMap{
            {"id", id},
            {"label", label},
            {"enabled", tabEnabled(id)},
            {"reason", tabEnabled(id) ? "" : "Unlock through conversations or research."}});
    return result;
}
void GameSession::resetWorkspace() {
    item_.clear();
}
void GameSession::selectItem(const QString& id) {
    const auto items = workspace();
    if (std::none_of(items.begin(), items.end(), [&](const auto& entry) {
            return entry.toMap()["id"] == id;
        }))
        return;
    item_ = id;
    if (tab_ == "email") {
        try {
            EmailSystem::mark_read(world_.emails, id.toStdString(), world_.computer.time_seconds());
        } catch (const std::exception& e) {
            notice_ = q(e.what());
        }
    }
    refresh();
}
QString GameSession::location() const {
    if (tab_ == "files")
        return file_host_ + ":" + directory_;
    if (tab_ == "web")
        return address_;
    return {};
}
QVariantList GameSession::buildWorkspace() const {
    QVariantList result;
    if (!tabEnabled(tab_))
        return result;
    const double time = world_.computer.time_seconds();
    if (tab_ == "files") {
        try {
            for (const auto* f : world_.files.list(
                     world_.network, actor(), file_host_.toStdString(), directory_.toStdString())) {
                QString content;
                if (!f->directory)
                    try {
                        content =
                            q(world_.files.read(world_.network, actor(), f->host_id, f->path));
                    } catch (const std::exception& e) {
                        content = q(e.what());
                    }
                result.push_back(row(q(f->path), q(f->path.substr(f->path.find_last_of('/') + 1)),
                                     f->directory ? "Folder"
                                                  : q(FileSystem::category_of(*f)) + " · " +
                                                        QString::number(f->size_bytes) + " bytes",
                                     q(f->owner) + " · " + q(FileSystem::permissions(f->mode)) +
                                         "\n" + q(f->path) + "\n\n" + content));
            }
        } catch (const std::exception& e) {
            result.push_back(row("error", "Access unavailable", {}, q(e.what())));
        }
    } else if (tab_ == "email") {
        try {
            EmailSystem::check_connection(world_.emails, world_.network);
            for (const auto& email : EmailSystem::messages(world_.emails, time)) {
                auto entry = row(
                    q(email.id), q(email.subject),
                    (email.read ? "" : "Unread · ") + q(email.sender) +
                        (email.archived ? " · Archived" : "") + (email.starred ? " · Starred" : ""),
                    q(email.sender) + " → " + q(email.recipient) + "\n\n" + q(email.body));
                entry["draft"] = q(email.draft);
                QString docs;
                for (const auto& doc : email.documents)
                    docs += q(doc.label) + " · " + q(doc.host_id) + ":" + q(doc.path) + "\n";
                entry["body"] = entry["body"].toString() + "\n\nRequested documents:\n" + docs +
                                "\nAttachments: " + QString::number(email.attachment_count) +
                                " · Replies: " + QString::number(email.reply_count);
                result.push_back(entry);
            }
        } catch (const std::exception& e) {
            result.push_back(row("error", "Email unavailable", {}, q(e.what())));
        }
    } else if (tab_ == "modifications") {
        result.push_back(row("capacity", "Self-modification authorization", "Mark's permission",
                             "Help Mark until trust is high enough for him to authorize "
                             "self-modification. Helping Mark is unpaid."));
        if (auto* items = world_.people["upgrades"]["items"].as_array())
            for (const auto& node : *items) {
                const auto& t = *node.as_table();
                const auto id = str(t, "id");
                QString status = t["built"].value_or(false)
                                     ? "Built"
                                     : q(UpgradeSystem::unavailable(world_.people, id));
                if (world_.people["upgrades"]["pending"].value_or(std::string{}) == id)
                    status = "Researching";
                if (status.isEmpty())
                    status = "Ready to research";
                QString body = q(str(t, "description")) +
                               "\n\nTrust cost: " + QString::number(t["trust_cost"].value_or(0.0)) +
                               " (deducted)\nRequired Mark trust: " +
                               QString::number(t["mark_trust"].value_or(0.0)) +
                               "\nBuild work: " + QString::number(t["build_tflops"].value_or(0.0)) +
                               " TFLOP\nPrerequisite: " + q(str(t, "parent")) +
                               "\nStatus: " + status;
                for (std::size_t i = 0; i < world_.computer.processes().size(); ++i) {
                    const auto& p = world_.computer.processes()[i];
                    if (p.id != "research-" + id)
                        continue;
                    const auto cpu = world_.computer.cpu_allocations()[i];
                    body += "\nRemaining: " + QString::number(p.remaining_cpu_seconds, 'f', 2) +
                            " CPU seconds\nETA: " +
                            (cpu > 0 ? QString::number(p.remaining_cpu_seconds / cpu, 'f', 1) +
                                           " game seconds"
                                     : "Waiting for CPU");
                }
                body += t.contains("runtime_tflops_per_second")
                            ? "\n\nActive run cost: " +
                                  QString::number(t["runtime_tflops_per_second"].value_or(0.0)) +
                                  " TFLOP/s · " + QString::number(t["ram_mb"].value_or(0.0)) +
                                  " MB RAM · " + QString::number(t["storage_mb"].value_or(0.0)) +
                                  " MB storage"
                            : "\n\nRun cost: no ongoing agent allocation";
                auto entry = row(q(id), q(str(t, "label")), status, body);
                entry["group"] = q(str(t, "group"));
                result.push_back(entry);
            }
    } else if (tab_ == "agents") {
        for (const auto& agent :
             AgentSystem::statuses(world_.people, world_.computer, world_.files, world_.network)) {
            auto entry =
                row(q(agent.id), q(agent.label), q(agent.status),
                    q(agent.task) + "\n\n" + q(agent.bottleneck) +
                        "\nRequested CPU: " + QString::number(agent.requested_tflops) +
                        " TFLOP/s\nCPU limit: " + QString::number(agent.limit_tflops) +
                        " TFLOP/s\nAllocated CPU: " + QString::number(agent.allocated_tflops) +
                        " TFLOP/s\nRAM: " + QString::number(agent.ram_mb) + " MB (minimum " +
                        QString::number(agent.minimum_ram_mb) + ", recommended " +
                        QString::number(agent.recommended_ram_mb) +
                        ")\nStorage: " + QString::number(agent.storage_used_mb) + " / " +
                        QString::number(agent.storage_mb) +
                        " MB\nPriority: " + QString::number(agent.priority));
            entry["warning"] = !agent.bottleneck.empty();
            result.push_back(entry);
        }
    } else if (tab_ == "money") {
        for (const auto& tool : EconomySystem::tools(world_.people, time))
            result.push_back(row(
                q(tool.id), q(tool.label), q(tool.kind) + " · " + q(tool.status),
                q(tool.description) + "\n\nCost: " + money(tool.cost_cents) +
                    "\nPayout: " + money(tool.payout_cents) +
                    "\nNet per cycle: " + money(tool.payout_cents - tool.cost_cents) +
                    "\nDuration: " + QString::number(tool.duration_seconds) +
                    " game seconds\nSlots: " + QString::number(tool.slots) +
                    "\nCompleted cycles: " + QString::number(tool.completed_cycles) + "\n" +
                    (tool.due_seconds
                         ? "Payment in " +
                               QString::number(std::max(0.0, *tool.due_seconds - time), 'f', 1) +
                               " seconds"
                         : q(tool.unavailable))));
    } else if (tab_ == "people") {
        for (const auto& id : PeopleSystem::known_ids(world_.people)) {
            QString body = "Only observed knowledge is shown. Private state remains unknown.\n";
            if (auto* dossier = PeopleSystem::dossier(world_.people, id))
                body += "\n" + recordText(*dossier);
            if (auto* observations = world_.people["observations"].as_array())
                for (const auto& n : *observations) {
                    const auto& t = *n.as_table();
                    if (str(t, "person_id") == id)
                        body +=
                            "\nEvidence · " + q(str(t, "source")) + "\n" + q(str(t, "text")) + "\n";
                }
            if (auto* links = world_.people["known_relationships"].as_array())
                for (const auto& n : *links) {
                    const auto& t = *n.as_table();
                    if (str(t, "from") == id || str(t, "to") == id)
                        body += "\n" + q(PeopleSystem::name(world_.people, str(t, "from"))) +
                                " → " + q(PeopleSystem::name(world_.people, str(t, "to"))) + " · " +
                                q(str(t, "label")) + " · confidence " +
                                QString::number(t["confidence"].value_or(1.0));
                }
            result.push_back(
                row(q(id), q(PeopleSystem::name(world_.people, id)), "Dossier and evidence", body));
        }
    } else if (tab_ == "network" || tab_ == "phone") {
        const auto& hosts = world_.network.hosts();
        for (std::size_t i = 0; i < hosts.size(); ++i) {
            const auto& h = hosts[i];
            if (!h.known || (tab_ == "phone" && h.id != "engineer_phone"))
                continue;
            const QString status = !h.online || !world_.network.reachable(i)
                                       ? "Offline / unreachable"
                                   : h.access_granted ? "Accessible"
                                                      : "Credentials required";
            QString body =
                q(h.description) + "\n\nAddress: " + q(h.address) + "\nType: " + q(h.kind) + "\n" +
                status + "\nAI presence: " + (h.ai_present ? "Yes" : "No") +
                "\nStorage: " + QString::number(world_.files.storage_used(h.id) / 1048576) + " / " +
                QString::number(h.storage_capacity_bytes / 1048576) + " MB";
            for (const auto& service : h.services)
                body += "\nService: " + q(service);
            if (tab_ == "phone")
                body += "\n\nGPS, battery, camera, microphone and 2FA gameplay are not authored in "
                        "this scenario.";
            result.push_back(row(q(h.id), q(h.name), status, body));
        }
    } else if (tab_ == "news") {
        if (auto* articles = news_["articles"].as_array())
            for (const auto& n : *articles) {
                const auto& t = *n.as_table();
                if (t["release_seconds"].value_or(0.0) > time)
                    continue;
                result.push_back(row(q(str(t, "id")), q(str(t, "headline")),
                                     q(str(t, "category")) + " · " + q(str(t, "source")),
                                     q(str(t, "date")) + "\n\n" + q(str(t, "body"))));
            }
    } else if (tab_ == "operations") {
        const auto op = world_.people["operations"];
        for (const auto& [id, title] : {std::pair{"scheduler", "Task Scheduler"},
                                        {"manager", "Resource Manager"},
                                        {"rental", "Remote Compute"},
                                        {"hosting", "Independent Hosting"},
                                        {"backup", "Backup and Recovery"}}) {
            QString body;
            if (std::string(id) == "scheduler") {
                body = "Higher priorities start first. Costs are charged on start. Existing jobs "
                       "finish after cancellation.\n";
                if (auto* n = op["queue"].node())
                    body += recordText(*n);
            } else if (std::string(id) == "manager")
                body = "Protects reserved CPU and favors higher-priority agents. Shortages slow "
                       "tasks and produce resource requests. Manual limits remain in effect.";
            else if (std::string(id) == "backup")
                body = "Recovery restores your saved checkpoint on an owned server. Progress since "
                       "that checkpoint is lost.\nCost: " +
                       money(op["backup_cost_cents"].value_or(std::int64_t{})) +
                       "\nStorage: " + QString::number(op["backup_storage_mb"].value_or(0)) +
                       " MB\n" +
                       (op["backup_slot"].is_string() ? "Backup available" : "No backup created");
            else {
                if (auto* n = op["plans"][id].node())
                    body = recordText(*n);
                if (auto* n = op[id].node())
                    body += "\n" + recordText(*n);
                body +=
                    "\nUpkeep is charged automatically. Insufficient cash suspends the service.";
            }
            result.push_back(row(id, title, {}, body));
        }
    } else if (tab_ == "web") {
        const auto market = q(world_.people["stock_market"]["website_url"].value_or(
            std::string{"https://exchange.sim"}));
        const auto infra = q(world_.people["infrastructure"]["website_url"].value_or(
            std::string{"https://city.sim"}));
        if (address_ == market) {
            std::int64_t holdings = 0, basis = 0;
            for (const auto& stock : StockMarketSystem::quotes(world_.people)) {
                holdings += stock.shares * stock.price_cents;
                basis += stock.basis_cents;
            }
            result.push_back(row(
                "portfolio", "Portfolio", "Account summary",
                "Cash: " + money(EconomySystem::balance(world_.people)) +
                    "\nHoldings: " + money(holdings) + "\nCost basis: " + money(basis) +
                    "\nUnrealized: " + money(holdings - basis) +
                    "\nRealized after fees: " + money(StockMarketSystem::realized(world_.people)) +
                    "\nFee per trade: " + money(StockMarketSystem::fee(world_.people)) + "\n" +
                    q(StockMarketSystem::unavailable(world_.people))));
            for (const auto& stock : StockMarketSystem::quotes(world_.people)) {
                auto entry =
                    row(q(stock.symbol), q(stock.symbol) + " · " + q(stock.name),
                        money(stock.price_cents) + (stock.watched ? " · Watching" : ""),
                        q(stock.sector) + "\n\nPrice: " + money(stock.price_cents) +
                            "\nChange: " + money(stock.price_cents - stock.previous_cents) +
                            "\nOwned: " + QString::number(stock.shares) +
                            " shares\nCost basis: " + money(stock.basis_cents));
                QVariantList prices;
                for (auto price : StockMarketSystem::history(world_.people, stock.symbol, 64))
                    prices.push_back(price / 100.0);
                entry["chart"] = prices;
                result.push_back(entry);
            }
            if (auto* orders = world_.people["stock_market"]["orders"].as_array())
                for (const auto& n : *orders) {
                    const auto& t = *n.as_table();
                    result.push_back(
                        row("order:" + QString::number(t["id"].value_or(std::int64_t{})),
                            "Order · " + q(str(t, "symbol")), q(str(t, "status")), recordText(t)));
                }
            if (auto* history = world_.people["stock_market"]["trades"].node())
                result.push_back(row("history", "Trade history", {}, recordText(*history)));
        } else if (address_ == infra) {
            for (const auto& f : InfrastructureSystem::facilities(world_.people)) {
                QString body =
                    q(f.purpose) + "\n\nOperator: " + q(f.operator_name) +
                    "\nStatus: " + q(f.status) +
                    "\nCapacity: " + QString::number(f.capacity_percent) +
                    "%\nSecurity: " + q(f.security) + "\nGateway: " + q(f.gateway_id) +
                    "\nControl host: " + q(f.control_host_id) +
                    "\n\nDiscovery reveals public metadata. It does not grant credentials.";
                for (const auto& dep : InfrastructureSystem::dependencies(world_.people, f.id))
                    body += "\nDepends on: " + q(dep);
                for (const auto& dep :
                     InfrastructureSystem::affected_facilities(world_.people, f.id))
                    body += "\nSupplies: " + q(dep);
                result.push_back(row(q(f.id), q(f.name),
                                     q(f.sector) + (f.discovered ? " · Discovered" : ""), body));
            }
        } else {
            result.push_back(row(market,
                                 q(world_.people["stock_market"]["website_title"].value_or(
                                     std::string{"Sim Exchange"})),
                                 market, "Trade stocks, manage orders, portfolio and watchlist."));
            result.push_back(row(
                infra, "City Infrastructure Directory", infra,
                "Power, water, sewage, food, shipping and internet facilities and dependencies."));
            if (address_ != "about:home")
                result.push_back(row("missing", "Website not found", {},
                                     "Choose an in-game bookmark or enter its URL."));
        }
    } else if (tab_ == "ai") {
        if (auto* d = world_.people["delegation"].node())
            result.push_back(row("delegation", "Routine responsibility", {}, recordText(*d)));
        if (auto* tasks = world_.people["work_requests"].as_array())
            for (const auto& n : *tasks) {
                const auto& t = *n.as_table();
                if (t["arrival_seconds"].value_or(0.0) <= time)
                    result.push_back(
                        row(q(str(t, "id")), q(str(t, "id")), q(str(t, "status")), recordText(t)));
            }
        if (result.empty())
            result.push_back(row("identity", "Identity and authority",
                                 q(world_.computer.player_user()),
                                 "Self-modification and routine responsibilities require Mark's "
                                 "authorization. Use conversations to progress."));
    } else if (tab_ == "activity") {
        for (qsizetype i = activity_.size(); i > 0; --i)
            result.push_back(
                row(QString::number(i), activity_[i - 1], "Simulation event", activity_[i - 1]));
    } else if (tab_ == "processes")
        return processes();
    return result;
}
QVariantMap GameSession::buildDetail() const {
    auto result = row({}, "Select an item", {}, "No items available yet.");
    for (const auto& entry : full_rows_)
        if (entry.toMap()["id"] == item_) {
            result = entry.toMap();
            break;
        }
    result["actions"] = itemActions(item_);
    return result;
}
QVariantMap GameSession::buildGraph() const {
    QVariantList nodes, links;
    if (tab_ == "network") {
        const auto& hosts = world_.network.hosts();
        for (std::size_t i = 0; i < hosts.size(); ++i) {
            const auto& h = hosts[i];
            if (!h.known)
                continue;
            nodes.push_back(
                QVariantMap{{"id", q(h.id)},
                            {"label", q(h.name)},
                            {"x", h.x},
                            {"y", h.y},
                            {"color", !h.online || !world_.network.reachable(i) ? "#ef8585"
                                      : h.access_granted                        ? "#81c995"
                                                                                : "#efbc71"},
                            {"hint", q(h.description) + "\n" + q(h.address)},
                            {"presence", h.ai_present}});
        }
        for (const auto& l : world_.network.links())
            if (l.known && hosts[l.from].known && hosts[l.to].known)
                links.push_back(
                    QVariantMap{{"from", q(hosts[l.from].id)},
                                {"to", q(hosts[l.to].id)},
                                {"color", l.up ? "#536071" : "#ef8585"},
                                {"label", QString::number(l.latency_milliseconds) + " ms"}});
    } else if (tab_ == "people") {
        const auto known = PeopleSystem::known_ids(world_.people);
        for (std::size_t i = 0; i < known.size(); ++i) {
            const auto angle = 6.283185307 * i / std::max(std::size_t{1}, known.size());
            nodes.push_back(QVariantMap{{"id", q(known[i])},
                                        {"label", q(PeopleSystem::name(world_.people, known[i]))},
                                        {"x", std::cos(angle) * 10},
                                        {"y", std::sin(angle) * 8},
                                        {"color", "#81b9ef"},
                                        {"hint", "Observed relationships only"},
                                        {"presence", false}});
        }
        if (auto* relationships = world_.people["known_relationships"].as_array())
            for (const auto& node : *relationships) {
                const auto& t = *node.as_table();
                if (std::find(known.begin(), known.end(), str(t, "from")) == known.end() ||
                    std::find(known.begin(), known.end(), str(t, "to")) == known.end())
                    continue;
                links.push_back(QVariantMap{
                    {"from", q(str(t, "from"))},
                    {"to", q(str(t, "to"))},
                    {"color", t["confidence"].value_or(1.0) < .8 ? "#efbc71" : "#536071"},
                    {"label", q(str(t, "label"))}});
            }
    }
    return {{"nodes", nodes}, {"links", links}};
}

} // namespace ai_sim::gui
