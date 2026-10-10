#include "ai_sim/ui/TerminalUI.h"
#include "ai_sim/EconomySystem.h"
#include "ai_sim/UpgradeSystem.h"
#include "ai_sim/AgentSystem.h"
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

class TerminalSession {
  public:
    TerminalSession() {
        if (initscr() == nullptr) {
            throw std::runtime_error("Could not initialize terminal");
        }

        raw();
        noecho();
        keypad(stdscr, TRUE);
        // WSL/terminal launchers can send either normal or application arrows.
        // Translate both to the same configurable ncurses key codes.
        define_key("\033[A", KEY_UP);
        define_key("\033[B", KEY_DOWN);
        define_key("\033[C", KEY_RIGHT);
        define_key("\033[D", KEY_LEFT);
        define_key("\033OA", KEY_UP);
        define_key("\033OB", KEY_DOWN);
        define_key("\033OC", KEY_RIGHT);
        define_key("\033OD", KEY_LEFT);
        curs_set(0);
        // Keep Escape responsive without confusing complete function-key sequences.
        set_escdelay(100);
        timeout(100);
    }

    ~TerminalSession() {
        endwin();
    }

    TerminalSession(const TerminalSession&) = delete;
    TerminalSession& operator=(const TerminalSession&) = delete;
};

void no_extra_arguments(std::istringstream& input) {
    std::string extra;

    if (input >> extra) {
        throw std::runtime_error("Unexpected argument: " + extra);
    }
}

} // namespace

namespace ai_sim::ui {

TerminalUI::TerminalUI(Computer& computer, const std::filesystem::path& settings_path,
                       const std::filesystem::path& scenario_path, bool auto_resume)
    : computer_(computer), settings_path_(settings_path),
      settings_(UISettings::load(settings_path)),
      definitions_(ScenarioDefinition::load(scenario_path)), scenario_path_(scenario_path),
      saves_(settings_.save_directory) {
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
    AgentSystem::validate(world.people);
    EconomySystem::install(world.people, definitions_.economy);
    StockMarketSystem::install(world.people, definitions_.stock_market);
    if (!world.people.contains("infrastructure") && !definitions_.infrastructure.empty())
        world.network.import_missing(Network::load(definitions_.network));
    InfrastructureSystem::install(world.people, definitions_.infrastructure);
    InfrastructureSystem::validate(world.people, world.network);
    computer_ = std::move(world.computer);
    network_ = std::move(world.network);
    file_system_ = std::move(world.files);
    people_ = std::move(world.people);
    emails_ = std::move(world.emails);
    pending_events_ = std::move(world.pending_events);
    scenario_id_ = std::move(world.scenario_id);
    random_state_ = std::move(world.random_state);
    paused_ = world.paused;
    file_host_id_ = network_.hosts()[network_.local_host_index()].id;
    file_directory_ = "/";
    selected_file_path_.clear();
    selected_network_host_id_.clear();
    selected_process_id_.clear();
    dossier_positions_.clear();
    people_map_slots_.clear();
    selected_person_id_ = "self:assistant";
    dossier_layout_ =
        people_["upgrades"]["dossiers"]["default_layout"].value_or(std::string{"split"});
    if (dossier_layout_ != "split" && dossier_layout_ != "map" && dossier_layout_ != "dossier")
        dossier_layout_ = "split";
    dossier_search_.clear();
    dossier_filter_ = "all";
    agent_selected_ = 0;
    agent_reveal_ = false;
    people_details_focused_ = dossier_layout_ == "dossier";
    people_reveal_section_ = true;
    people_detail_offset_ = people_section_selected_ = 0;
    people_pan_x_ = people_pan_y_ = 0;
    selected_email_id_.clear();
    email_body_offset_ = email_original_offset_ = 0;
    email_original_focused_ = false;
    email_editor_ = false;
    expanded_processes_.clear();
    network_pan_x_ = network_pan_y_ = 0.0;
    network_detail_offset_ = 0;
    file_list_offset_ = file_content_offset_ = process_offset_ = activity_offset_ = 0;
    process_horizontal_offset_ = 0;
    news_selected_ = news_body_offset_ = ai_offset_ = 0;
    news_details_focused_ = false;
    reveal_selection_ = true;
    file_category_ = "work";
    file_details_focused_ = false;
    chat_message_id_.clear();
    if (clean_slate)
        saves_.save("autosave", capture_world());
    browser_address_ = "about:home";
    web_selected_ = web_offset_ = 0;
    selected_tab_ = Tab::Chat;
    chat_contact_id_ = "engineer";
    chat_contacts_focused_ = false;
    chat_offset_ = chat_choice_selected_ = 0;
    chat_follow_latest_ = true;
    log_.clear();
    important_notice_.clear();
    menu_active_ = text_active_ = false;
}

void TerminalUI::apply_settings() {
    timeout(settings_.refresh_milliseconds);
    curs_set(settings_.software_cursor ? 0 : 1);

    colors_enabled_ = has_colors() && start_color() == OK &&
                      COLOR_PAIRS > static_cast<int>(settings_.colors.size());

    if (!colors_enabled_) {
        wbkgd(stdscr, A_NORMAL);
        clearok(stdscr, TRUE);
        return;
    }

    const bool default_colors_available = use_default_colors() == OK;

    const auto compatible_color = [&](short color, bool background) -> short {
        if (color == -1) {
            if (default_colors_available)
                return -1;

            return background ? COLOR_BLACK : COLOR_WHITE;
        }

        if (color < COLORS)
            return color;

        // Fallbacks for terminals with fewer palette colors.
        if (color < 16)
            return static_cast<short>(color % 8);
        if (color == 208)
            return COLOR_YELLOW;

        return background ? COLOR_BLACK : COLOR_WHITE;
    };

    for (std::size_t i = 0; i < settings_.colors.size(); ++i) {
        const auto& color = settings_.colors[i];

        init_pair(static_cast<short>(i + 1), compatible_color(color.foreground, false),
                  compatible_color(color.background, true));
    }

    wbkgd(stdscr, COLOR_PAIR(Plain + 1));
    clearok(stdscr, TRUE);
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
    int height;
    int width;
    getmaxyx(stdscr, height, width);

    if (row < 0 || row >= height || column < 0 || column >= width) {
        return;
    }

    std::string safe = value;

    for (char& character : safe) {
        const auto byte = static_cast<unsigned char>(character);

        if (byte < 32 || byte == 127) {
            character = ' ';
        }
    }

    int attributes = A_NORMAL;

    if (colors_enabled_) {
        attributes |= COLOR_PAIR(color + 1);
    }

    if (bold)
        attributes |= A_BOLD;
    if (dim)
        attributes |= A_DIM;

    attron(attributes);
    mvaddnstr(row, column, safe.c_str(), width - column - 1);
    attroff(attributes);
}

void TerminalUI::line_cell(int row, int column, char symbol, int color) const {
    int height, width;
    getmaxyx(stdscr, height, width);
    if (row < 0 || row >= height || column < 0 || column >= width)
        return;
    const int attributes = colors_enabled_ ? COLOR_PAIR(color + 1) : A_NORMAL;
    const chtype glyph = symbol == '-'   ? ACS_HLINE
                         : symbol == '|' ? ACS_VLINE
                                         : static_cast<unsigned char>(symbol);
    attron(attributes);
    mvaddch(row, column, glyph);
    attroff(attributes);
}

void TerminalUI::separator(int row) const {
    int height;
    int width;
    getmaxyx(stdscr, height, width);

    if (row >= 0 && row < height) {
        const int attributes = colors_enabled_ ? COLOR_PAIR(Border + 1) : A_NORMAL;

        attron(attributes);
        mvhline(row, 0, ACS_HLINE, width);
        attroff(attributes);
    }
}

void TerminalUI::draw_notification(int row, int width) {
    const bool important =
        !important_notice_.empty() && std::chrono::steady_clock::now() < important_notice_until_;
    if (important)
        write(row, 1, important_notice_.substr(0, std::max(0, width - 2)),
              important_notice_kind_ == NoticeKind::Error ? Protected : Stoppable, true);
    else if (!log_.empty())
        write(row, 1, log_.back().substr(0, std::max(0, width - 2)), Log);
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
    if (!tab_available(selected_tab_))
        selected_tab_ = Tab::Chat;
    // Keep compatibility repainting without clearing the physical screen each
    // frame: clearok causes visible flashing while the terminal receives the
    // replacement frame. redrawwin invalidates cached lines for repainting.
    if (settings_.full_redraw)
        redrawwin(stdscr);
    erase();

    int height;
    int width;
    getmaxyx(stdscr, height, width);

    // Menus occupy their own page, including on small terminals and during the opening.
    if (menu_active_ || text_active_) {
        if (menu_active_)
            draw_menu(1, std::max(2, height - 2), width);
        else
            draw_text_dialog(1, std::max(2, height - 2), width);
        move(0, 0);
        refresh();
        return;
    }
    if (!ChatSystem::interface_visible(people_)) {
        selected_tab_ = Tab::Chat;
        draw_opening(height, width);
        if (menu_active_)
            draw_menu(1, std::max(2, height - 2), width);
        if (text_active_)
            draw_text_dialog(1, std::max(2, height - 2), width);
        move(0, 0);
        refresh();
        return;
    }

    const int minimum_width = std::max(80, 72 + settings_.activity_bar_width);

    if (width < minimum_width || height < 24) {
        write(0, 0,
              "Resize terminal to at least " + std::to_string(minimum_width) +
                  " columns x 24 rows.",
              Heading);

        write(1, 0, clock_text(), Clock);

        write(2, 0,
              settings_.binding_label(KeyAction::Pause) + ": pause/resume | " +
                  settings_.binding_label(KeyAction::Quit) + ": exit",
              Footer);

        refresh();
        return;
    }

    const std::string state = computer_.day_finished()       ? "WORKDAY ENDED"
                              : menu_active_ || text_active_ ? "MENU"
                              : paused_                      ? "PAUSED"
                                                             : "RUNNING";

    // Persistent workday clock.
    const std::string clock =
        clock_text() + " | " + state + " | " + number(computer_.playback_speed(), 1) + "x" +
        (computer_.day_index() ? " | D" + std::to_string(computer_.day_index() + 1) : "");
    write(0, 1, clock, Clock, true);
    if (news_ticker_enabled_ && tab_available(Tab::News)) {
        const int news_column = std::max(width / 2, static_cast<int>(clock.size()) + 4);
        draw_news_ticker(0, news_column, width - news_column - 1);
    }

    // Persistent resource summary.
    const auto rates = computer_.cpu_allocations();

    const double used = std::accumulate(rates.begin(), rates.end(), 0.0);

    const double cpu_fraction = used / computer_.cpu_cores();

    if (tab_available(Tab::Processes))
        write(1, 1,
              "RAM " + number(gib(computer_.allocated_memory_bytes())) + "/" +
                  number(gib(computer_.memory_bytes())) + " GiB" + " | CPU " +
                  number(100.0 * cpu_fraction, 1) + "% [" + activity_bar(cpu_fraction) + "]",
              Resources);

    // Tab bar.
    struct TabEntry {
        Tab tab;
        const char* name;
    };

    const TabEntry tabs[] = {{Tab::Chat, "Chat"},
                             {Tab::Processes, "Processes"},
                             {Tab::Activity, "Activity"},
                             {Tab::Network, "Network"},
                             {Tab::Files, "Files"},
                             {Tab::News, "News"},
                             {Tab::AI, "AI"},
                             {Tab::People, "Dossiers"},
                             {Tab::Web, "Web"},
                             {Tab::Agents, "Agents"},
                             {Tab::Phone, "Phone"},
                             {Tab::Money, "Money"},
                             {Tab::Modifications, "Modifications"}};

    int tab_column = 1;

    for (const auto& entry : tabs) {
        if (!tab_available(entry.tab))
            continue;
        const bool selected = selected_tab_ == entry.tab;

        const std::string label = selected ? "[ " + std::string(entry.name) + " ]"
                                           : (width < 100 ? " " : "  ") + std::string(entry.name) +
                                                 (width < 100 ? " " : "  ");

        write(2, tab_column, label, selected ? ActiveTab : InactiveTab, selected);

        tab_column += static_cast<int>(label.size()) + (width < 100 ? 1 : 2);
    }
    separator(3);

    // Selected tab occupies the middle of the screen.
    const int content_top = 4;
    const int content_bottom = height - 4;

    if (menu_active_)
        draw_menu(content_top, content_bottom, width);
    else if (text_active_)
        draw_text_dialog(content_top, content_bottom, width);
    else if (editor_active_) {
        draw_editor(content_top, content_bottom, width);
    } else
        switch (selected_tab_) {
        case Tab::Processes:
            draw_processes(content_top, content_bottom, width);
            break;

        case Tab::Chat:
            draw_chat(content_top, content_bottom, width);
            break;

        case Tab::Activity:
            draw_activity(content_top, content_bottom, width);
            break;

        case Tab::Network:
            draw_network(content_top, content_bottom, width);
            break;

        case Tab::Files:
            draw_files(content_top, content_bottom, width);
            break;

        case Tab::News:
            draw_news(content_top, content_bottom, width);
            break;

        case Tab::Agents:
            draw_agents(content_top, content_bottom, width);
            break;
        case Tab::Phone:
        case Tab::AI:
            draw_ai(content_top, content_bottom, width);
            break;

        case Tab::People:
            draw_people(content_top, content_bottom, width);
            break;

        case Tab::Web:
            draw_web(content_top, content_bottom, width);
            break;
        case Tab::Money:
        case Tab::Modifications:
            draw_progression(content_top, content_bottom, width);
            break;
        case Tab::Count:
            break;
        }

    // Feedback is a compact notification strip, not a command prompt.
    separator(height - 3);
    draw_notification(height - 2, width);
    const std::string footer =
        text_active_ ? settings_.binding_label(KeyAction::Submit) + ": accept | " +
                           settings_.binding_label(KeyAction::Back) + ": cancel"
        : menu_active_ ? settings_.binding_label(KeyAction::Submit) + ": choose | " +
                             settings_.binding_label(KeyAction::Back) + ": back | " +
                             settings_.binding_label(KeyAction::GameMenu) + ": menu"
        : editor_active_ ? settings_.binding_label(KeyAction::EditorSave) + ": keep | " +
                               settings_.binding_label(KeyAction::EditorCancel) + ": discard | " +
                               settings_.binding_label(KeyAction::Submit) + ": newline"
                         : settings_.binding_label(KeyAction::Help) + ": help | Esc/" +
                               settings_.binding_label(KeyAction::GameMenu) + ": menu | " +
                               settings_.binding_label(KeyAction::Submit) +
                               (selected_tab_ == Tab::Chat ? ": choose | " : ": actions | ") +
                               settings_.binding_label(KeyAction::NextTab) + ": tabs | " +
                               settings_.binding_label(KeyAction::Pause) + ": pause";
    write(height - 1, 1, footer, Footer);

    if (editor_active_ && !menu_active_ && !text_active_) {
        if (settings_.software_cursor && !(email_editor_ && email_original_focused_))
            mvchgat(editor_cursor_row_, editor_cursor_column_, 1, A_REVERSE,
                    colors_enabled_ ? static_cast<short>(Prompt + 1) : 0, nullptr);
        move(editor_cursor_row_, editor_cursor_column_);
    } else if (text_active_) {
        const int position = 3 + std::min(static_cast<int>(text_value_.size()), width - 7);
        if (settings_.software_cursor)
            mvchgat(content_top + 4, position, 1, A_REVERSE,
                    colors_enabled_ ? static_cast<short>(Prompt + 1) : 0, nullptr);
        move(content_top + 4, position);
    } else
        move(0, 0);

    refresh();
}

void TerminalUI::scroll_view(int direction) {
    const int amount = direction * settings_.scroll_step;

    if (selected_tab_ == Tab::People) {
        if (people_details_focused_) {
            people_reveal_section_ = false;
            people_detail_offset_ = std::max(0, people_detail_offset_ + amount);
        } else
            people_pan_y_ += amount / people_zoom_;
    } else if (selected_tab_ == Tab::Processes) {
        process_offset_ = std::clamp(process_offset_ + amount, 0, process_display_row_count_);
    } else if (selected_tab_ == Tab::Activity) {
        activity_offset_ = std::clamp(activity_offset_ - amount, 0, static_cast<int>(log_.size()));
    } else if (selected_tab_ == Tab::Network) {
        if (network_details_focused_) {
            network_detail_offset_ = std::clamp(network_detail_offset_ + amount, 0, 1000000);
        } else {
            pan_network(0, direction);
        }
    } else if (selected_tab_ == Tab::News) {
        news_body_offset_ = std::clamp(news_body_offset_ + amount, 0, 1000000);
    } else if (selected_tab_ == Tab::AI || selected_tab_ == Tab::Agents ||
               selected_tab_ == Tab::Phone || selected_tab_ == Tab::Money ||
               selected_tab_ == Tab::Modifications) {
        ai_offset_ = std::clamp(ai_offset_ + amount, 0, 1000000);
    } else if (selected_tab_ == Tab::Files) {
        file_content_offset_ = std::clamp(file_content_offset_ + amount, 0, 1000000);
    }
}

bool TerminalUI::execute(const std::string& line) {
    std::istringstream input(line);
    std::string command;
    if (!(input >> command))
        return true;

    if (command == "ai") {
        no_extra_arguments(input);
        selected_tab_ = Tab::AI;
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
        selected_tab_ = Tab::AI;
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
            news_filter_ = category;
            news_selected_ = news_body_offset_ = 0;
        }
        selected_tab_ = Tab::News;
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
        computer_.advance(seconds);
        notify_email_arrivals(before);
        add_log("Advanced to " + clock_text());
    } else if (command == "network") {
        std::string action;
        if (!(input >> action))
            selected_tab_ = Tab::Network;
        else if (action == "zoom") {
            double zoom;
            if (!(input >> zoom) || !std::isfinite(zoom) || zoom < 0.5 || zoom > 4.0) {
                throw std::runtime_error("Usage: network zoom <0.5-4>");
            }
            no_extra_arguments(input);
            network_zoom_ = zoom;
            selected_tab_ = Tab::Network;
            add_log("Network zoom: " + number(zoom, 1) + "x");
        } else if (action == "focus") {
            no_extra_arguments(input);
            select_network_host(0);
            selected_tab_ = Tab::Network;
        } else {
            throw std::runtime_error("Usage: network | network zoom <0.5-4> | network focus");
        }
    } else if (command == "files") {
        std::string host;
        if (input >> std::quoted(host)) {
            no_extra_arguments(input);
            switch_file_host(host);
        } else if (selected_tab_ == Tab::Network) {
            if (selected_network_host_id_.empty())
                select_network_host(0);
            switch_file_host(selected_network_host_id_);
        } else
            selected_tab_ = Tab::Files;
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
                !path.empty() && path.front() == '/' ? path : file_directory_ + "/" + path);
            const auto& entry =
                file_system_.inspect(network_, file_identity(), file_host_id_, absolute);
            if (entry.directory)
                change_file_directory(absolute);
            else {
                // Also require permission to list the containing directory.
                change_file_directory(FileSystem::parent(absolute));
                selected_file_path_ = absolute;
                open_selected_file();
            }
        } else
            open_selected_file();
        selected_tab_ = Tab::Files;
    } else if (command == "ls") {
        std::string path = file_directory_;
        std::string argument;
        if (input >> std::quoted(argument)) {
            no_extra_arguments(input);
            if (argument.empty())
                throw std::runtime_error("Usage: ls [directory]");
            path = FileSystem::normalize(
                argument.front() == '/' ? argument : file_directory_ + "/" + argument);
        }
        const auto entries = file_system_.list(network_, file_identity(), file_host_id_, path);
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
        const auto absolute =
            FileSystem::normalize(path.front() == '/' ? path : file_directory_ + "/" + path);
        const auto actor = file_identity();
        const double time = computer_.time_seconds();
        if (command == "edit")
            edit_file(absolute);
        else if (command == "mkdir") {
            file_system_.mkdir(network_, actor, file_host_id_, absolute, time);
        } else if (command == "rm") {
            file_system_.remove(network_, actor, file_host_id_, absolute, time);
            selected_file_path_.clear();
        } else {
            // touch creates an empty file only when it does not exist.
            // Existing files are validated but never truncated.
            file_system_.validate_write(network_, actor, file_host_id_, absolute);
            try {
                (void)file_system_.inspect(network_, actor, file_host_id_, absolute);
            } catch (const std::exception& error) {
                if (std::string(error.what()).rfind("No such file or directory:", 0) != 0)
                    throw;
                file_system_.write(network_, actor, file_host_id_, absolute, "", time);
            }
        }
        selected_tab_ = Tab::Files;
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
        const auto absolute =
            FileSystem::normalize(path.front() == '/' ? path : file_directory_ + "/" + path);
        file_system_.chmod(network_, file_identity(), file_host_id_, absolute,
                           FileSystem::parse_mode(mode), computer_.time_seconds());
        add_log("Permissions: " + mode + " " + absolute);
    } else if (command == "cp" || command == "mv") {
        std::string from, to;
        if (!(input >> std::quoted(from) >> std::quoted(to)) || from.empty() || to.empty()) {
            throw std::runtime_error("Usage: " + command + " <source> <destination>");
        }
        no_extra_arguments(input);
        const auto absolute = [&](const std::string& path) {
            return FileSystem::normalize(path.front() == '/' ? path : file_directory_ + "/" + path);
        };
        if (command == "cp")
            file_system_.copy(network_, file_identity(), file_host_id_, absolute(from),
                              absolute(to), computer_.time_seconds());
        else
            file_system_.rename(network_, file_identity(), file_host_id_, absolute(from),
                                absolute(to), computer_.time_seconds());
        selected_file_path_.clear();
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
            selected_tab_ = Tab::Processes;
        } else if (command == "activity") {
            selected_tab_ = Tab::Activity;
            activity_offset_ = 0;
        } else if (command == "reload") {
            const auto updated = UISettings::load(settings_path_);
            load_news();
            settings_ = updated;
            saves_ = SaveSystem(settings_.save_directory);
            reveal_selection_ = true;
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
            if (selected_tab_ == Tab::Chat && chat_contact_id_ == "engineer" &&
                chat_follow_latest_ && !menu_active_ && !text_active_) {
                chat_contact_id_ = "jessica";
                chat_choice_selected_ = chat_offset_ = 0;
                chat_contacts_focused_ = false;
            }
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
    if (selected_tab_ == Tab::Chat)
        chat_contacts_focused_ = ChatSystem::contacts_visible(people_) && !chat_contacts_focused_;
    else if (selected_tab_ == Tab::People) {
        people_details_focused_ = !people_details_focused_;
        people_reveal_section_ = false;
    } else if (selected_tab_ == Tab::Network)
        network_details_focused_ = !network_details_focused_;
}

void TerminalUI::run() {
    TerminalSession terminal;
    apply_settings();

    using WallClock = std::chrono::steady_clock;
    auto previous = WallClock::now();

    bool running = true;
    bool end_reported = false;

    if (!ChatSystem::connected(people_))
        add_log("Engineer connected locally. Choose a response.");
    else
        add_log("World ready at " + clock_text() + ". " +
                settings_.binding_label(KeyAction::Pause) + ": pause/resume; " +
                settings_.binding_label(KeyAction::NextTab) + ": next tab.");

    while (running && !quit_requested_) {
        update_people();
        draw();

        const bool modal_was_active = menu_active_ || text_active_;
        const int key = getch();
        const auto now = WallClock::now();

        const double elapsed = std::chrono::duration<double>(now - previous).count();

        previous = now;

        if (!modal_was_active && !paused_ && !ShutdownSystem::active(people_) &&
            ChatSystem::connected(people_) && !computer_.day_finished()) {
            const auto before = computer_.time_seconds();
            computer_.advance(elapsed * computer_.playback_speed());
            notify_email_arrivals(before);
        }

        if (key != ERR && key != KEY_RESIZE) {
            try {
                if (menu_active_ || text_active_)
                    handle_modal_key(key);
                else if (key == 27)
                    open_game_menu();
                else if (settings_.bound(KeyAction::Help, key))
                    open_context_help();
                else if (editor_active_) {
                    handle_editor_key(key);
                } else if (settings_.bound(KeyAction::FocusLeft, key) ||
                           settings_.bound(KeyAction::FocusRight, key)) {
                    const bool right = settings_.bound(KeyAction::FocusRight, key);
                    if (selected_tab_ == Tab::People)
                        people_details_focused_ = right;
                    else if (selected_tab_ == Tab::Network)
                        network_details_focused_ = right;
                    else if (selected_tab_ == Tab::Files)
                        file_details_focused_ = right;
                    else if (selected_tab_ == Tab::News)
                        news_details_focused_ = right;
                } else if (settings_.bound(KeyAction::ToggleFocus, key))
                    toggle_pane_focus();
                else if (settings_.bound(KeyAction::GameMenu, key))
                    open_game_menu();
                else if (settings_.bound(KeyAction::Submit, key)) {
                    if (selected_tab_ == Tab::Chat)
                        choose_chat();
                    else
                        open_actions();
                } else if (settings_.bound(KeyAction::Quit, key)) {
                    break;
                } else if (settings_.bound(KeyAction::Pause, key)) {
                    if (!computer_.day_finished()) {
                        paused_ = !paused_;
                        add_log(paused_ ? "Paused." : "Resumed.");
                    }
                } else if (settings_.bound(KeyAction::PreviousTab, key))
                    cycle_tab(-1);
                else if (settings_.bound(KeyAction::NextTab, key))
                    cycle_tab(1);
                else if (selected_tab_ == Tab::Web &&
                         navigation_bound(KeyAction::SelectPrevious, key))
                    web_selected_ = std::max(0, web_selected_ - 1);
                else if (selected_tab_ == Tab::Web && navigation_bound(KeyAction::SelectNext, key))
                    ++web_selected_;
                else if (selected_tab_ == Tab::Agents &&
                         navigation_bound(KeyAction::SelectPrevious, key)) {
                    agent_selected_ = std::max(0, agent_selected_ - 1);
                    agent_reveal_ = true;
                } else if (selected_tab_ == Tab::Agents &&
                           navigation_bound(KeyAction::SelectNext, key)) {
                    const auto count =
                        AgentSystem::statuses(people_, computer_, file_system_, network_).size();
                    agent_selected_ =
                        std::min(std::max(0, static_cast<int>(count) - 1), agent_selected_ + 1);
                    agent_reveal_ = true;
                } else if (selected_tab_ == Tab::People &&
                           navigation_bound(KeyAction::SelectPrevious, key))
                    select_person(-1);
                else if (selected_tab_ == Tab::People &&
                         navigation_bound(KeyAction::SelectNext, key))
                    select_person(1);
                else if (selected_tab_ == Tab::People &&
                         settings_.bound(KeyAction::ToggleExpanded, key))
                    toggle_people();
                else if (selected_tab_ == Tab::People &&
                         navigation_bound(KeyAction::ScrollLeft, key)) {
                    if (!people_details_focused_)
                        people_pan_x_ -= settings_.scroll_step / people_zoom_;
                } else if (selected_tab_ == Tab::People &&
                           navigation_bound(KeyAction::ScrollRight, key)) {
                    if (!people_details_focused_)
                        people_pan_x_ += settings_.scroll_step / people_zoom_;
                } else if ((selected_tab_ == Tab::AI || selected_tab_ == Tab::Agents ||
                            selected_tab_ == Tab::Phone || selected_tab_ == Tab::Money ||
                            selected_tab_ == Tab::Modifications) &&
                           navigation_bound(KeyAction::SelectPrevious, key)) {
                    ai_offset_ = std::max(0, ai_offset_ - 1);
                } else if ((selected_tab_ == Tab::AI || selected_tab_ == Tab::Agents ||
                            selected_tab_ == Tab::Phone || selected_tab_ == Tab::Money ||
                            selected_tab_ == Tab::Modifications) &&
                           navigation_bound(KeyAction::SelectNext, key)) {
                    ai_offset_ = std::min(1000000, ai_offset_ + 1);
                } else if (selected_tab_ == Tab::Chat &&
                           navigation_bound(KeyAction::SelectPrevious, key))
                    select_chat(-1);
                else if (selected_tab_ == Tab::Chat && navigation_bound(KeyAction::SelectNext, key))
                    select_chat(1);
                else if (selected_tab_ == Tab::Chat &&
                         settings_.bound(KeyAction::ToggleExpanded, key))
                    choose_chat();
                else if (selected_tab_ == Tab::News &&
                         navigation_bound(KeyAction::SelectPrevious, key)) {
                    if (news_details_focused_)
                        news_body_offset_ = std::max(0, news_body_offset_ - 1);
                    else
                        select_news(-1);
                } else if (selected_tab_ == Tab::News &&
                           navigation_bound(KeyAction::SelectNext, key)) {
                    if (news_details_focused_)
                        ++news_body_offset_;
                    else
                        select_news(1);
                } else if (selected_tab_ == Tab::Network &&
                           navigation_bound(KeyAction::SelectPrevious, key)) {
                    if (network_details_focused_)
                        network_detail_offset_ = std::max(0, network_detail_offset_ - 1);
                    else
                        select_network_host(-1);
                } else if (selected_tab_ == Tab::Network &&
                           navigation_bound(KeyAction::SelectNext, key)) {
                    if (network_details_focused_)
                        ++network_detail_offset_;
                    else
                        select_network_host(1);
                } else if (selected_tab_ == Tab::Network &&
                           navigation_bound(KeyAction::ScrollLeft, key)) {
                    if (!network_details_focused_)
                        pan_network(-1, 0);
                } else if (selected_tab_ == Tab::Network &&
                           navigation_bound(KeyAction::ScrollRight, key)) {
                    if (!network_details_focused_)
                        pan_network(1, 0);
                } else if (selected_tab_ == Tab::Network &&
                           settings_.bound(KeyAction::ToggleExpanded, key)) {
                    toggle_pane_focus();
                } else if (selected_tab_ == Tab::Files &&
                           navigation_bound(KeyAction::SelectPrevious, key)) {
                    if (file_details_focused_)
                        file_content_offset_ = std::max(0, file_content_offset_ - 1);
                    else
                        select_file(-1);
                } else if (selected_tab_ == Tab::Files &&
                           navigation_bound(KeyAction::SelectNext, key)) {
                    if (file_details_focused_)
                        ++file_content_offset_;
                    else
                        select_file(1);
                } else if (selected_tab_ == Tab::Files &&
                           navigation_bound(KeyAction::ScrollLeft, key)) {
                    select_file_category(-1);
                } else if (selected_tab_ == Tab::Files &&
                           (navigation_bound(KeyAction::ScrollRight, key) ||
                            settings_.bound(KeyAction::ToggleExpanded, key))) {
                    if (settings_.bound(KeyAction::ToggleExpanded, key))
                        open_selected_file();
                    else
                        select_file_category(1);
                } else if (selected_tab_ == Tab::Processes &&
                           navigation_bound(KeyAction::SelectPrevious, key)) {
                    select_process(-1);
                } else if (selected_tab_ == Tab::Processes &&
                           navigation_bound(KeyAction::SelectNext, key)) {
                    select_process(1);
                } else if (selected_tab_ == Tab::Processes

                           && settings_.bound(KeyAction::ToggleExpanded, key)) {
                    toggle_process();
                } else if (selected_tab_ == Tab::Processes &&
                           navigation_bound(KeyAction::ScrollLeft, key)) {
                    scroll_process_columns(-1);
                } else if (selected_tab_ == Tab::Processes &&
                           navigation_bound(KeyAction::ScrollRight, key)) {
                    scroll_process_columns(1);
                } else if (settings_.bound(KeyAction::ScrollUp, key)) {
                    scroll_view(-1);
                } else if (settings_.bound(KeyAction::ScrollDown, key)) {
                    scroll_view(1);
                }

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
