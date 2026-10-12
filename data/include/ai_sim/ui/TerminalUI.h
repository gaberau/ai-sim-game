// Module: Bridge the live world and gameplay actions to RootUI-owned child pages.
#pragma once

#include "ai_sim/Computer.h"
#include "ai_sim/DefinitionLoader.h"
#include "ai_sim/ui/UISettings.h"
#include "ai_sim/ui/RootUI.h"
#include "ai_sim/ui/PageState.h"
#include "ai_sim/ui/UIStyle.h"
#include "ai_sim/Network.h"
#include "ai_sim/FileSystem.h"
#include "ai_sim/SaveSystem.h"
#include "ai_sim/Simulation.h"
#include "ai_sim/EmailSystem.h"
#include "ai_sim/PeopleSystem.h"
#include "ai_sim/ChatSystem.h"
#include "ai_sim/ChatResponses.h"
#include "ai_sim/ConversationSystem.h"
#include <iosfwd>
#include <functional>

#include <cstdint>
#include <chrono>
#include <deque>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace ai_sim::ui {

class ScreenRenderer;

enum class Tab {
    Chat,
    Processes,
    Network,
    Files,
    News,
    AI,
    People,
    Web,
    Agents,
    Phone,
    Money,
    Modifications,
    Operations,
    Count
};

class TerminalUI {
  public:
    // Initialize ownership and configuration; terminal startup remains explicit.
    explicit TerminalUI(const std::filesystem::path& settings_path = "data/config/settings.toml",
                        const std::filesystem::path& scenario_path = "data/scenarios/hobbyist.toml",
                        bool auto_resume = true);

    // Run the application loop, presenting through RootUI and advancing the live world.
    void run();

  private:
    using EmailFolder = EditorPageState::EmailFolder;
    using NewsArticle = NewsPageState::NewsArticle;
    using DossierPosition = DossiersPageState::DossierPosition;
    using MenuAction = MenuPageState::MenuAction;

    // Own one live model. The aliases below share it with UIContext; restoring a
    // save assigns its members without invalidating these references.
    WorldState world_;
    Computer& computer_ = world_.computer;
    std::filesystem::path settings_path_;
    UISettings settings_;
    ScenarioDefinition definitions_;
    Network& network_ = world_.network;
    FileSystem& file_system_ = world_.files;
    std::filesystem::path scenario_path_;
    SaveSystem saves_;
    toml::table& people_ = world_.people;
    toml::table& emails_ = world_.emails;
    toml::array& pending_events_ = world_.pending_events;
    std::string& scenario_id_ = world_.scenario_id;
    std::string& random_state_ = world_.random_state;

    std::vector<Email> filtered_emails() const;
    void draw_original_email(int top, int bottom, int column, int width);
    void notify_email_arrivals(double previous_time);
    Email selected_email();
    void email_command(std::istringstream& input);
    void edit_email(const std::string& id);

    std::vector<const FileEntry*> file_entries() const;
    void open_file_hosts();
    void select_file_category(int direction);
    void create_category_file(const std::string& name);

    void load_news();
    std::vector<const NewsArticle*> available_news() const;
    void draw_news(int top, int bottom, int width);
    void draw_news_ticker(int row, int column, int width);
    void select_news(int direction);
    void draw_operations(int top, int bottom, int width);
    void open_operations();
    void open_scheduler();
    void draw_modifications(int top, int bottom, int width);
    void draw_ai(int top, int bottom, int width);
    void update_people();
    void advance_time(double seconds);
    void publish_update(const SimulationUpdate& update);
    std::vector<std::string> dossier_people() const;
    void draw_people(int top, int bottom, int width);
    void select_person(int direction);
    void toggle_people();
    void open_people_actions();
    void open_people_connections();
    void open_people_evidence();
    void open_observation(const std::string& id);
    bool navigation_bound(KeyAction action, int key) const;
    void toggle_pane_focus();

    void change_person(const std::string& id);
    std::vector<std::string> visible_people() const;

    Tab selected_tab() const;
    void select_tab(Tab tab);
    int& scroll_offset();
    int list_divider(int width) const {
        return settings_.list_divider(width);
    }
    bool handle_page_key(int key);
    template <class State>
    State& attach_page(Tab tab, const char* id, const char* title,
                       void (TerminalUI::*draw)(int, int, int));
    template <class State> State& attach_overlay(const char* id, State initial, bool centered);
    // RootUI owns child state. The page references below never own or recreate it.
    RootUI root_;
    ScreenRenderer* renderer_ = &root_.renderer();
    MessagesPageState& messages_ui_;
    ProcessesPageState& processes_ui_;
    NetworkPageState& network_ui_;
    FilesPageState& files_ui_;
    NewsPageState& news_ui_;
    ScrollPageState& ai_ui_;
    DossiersPageState& dossiers_ui_;
    WebPageState& web_ui_;
    AgentsPageState& agents_ui_;
    ScrollPageState& phone_ui_;
    ScrollPageState& money_ui_;
    ModificationsPageState& modifications_ui_;
    OperationsPageState& operations_ui_;
    EditorPageState& editor_ui_;
    MenuPageState& menu_ui_;
    TextPageState& text_ui_;
    ChatLine selected_chat_line() const;
    ChatLine selected_chat_line(const std::vector<ChatLine>& timeline) const;
    void draw_opening(int height, int width);
    void draw_chat(int top, int bottom, int width);
    void select_chat(int direction);
    void choose_chat();
    void archive_current_chat();
    void toggle_archived_chats();
    std::vector<ChatContact> visible_chat_contacts() const;
    std::vector<toml::table> chat_options() const;
    std::vector<toml::table> chat_options(const ChatLine& line) const;
    void respond_chat(const toml::table& choice);
    std::optional<double> next_chat_response_time() const;
    void open_chat_request(const std::string& id);
    bool tab_available(Tab tab) const;
    std::pair<int, std::string> file_access(const FileEntry&) const;
    bool& paused_ = world_.paused;

    enum class NoticeKind { Routine, Success, Error };
    std::deque<std::string> log_;
    std::string important_notice_;
    NoticeKind important_notice_kind_ = NoticeKind::Routine;
    std::chrono::steady_clock::time_point important_notice_until_{};
    void draw_notification(int row, int width);

    bool quit_requested_ = false;
    void open_menu(const std::string&, std::vector<MenuAction>, std::function<void()> back = {});
    void open_text(const std::string&, const std::string&, std::function<void(const std::string&)>,
                   const std::string& initial = {}, std::function<void()> back = {});
    void open_actions();
    void open_game_menu();
    void draw_money(int top, int bottom, int width);
    void open_money_tools();
    void open_upgrades();
    void research_selected_upgrade();
    void draw_agents(int top, int bottom, int width);
    void open_agent_actions(const std::string& id);
    void run_agent_control();
    void edit_agent_resource(const std::string& id, int field);
    std::string market_url() const;
    std::string infrastructure_url() const;
    void open_infrastructure();
    void open_infrastructure_sector(const std::string& sector);
    void open_infrastructure_facility(const std::string& id);
    void draw_web(int top, int bottom, int width);
    void follow_web_link();
    void open_web_address();
    void open_stock_market();
    void open_stock(const std::string& symbol);
    void open_stock_orders();
    void open_stock_history();
    void enter_stock_order(const std::string& symbol, bool buying);
    void enter_stock_trade(const std::string& symbol, bool buying);
    void open_money_tool(const std::string& id);
    void open_settings();
    void open_context_help();
    void apply_settings_edit(const std::string& section, const std::string& key,
                             bool editing_keybindings, const std::string& literal);
    void open_settings_section(const std::string& section, bool keys = false);
    void edit_settings_value(const std::string& section, const std::string& key,
                             bool editing_keybindings, const std::string& current_literal);
    void open_save_menu(bool loading);
    void open_permissions(const std::string& path);
    void open_documents(const std::string& email_id);
    void open_document(const std::string& email_id, std::size_t index);
    void open_attachments(const std::string& email_id);
    void draw_menu(int top, int bottom, int width);
    void draw_text_dialog(int top, int bottom, int width);
    void handle_modal_key(int key);
    void run_action(const std::string& action);
    void show_message(const std::string&, const std::string&);

    void apply_settings();
    void draw();

    void draw_processes(int top, int bottom, int width);
    void draw_network(int top, int bottom, int width);
    void select_network_host(int direction);
    void pan_network(int horizontal, int vertical);
    void draw_files(int top, int bottom, int width);
    void select_file(int direction);
    void open_selected_file();
    void change_file_directory(const std::string& path);
    void switch_file_host(const std::string& host);
    Identity file_identity() const;
    WorldState capture_world() const;
    void restore_world(WorldState world);
    void edit_file(const std::string& path);
    void draw_editor(int top, int bottom, int width);
    void handle_editor_key(int key);

    void write(int row, int column, const std::string& value, int color = Plain, bool bold = false,
               bool dim = false) const;

    void line_cell(int row, int column, char symbol, int color) const;

    void add_log(const std::string& message, NoticeKind kind = NoticeKind::Routine);
    bool execute(const std::string& command);

    void scroll_view(int direction);
    void select_process(int direction);
    void toggle_process();
    void scroll_process_columns(int direction);

    std::string clock_text() const;

    static std::string number(double value, int precision = 2);
    static double gib(std::uint64_t bytes);
};

} // namespace ai_sim::ui
