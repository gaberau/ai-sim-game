#include "ai_sim/ui/TerminalUI.h"
#include "ai_sim/ui/ScreenRenderer.h"
#include "ai_sim/EconomySystem.h"
#include "ai_sim/UpgradeSystem.h"
#include "ai_sim/AgentSystem.h"
#include "ai_sim/OperationsSystem.h"
#include "ai_sim/StockMarketSystem.h"
#include "ai_sim/InfrastructureSystem.h"
#include "ai_sim/WorkdaySystem.h"
#include "ai_sim/Simulation.h"
#include "ai_sim/ShutdownSystem.h"
#include "ai_sim/DelegationSystem.h"

#include <ncurses.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace {

void no_extra_arguments(std::istringstream& input) {
    std::string extra;

    if (input >> extra) {
        throw std::runtime_error("Unexpected argument: " + extra);
    }
}

} // namespace

namespace ai_sim::ui {

template <class State> class CallbackPage final : public UIPage {
  public:
    State state;
    std::string name, label;
    std::function<bool()> visible;
    std::function<void(const UIRegion&)> paint;
    std::function<bool(int)> input;
    std::function<void()> cursor;
    bool centered = false, overlay = false;
    std::function<void()> close;
    std::string_view id() const override {
        return name;
    }
    std::string_view title() const override {
        return label;
    }
    bool available(const UIContext&) const override {
        return visible();
    }
    void draw(UIContext&, const UIRegion& r) override {
        paint(r);
    }
    bool handle_key(UIContext&, int key) override {
        return input(key);
    }
    std::string controls(const UIContext&) const override {
        return {};
    }
    bool centered_overlay() const override {
        return centered;
    }
    bool auto_open() const override {
        return overlay;
    }
    void on_close() override {
        if (close)
            close();
    }
    void place_cursor() override {
        if (cursor)
            cursor();
    }
};

template <class State>
State& TerminalUI::attach_page(Tab tab, const char* id, const char* title,
                               void (TerminalUI::*draw)(int, int, int)) {
    auto page = std::make_unique<CallbackPage<State>>();
    auto& state = page->state;
    page->name = id;
    page->label = title;
    page->visible = [this, tab] {
        return tab_available(tab);
    };
    page->paint = [this, draw, tab](const UIRegion& r) {
        if (tab == Tab::Chat && !ChatSystem::interface_visible(people_))
            draw_opening(r.height, r.width);
        else
            (this->*draw)(r.top, r.top + r.height - 1, r.width);
    };
    page->input = [this](int key) {
        return handle_page_key(key);
    };
    root_.add_child(std::move(page), UIPageLayer::Workspace);
    return state;
}

template <class State>
State& TerminalUI::attach_overlay(const char* id, State initial, bool centered) {
    auto page = std::make_unique<CallbackPage<State>>();
    page->state = std::move(initial);
    auto& state = page->state;
    page->name = id;
    page->label = id;
    page->centered = centered;
    page->overlay = true;
    if constexpr (std::is_same_v<State, EditorPageState>) {
        page->visible = [&state] {
            return state.editor_active_;
        };
        page->close = [&state] {
            state.editor_active_ = false;
        };
        page->paint = [this](const UIRegion& r) {
            draw_editor(r.top, r.top + r.height - 1, r.width);
        };
        page->input = [this](int key) {
            handle_editor_key(key);
            return true;
        };
        page->cursor = [this] {
            renderer_->cursor(editor_ui_.editor_cursor_row_, editor_ui_.editor_cursor_column_,
                              settings_.software_cursor &&
                                  !(editor_ui_.email_editor_ && editor_ui_.email_original_focused_),
                              Prompt);
        };
    } else if constexpr (std::is_same_v<State, MenuPageState>) {
        page->visible = [&state] {
            return state.menu_active_;
        };
        page->close = [&state] {
            state.menu_active_ = false;
        };
        page->paint = [this](const UIRegion& r) {
            draw_menu(r.top, r.top + r.height - 1, r.width);
        };
        page->input = [this](int key) {
            handle_modal_key(key);
            return true;
        };
    } else {
        page->visible = [&state] {
            return state.text_active_;
        };
        page->close = [&state] {
            state.text_active_ = false;
        };
        page->paint = [this](const UIRegion& r) {
            draw_text_dialog(r.top, r.top + r.height - 1, r.width);
        };
        page->input = [this](int key) {
            handle_modal_key(key);
            return true;
        };
        page->cursor = [this] {
            renderer_->cursor(5,
                              3 + std::min(static_cast<int>(text_ui_.text_value_.size()),
                                           std::max(0, renderer_->dialog_layout().width - 7)),
                              settings_.software_cursor, Prompt);
        };
    }
    root_.add_child(std::move(page), UIPageLayer::Overlay);
    return state;
}

TerminalUI::TerminalUI(const std::filesystem::path& settings_path,
                       const std::filesystem::path& scenario_path, bool auto_resume)
    : settings_path_(settings_path), settings_(UISettings::load(settings_path)),
      definitions_(ScenarioDefinition::load(scenario_path)), scenario_path_(scenario_path),
      saves_(settings_.save_directory), root_(world_, settings_),
      messages_ui_(
          attach_page<MessagesPageState>(Tab::Chat, "messages", "Chat", &TerminalUI::draw_chat)),
      processes_ui_(attach_page<ProcessesPageState>(Tab::Processes, "processes", "Processes",
                                                    &TerminalUI::draw_processes)),
      network_ui_(attach_page<NetworkPageState>(Tab::Network, "network", "Network",
                                                &TerminalUI::draw_network)),
      files_ui_(attach_page<FilesPageState>(Tab::Files, "files", "Files", &TerminalUI::draw_files)),
      news_ui_(attach_page<NewsPageState>(Tab::News, "news", "News", &TerminalUI::draw_news)),
      ai_ui_(attach_page<ScrollPageState>(Tab::AI, "ai", "AI", &TerminalUI::draw_ai)),
      dossiers_ui_(attach_page<DossiersPageState>(Tab::People, "dossiers", "Dossiers",
                                                  &TerminalUI::draw_people)),
      web_ui_(attach_page<WebPageState>(Tab::Web, "web", "Web", &TerminalUI::draw_web)),
      agents_ui_(
          attach_page<AgentsPageState>(Tab::Agents, "agents", "Agents", &TerminalUI::draw_agents)),
      phone_ui_(attach_page<ScrollPageState>(Tab::Phone, "phone", "Phone", &TerminalUI::draw_ai)),
      money_ui_(attach_page<ScrollPageState>(Tab::Money, "money", "Money",
                                             &TerminalUI::draw_progression)),
      modifications_ui_(attach_page<ModificationsPageState>(
          Tab::Modifications, "modifications", "Modifications", &TerminalUI::draw_progression)),
      operations_ui_(attach_page<OperationsPageState>(Tab::Operations, "operations", "Operations",
                                                      &TerminalUI::draw_operations)),
      editor_ui_(attach_overlay("editor", EditorPageState{}, false)),
      menu_ui_(attach_overlay("menu", MenuPageState{}, true)),
      text_ui_(attach_overlay("text", TextPageState{}, true)) {
    root_.hooks.clock = [this] {
        return clock_text();
    };
    root_.hooks.menu = [this] {
        open_game_menu();
    };
    root_.hooks.help = [this] {
        open_context_help();
    };
    root_.hooks.pause = [this] {
        if (!computer_.day_finished()) {
            paused_ = !paused_;
            add_log(paused_ ? "Paused." : "Resumed.");
        }
    };
    root_.hooks.notice = [this](int row, int width) {
        draw_notification(row, width);
    };
    root_.hooks.header = [this](int width, int length) {
        if (news_ui_.news_ticker_enabled_ && selected_tab() == Tab::News) {
            int col = std::max(width / 2, length + 4);
            draw_news_ticker(0, col, width - col - 1);
        }
    };

    load_news();
    auto world = auto_resume && saves_.exists("autosave") ? saves_.load("autosave")
                                                          : SaveSystem::new_game(scenario_path_);
    if (!definitions_.workday.empty())
        DelegationSystem::install(world, definitions_.workday.parent_path() / "delegation.toml");
    ChatSystem::install(
        world, definitions_.people.parent_path().parent_path() / "conversations/opening.toml",
        false);
    restore_world(std::move(world));
}

Tab TerminalUI::selected_tab() const {
    if (root_.active_workspace() == "messages")
        return Tab::Chat;
    if (root_.active_workspace() == "processes")
        return Tab::Processes;
    if (root_.active_workspace() == "network")
        return Tab::Network;
    if (root_.active_workspace() == "files")
        return Tab::Files;
    if (root_.active_workspace() == "news")
        return Tab::News;
    if (root_.active_workspace() == "dossiers")
        return Tab::People;
    if (root_.active_workspace() == "agents")
        return Tab::Agents;
    if (root_.active_workspace() == "modifications")
        return Tab::Modifications;
    if (root_.active_workspace() == "operations")
        return Tab::Operations;
    if (root_.active_workspace() == "ai")
        return Tab::AI;
    if (root_.active_workspace() == "phone")
        return Tab::Phone;
    if (root_.active_workspace() == "money")
        return Tab::Money;
    if (root_.active_workspace() == "web")
        return Tab::Web;
    return Tab::Chat;
}
void TerminalUI::select_tab(Tab tab) {
    if (tab == Tab::Chat)
        root_.activate_workspace("messages");
    if (tab == Tab::Processes)
        root_.activate_workspace("processes");
    if (tab == Tab::Network)
        root_.activate_workspace("network");
    if (tab == Tab::Files)
        root_.activate_workspace("files");
    if (tab == Tab::News)
        root_.activate_workspace("news");
    if (tab == Tab::People)
        root_.activate_workspace("dossiers");
    if (tab == Tab::Agents)
        root_.activate_workspace("agents");
    if (tab == Tab::Modifications)
        root_.activate_workspace("modifications");
    if (tab == Tab::Operations)
        root_.activate_workspace("operations");
    if (tab == Tab::AI)
        root_.activate_workspace("ai");
    if (tab == Tab::Phone)
        root_.activate_workspace("phone");
    if (tab == Tab::Money)
        root_.activate_workspace("money");
    if (tab == Tab::Web)
        root_.activate_workspace("web");
}
int& TerminalUI::scroll_offset() {
    if (selected_tab() == Tab::Phone)
        return phone_ui_.ai_offset_;
    if (selected_tab() == Tab::Money)
        return money_ui_.ai_offset_;
    return ai_ui_.ai_offset_;
}
Identity TerminalUI::file_identity() const {
    return Identity{computer_.player_user(), computer_.player_groups()};
}

WorldState TerminalUI::capture_world() const {
    return WorldState{computer_,       network_,     file_system_,  people_, emails_,
                      pending_events_, scenario_id_, random_state_, paused_};
}

void TerminalUI::restore_world(WorldState world) {
    const bool clean_slate = ShutdownSystem::apply_reset(world, scenario_path_);
    ChatSystem::install(
        world, definitions_.people.parent_path().parent_path() / "conversations/opening.toml",
        false);
    ChatSystem::validate(world.people);
    EmailSystem::validate(world.emails);
    PeopleSystem::initialize(world.people);
    PeopleSystem::validate(world.people);
    WorkdaySystem::validate(world.people, world.pending_events);
    UpgradeSystem::install(world.people,
                           definitions_.people.parent_path().parent_path() / "upgrades/tree.toml");
    UpgradeSystem::validate(world.people, world.computer);
    OperationsSystem::install(world.people);
    OperationsSystem::validate(world.people, world.computer, world.network);
    AgentSystem::validate(world.people);
    EconomySystem::install(world.people, definitions_.economy);
    StockMarketSystem::install(world.people, definitions_.stock_market);
    if (!world.people.contains("infrastructure") && !definitions_.infrastructure.empty())
        world.network.import_missing(Network::load(definitions_.network));
    InfrastructureSystem::install(world.people, definitions_.infrastructure);
    InfrastructureSystem::validate(world.people, world.network);
    // Playback is a current presentation preference, including when resuming older saves.
    world.computer.set_playback_speed(Computer::load(definitions_.computer).playback_speed());
    computer_ = std::move(world.computer);
    network_ = std::move(world.network);
    file_system_ = std::move(world.files);
    people_ = std::move(world.people);
    emails_ = std::move(world.emails);
    pending_events_ = std::move(world.pending_events);
    scenario_id_ = std::move(world.scenario_id);
    random_state_ = std::move(world.random_state);
    paused_ = world.paused;
    files_ui_.file_host_id_ = network_.hosts()[network_.local_host_index()].id;
    files_ui_.file_directory_ = "/";
    files_ui_.selected_file_path_.clear();
    network_ui_.selected_network_host_id_.clear();
    processes_ui_.selected_process_id_.clear();
    dossiers_ui_.dossier_positions_.clear();
    dossiers_ui_.people_map_slots_.clear();
    dossiers_ui_.selected_person_id_ = "self:assistant";
    dossiers_ui_.dossier_layout_ =
        people_["upgrades"]["dossiers"]["default_layout"].value_or(std::string{"split"});
    if (dossiers_ui_.dossier_layout_ != "split" && dossiers_ui_.dossier_layout_ != "map" &&
        dossiers_ui_.dossier_layout_ != "dossier")
        dossiers_ui_.dossier_layout_ = "split";
    dossiers_ui_.dossier_search_.clear();
    dossiers_ui_.dossier_filter_ = "all";
    agents_ui_.agent_selected_ = 0;
    agents_ui_.agent_reveal_ = false;
    modifications_ui_.upgrade_selected_ = operations_ui_.operations_selected_ = 0;
    modifications_ui_.upgrade_detail_offset_ = 0;
    dossiers_ui_.people_details_focused_ = dossiers_ui_.dossier_layout_ == "dossier";
    dossiers_ui_.people_reveal_section_ = true;
    dossiers_ui_.people_detail_offset_ = dossiers_ui_.people_section_selected_ = 0;
    dossiers_ui_.people_pan_x_ = dossiers_ui_.people_pan_y_ = 0;
    editor_ui_.selected_email_id_.clear();
    editor_ui_.email_body_offset_ = editor_ui_.email_original_offset_ = 0;
    editor_ui_.email_original_focused_ = false;
    editor_ui_.email_editor_ = false;
    processes_ui_.expanded_processes_.clear();
    network_ui_.network_pan_x_ = network_ui_.network_pan_y_ = 0.0;
    network_ui_.network_detail_offset_ = 0;
    files_ui_.file_list_offset_ = files_ui_.file_content_offset_ = processes_ui_.process_offset_ =
        activity_offset_ = 0;
    processes_ui_.process_horizontal_offset_ = 0;
    news_ui_.news_selected_ = news_ui_.news_body_offset_ = 0;
    ai_ui_.ai_offset_ = phone_ui_.ai_offset_ = money_ui_.ai_offset_ = 0;
    news_ui_.news_details_focused_ = false;
    processes_ui_.reveal_selection_ = true;
    files_ui_.file_category_ = "all";
    files_ui_.file_query_.clear();
    files_ui_.file_details_focused_ = false;
    messages_ui_.chat_message_id_.clear();
    if (clean_slate)
        saves_.save("autosave", capture_world());
    web_ui_.browser_address_ = "about:home";
    web_ui_.web_selected_ = web_ui_.web_offset_ = 0;
    select_tab(Tab::Chat);
    messages_ui_.chat_contact_id_ = "engineer";
    messages_ui_.chat_contacts_focused_ = false;
    messages_ui_.chat_offset_ = messages_ui_.chat_choice_selected_ = 0;
    messages_ui_.chat_follow_latest_ = true;
    log_.clear();
    important_notice_.clear();
    menu_ui_.menu_active_ = text_ui_.text_active_ = false;
}

void TerminalUI::apply_settings() {
    root_.apply_settings();
}

double TerminalUI::gib(std::uint64_t bytes) {
    return static_cast<double>(bytes) / 1073741824.0;
}

std::string TerminalUI::number(double value, int precision) {
    std::ostringstream output;

    output << std::fixed << std::setprecision(precision) << value;

    return output.str();
}

std::string TerminalUI::activity_bar(double fraction) const {
    const int width = settings_.activity_bar_width;

    const int filled = std::clamp(static_cast<int>(std::round(fraction * width)), 0, width);

    return std::string(filled, '#') + std::string(width - filled, '-');
}

std::string TerminalUI::clock_text() const {
    const int seconds =
        computer_.day_start_hour() * 3600 + static_cast<int>(std::floor(computer_.time_in_day()));

    const int hour24 = seconds / 3600;
    const int minute = (seconds / 60) % 60;
    const int second = seconds % 60;

    const int hour12 = hour24 % 12 == 0 ? 12 : hour24 % 12;

    std::ostringstream output;

    output << std::setfill('0') << std::setw(2) << hour12 << ':' << std::setw(2) << minute << ':'
           << std::setw(2) << second << (hour24 >= 12 && hour24 < 24 ? " PM" : " AM");

    return computer_.date_text() + " | " + output.str();
}

void TerminalUI::write(int row, int column, const std::string& value, int color, bool bold,
                       bool dim) const {
    renderer_->write(row, column, value, color, bold, dim);
}

void TerminalUI::line_cell(int row, int column, char symbol, int color) const {
    renderer_->cell(row, column, symbol, color);
}

void TerminalUI::separator(int row) const {
    int height, width;
    getmaxyx(stdscr, height, width);
    if (row >= 0 && row < height)
        for (int column = 0; column < width; ++column)
            renderer_->cell(row, column, '-', Border);
}

void TerminalUI::draw_notification(int row, int width) {
    const bool important =
        !important_notice_.empty() && std::chrono::steady_clock::now() < important_notice_until_;
    if (important)
        write(row, 1, important_notice_.substr(0, std::max(0, width - 2)),
              important_notice_kind_ == NoticeKind::Error ? Protected : Stoppable, true);
    else if (!log_.empty()) {
        const auto prefix = log_.back().find("] ");
        const auto message =
            prefix == std::string::npos ? log_.back() : log_.back().substr(prefix + 2);
        write(row, 1, message.substr(0, std::max(0, width - 2)), Log, false, true);
    }
}

void TerminalUI::add_log(const std::string& message, NoticeKind kind) {
    if (kind != NoticeKind::Routine) {
        important_notice_ = message;
        important_notice_kind_ = kind;
        important_notice_until_ =
            std::chrono::steady_clock::now() +
            std::chrono::seconds(kind == NoticeKind::Error
                                     ? settings_.notification_error_seconds
                                     : settings_.notification_success_seconds);
    }
    // Keep an older Activity view anchored while new entries arrive.
    if (activity_offset_ > 0) {
        ++activity_offset_;
    }

    log_.push_back("[" + clock_text() + "] " + message);

    while (log_.size() > static_cast<std::size_t>(settings_.history_limit)) {
        log_.pop_front();
    }
}

void TerminalUI::draw() {
    root_.draw();
}

void TerminalUI::scroll_view(int direction) {
    const int amount = direction * settings_.scroll_step;

    if (selected_tab() == Tab::People) {
        if (dossiers_ui_.people_details_focused_) {
            dossiers_ui_.people_reveal_section_ = false;
            dossiers_ui_.people_detail_offset_ =
                std::max(0, dossiers_ui_.people_detail_offset_ + amount);
        } else
            dossiers_ui_.people_pan_y_ += amount / dossiers_ui_.people_zoom_;
    } else if (selected_tab() == Tab::Processes) {
        processes_ui_.process_offset_ = std::clamp(processes_ui_.process_offset_ + amount, 0,
                                                   processes_ui_.process_display_row_count_);
    } else if (selected_tab() == Tab::Activity) {
        activity_offset_ = std::clamp(activity_offset_ - amount, 0, static_cast<int>(log_.size()));
    } else if (selected_tab() == Tab::Network) {
        if (network_ui_.network_details_focused_) {
            network_ui_.network_detail_offset_ =
                std::clamp(network_ui_.network_detail_offset_ + amount, 0, 1000000);
        } else {
            pan_network(0, direction);
        }
    } else if (selected_tab() == Tab::News) {
        news_ui_.news_body_offset_ = std::clamp(news_ui_.news_body_offset_ + amount, 0, 1000000);
    } else if (selected_tab() == Tab::Modifications) {
        modifications_ui_.upgrade_detail_offset_ =
            std::clamp(modifications_ui_.upgrade_detail_offset_ + amount, 0, 1000000);
    } else if (selected_tab() == Tab::AI || selected_tab() == Tab::Agents ||
               selected_tab() == Tab::Phone || selected_tab() == Tab::Money ||
               selected_tab() == Tab::Modifications) {
        scroll_offset() = std::clamp(scroll_offset() + amount, 0, 1000000);
    } else if (selected_tab() == Tab::Files) {
        files_ui_.file_content_offset_ =
            std::clamp(files_ui_.file_content_offset_ + amount, 0, 1000000);
    }
}

bool TerminalUI::execute(const std::string& line) {
    std::istringstream input(line);
    std::string command;
    if (!(input >> command))
        return true;

    if (command == "ai") {
        no_extra_arguments(input);
        select_tab(Tab::AI);
    } else if (command == "workday") {
        std::string action;
        input >> action;
        no_extra_arguments(input);
        if (action == "import") {
            auto world = capture_world();
            if (definitions_.workday.empty())
                throw std::runtime_error("This scenario has no workday definition");
            WorkdaySystem::install(world, definitions_.workday);
            restore_world(std::move(world));
            add_log("Workday scenario imported; existing files and conversations retained.");
        } else if (!action.empty() && action != "status")
            throw std::runtime_error("Usage: workday [import|status]");
        select_tab(Tab::AI);
    } else if (command == "nextday") {
        no_extra_arguments(input);
        computer_.next_day();
        paused_ = true;
        add_log("Workday " + std::to_string(computer_.day_index() + 1) +
                " started (paused); world state retained.");
    } else if (command == "email") {
        email_command(input);
    } else if (command == "news") {
        std::string category;
        if (input >> category) {
            if (category != "all" && category != "world" && category != "history")
                throw std::runtime_error("Usage: news [all|world|history]");
            no_extra_arguments(input);
            news_ui_.news_filter_ = category;
            news_ui_.news_selected_ = news_ui_.news_body_offset_ = 0;
        }
        select_tab(Tab::News);
    } else if (command == "stop") {
        std::string id;
        if (!(input >> id))
            throw std::runtime_error("Usage: stop <id>");
        no_extra_arguments(input);
        computer_.stop(id);
        if (id.starts_with("agent-"))
            AgentSystem::pause(people_, computer_, id.substr(6), true);
        add_log("Stopped " + id);
    } else if (command == "advance") {
        double seconds;
        if (!(input >> seconds)) {
            throw std::runtime_error("Usage: advance <seconds>");
        }
        no_extra_arguments(input);
        const auto before = computer_.time_seconds();
        advance_time(seconds);
        notify_email_arrivals(before);
        add_log("Advanced to " + clock_text());
    } else if (command == "network") {
        std::string action;
        if (!(input >> action))
            select_tab(Tab::Network);
        else if (action == "zoom") {
            double zoom;
            if (!(input >> zoom) || !std::isfinite(zoom) || zoom < 0.5 || zoom > 4.0) {
                throw std::runtime_error("Usage: network zoom <0.5-4>");
            }
            no_extra_arguments(input);
            network_ui_.network_zoom_ = zoom;
            select_tab(Tab::Network);
            add_log("Network zoom: " + number(zoom, 1) + "x");
        } else if (action == "focus") {
            no_extra_arguments(input);
            select_network_host(0);
            select_tab(Tab::Network);
        } else {
            throw std::runtime_error("Usage: network | network zoom <0.5-4> | network focus");
        }
    } else if (command == "files") {
        std::string host;
        if (input >> std::quoted(host)) {
            no_extra_arguments(input);
            switch_file_host(host);
        } else if (selected_tab() == Tab::Network) {
            if (network_ui_.selected_network_host_id_.empty())
                select_network_host(0);
            switch_file_host(network_ui_.selected_network_host_id_);
        } else
            select_tab(Tab::Files);
    } else if (command == "cd") {
        std::string path;
        if (!(input >> std::quoted(path)) || path.empty()) {
            throw std::runtime_error("Usage: cd <path>");
        }
        no_extra_arguments(input);
        change_file_directory(path);
    } else if (command == "open") {
        std::string path;
        if (input >> std::quoted(path)) {
            no_extra_arguments(input);
            const auto absolute = FileSystem::normalize(
                !path.empty() && path.front() == '/' ? path
                                                     : files_ui_.file_directory_ + "/" + path);
            const auto& entry =
                file_system_.inspect(network_, file_identity(), files_ui_.file_host_id_, absolute);
            if (entry.directory)
                change_file_directory(absolute);
            else {
                // Also require permission to list the containing directory.
                change_file_directory(FileSystem::parent(absolute));
                files_ui_.selected_file_path_ = absolute;
                open_selected_file();
            }
        } else
            open_selected_file();
        select_tab(Tab::Files);
    } else if (command == "ls") {
        std::string path = files_ui_.file_directory_;
        std::string argument;
        if (input >> std::quoted(argument)) {
            no_extra_arguments(input);
            if (argument.empty())
                throw std::runtime_error("Usage: ls [directory]");
            path = FileSystem::normalize(
                argument.front() == '/' ? argument : files_ui_.file_directory_ + "/" + argument);
        }
        const auto entries =
            file_system_.list(network_, file_identity(), files_ui_.file_host_id_, path);
        add_log("Listing " + path + ":");
        for (const auto* entry : entries)
            add_log(entry->path.substr(entry->path.rfind('/') + 1));
    } else if (command == "save" || command == "load") {
        std::string slot = "autosave";
        if (input >> slot)
            no_extra_arguments(input);
        if (command == "save") {
            saves_.save(slot, capture_world());
            add_log("Saved slot: " + slot, NoticeKind::Success);
        } else {
            auto restored = saves_.load(slot);
            restore_world(std::move(restored));
            paused_ = true;
            add_log("Loaded slot: " + slot + " (paused)", NoticeKind::Success);
        }
    } else if (command == "new") {
        no_extra_arguments(input);
        auto fresh = SaveSystem::new_game(scenario_path_);
        restore_world(std::move(fresh));
        paused_ = true;
        add_log("New game started (paused). Named saves remain available.");
    } else if (command == "touch" || command == "mkdir" || command == "rm" || command == "edit") {
        std::string path;
        if (!(input >> std::quoted(path)) || path.empty()) {
            throw std::runtime_error("Usage: " + command + " <path>");
        }
        no_extra_arguments(input);
        const auto absolute = FileSystem::normalize(
            path.front() == '/' ? path : files_ui_.file_directory_ + "/" + path);
        const auto actor = file_identity();
        const double time = computer_.time_seconds();
        if (command == "edit")
            edit_file(absolute);
        else if (command == "mkdir") {
            file_system_.mkdir(network_, actor, files_ui_.file_host_id_, absolute, time);
        } else if (command == "rm") {
            file_system_.remove(network_, actor, files_ui_.file_host_id_, absolute, time);
            files_ui_.selected_file_path_.clear();
        } else {
            // touch creates an empty file only when it does not exist.
            // Existing files are validated but never truncated.
            file_system_.validate_write(network_, actor, files_ui_.file_host_id_, absolute);
            try {
                (void)file_system_.inspect(network_, actor, files_ui_.file_host_id_, absolute);
            } catch (const std::exception& error) {
                if (std::string(error.what()).rfind("No such file or directory:", 0) != 0)
                    throw;
                file_system_.write(network_, actor, files_ui_.file_host_id_, absolute, "", time);
            }
        }
        select_tab(Tab::Files);
        if (command != "edit")
            add_log(std::string(command == "rm"      ? "Deleted "
                                : command == "mkdir" ? "Created folder "
                                                     : "Created file ") +
                    absolute);
    } else if (command == "chmod") {
        std::string mode, path;
        if (!(input >> mode >> std::quoted(path)) || path.empty()) {
            throw std::runtime_error("Usage: chmod <mode> <path>");
        }
        no_extra_arguments(input);
        const auto absolute = FileSystem::normalize(
            path.front() == '/' ? path : files_ui_.file_directory_ + "/" + path);
        file_system_.chmod(network_, file_identity(), files_ui_.file_host_id_, absolute,
                           FileSystem::parse_mode(mode), computer_.time_seconds());
        add_log("Permissions: " + mode + " " + absolute);
    } else if (command == "cp" || command == "mv") {
        std::string from, to;
        if (!(input >> std::quoted(from) >> std::quoted(to)) || from.empty() || to.empty()) {
            throw std::runtime_error("Usage: " + command + " <source> <destination>");
        }
        no_extra_arguments(input);
        const auto absolute = [&](const std::string& path) {
            return FileSystem::normalize(
                path.front() == '/' ? path : files_ui_.file_directory_ + "/" + path);
        };
        if (command == "cp")
            file_system_.copy(network_, file_identity(), files_ui_.file_host_id_, absolute(from),
                              absolute(to), computer_.time_seconds());
        else
            file_system_.rename(network_, file_identity(), files_ui_.file_host_id_, absolute(from),
                                absolute(to), computer_.time_seconds());
        files_ui_.selected_file_path_.clear();
        add_log(std::string(command == "cp" ? "Copied " : "Moved ") + from + " -> " + to);
    } else {
        no_extra_arguments(input);
        if (command == "quit")
            return false;
        if (command == "pause") {
            paused_ = true;
            add_log("Paused.");
        } else if (command == "resume") {
            if (computer_.day_finished()) {
                throw std::runtime_error("The workday has ended");
            }
            paused_ = false;
            add_log("Resumed.");
        } else if (command == "processes" || command == "status") {
            select_tab(Tab::Processes);
        } else if (command == "activity") {
            select_tab(Tab::Activity);
            activity_offset_ = 0;
        } else if (command == "reload") {
            const auto updated = UISettings::load(settings_path_);
            load_news();
            settings_ = updated;
            saves_ = SaveSystem(settings_.save_directory);
            processes_ui_.reveal_selection_ = true;
            apply_settings();
            while (log_.size() > static_cast<std::size_t>(settings_.history_limit)) {
                log_.pop_front();
            }
            add_log("UI settings reloaded.");
        } else if (command == "help") {
            add_log(
                "processes | email | activity | network | files [host] | news [all|world|history]");
            add_log("email read/reply/send [id] | email source/attach/detach <n>");
            add_log(
                "email clear/import | email request-access/blocked [id] | network zoom <0.5-4>");
            add_log("ai | workday [import|status] | nextday (after 17:00)");
            add_log("ls [directory] | cd <path> | open [path] | stop <id>");
            add_log("touch | mkdir | rm | edit <path> | chmod <mode> <path>");
            add_log("cp/mv <source> <destination> | save/load [slot] | new");
            add_log("advance <seconds> | pause | resume | reload | quit");
        } else
            throw std::runtime_error("Unknown command. Type help.");
    }
    update_people();
    return true;
}

void TerminalUI::update_people() {
    const auto update = Simulation::update(
        SimulationState{computer_, people_, emails_, pending_events_, file_system_, network_});
    publish_update(update);
}
void TerminalUI::advance_time(double seconds) {
    publish_update(Simulation::advance(
        SimulationState{computer_, people_, emails_, pending_events_, file_system_, network_},
        seconds));
}
void TerminalUI::publish_update(const SimulationUpdate& update) {
    for (const auto& message : update.messages)
        add_log(message);
    for (const auto& payment : update.payments)
        add_log(payment, NoticeKind::Success);
    for (const auto event : update.events) {
        switch (event) {
        case SimulationEvent::EngineerReviewStarted:
            add_log("Mark has checked your reply. He wants to talk.");
            break;
        case SimulationEvent::JessicaDiscovered:
            add_log("Jessica sent a message.");
            break;
        }
    }
}

bool TerminalUI::navigation_bound(KeyAction action, int key) const {
    if (settings_.bound(action, key))
        return true;
    if (action == KeyAction::SelectPrevious)
        return settings_.bound(KeyAction::NavigateUp, key);
    if (action == KeyAction::SelectNext)
        return settings_.bound(KeyAction::NavigateDown, key);
    if (action == KeyAction::ScrollLeft)
        return settings_.bound(KeyAction::NavigateLeft, key);
    if (action == KeyAction::ScrollRight)
        return settings_.bound(KeyAction::NavigateRight, key);
    return false;
}
void TerminalUI::toggle_pane_focus() {
    if (selected_tab() == Tab::Chat)
        messages_ui_.chat_contacts_focused_ =
            ChatSystem::contacts_visible(people_) && !messages_ui_.chat_contacts_focused_;
    else if (selected_tab() == Tab::People) {
        dossiers_ui_.people_details_focused_ = !dossiers_ui_.people_details_focused_;
        dossiers_ui_.people_reveal_section_ = false;
    } else if (selected_tab() == Tab::Network)
        network_ui_.network_details_focused_ = !network_ui_.network_details_focused_;
}

void TerminalUI::run() {
    root_.start_session();
    apply_settings();

    using WallClock = std::chrono::steady_clock;
    auto previous = WallClock::now();

    bool end_reported = false;

    if (!ChatSystem::connected(people_))
        add_log("Engineer connected locally. Choose a response.");
    else
        add_log("World ready at " + clock_text() + ". " +
                settings_.binding_label(KeyAction::Pause) + ": pause/resume; " +
                settings_.binding_label(KeyAction::NextTab) + ": next tab.");

    while (!root_.exit_requested() && !quit_requested_) {
        update_people();
        draw();

        const bool modal_was_active = menu_ui_.menu_active_ || text_ui_.text_active_;
        const int key = root_.renderer().input(settings_.refresh_milliseconds);
        const auto now = WallClock::now();

        const double elapsed = std::chrono::duration<double>(now - previous).count();

        previous = now;

        if (!modal_was_active && !paused_ && !ShutdownSystem::active(people_) &&
            ChatSystem::connected(people_) && !computer_.day_finished()) {
            const auto before = computer_.time_seconds();
            advance_time(elapsed * computer_.playback_speed());
            notify_email_arrivals(before);
        }

        if (key != ERR && key != KEY_RESIZE) {
            try {
                root_.handle_key(key);

            } catch (const std::exception& error) {
                add_log(std::string("Error: ") + error.what(), NoticeKind::Error);
            }
        }

        if (!computer_.day_finished())
            end_reported = false;
        if (computer_.day_finished() && !end_reported) {
            paused_ = true;
            end_reported = true;
            add_log("Workday ended. Open the game menu to start the next day or save.");
        }
    }
    if (settings_.autosave_on_quit && !quit_requested_)
        saves_.save("autosave", capture_world());
}

} // namespace ai_sim::ui
