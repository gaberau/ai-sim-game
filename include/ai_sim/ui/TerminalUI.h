#pragma once

#include "ai_sim/Computer.h"
#include "ai_sim/DefinitionLoader.h"
#include "ai_sim/ui/UISettings.h"
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
    Activity,
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
    explicit TerminalUI(Computer& computer,
                        const std::filesystem::path& settings_path = "config/settings.toml",
                        const std::filesystem::path& scenario_path = "data/scenarios/hobbyist.toml",
                        bool auto_resume = true);

    void run();

  private:
    ScreenRenderer* renderer_ = nullptr;
    bool drawing_background_ = false;
    int upgrade_detail_offset_ = 0;
    enum Color {
        Plain = 0,
        Protected = 1,
        Stoppable = 2,
        Player = 3,
        Heading = 4,
        Clock = 5,
        Resources = 6,
        ActiveTab = 7,
        InactiveTab = 8,
        Border = 9,
        Log = 10,
        Prompt = 11,
        Footer = 12,
        FileEdit = 13,
        FileRead = 14,
        FileApproval = 15,
        FileBlocked = 16,
        FileUnknown = 17,
        MapLocked = 18
    };

    Computer& computer_;
    std::filesystem::path settings_path_;
    UISettings settings_;
    ScenarioDefinition definitions_;
    Network network_;
    FileSystem file_system_;
    std::filesystem::path scenario_path_;
    SaveSystem saves_;
    toml::table people_;
    toml::table emails_;
    toml::array pending_events_;
    std::string scenario_id_;
    std::string random_state_;

    enum class EmailFolder { Inbox, Starred, Unread, Archived };
    EmailFolder email_folder_ = EmailFolder::Inbox;
    std::vector<Email> filtered_emails() const;
    void cycle_email_folder(int direction);
    void draw_original_email(int top, int bottom, int column, int width);
    int email_original_offset_ = 0;
    bool email_original_focused_ = false;
    std::string selected_email_id_;
    int email_body_offset_ = 0;
    bool email_editor_ = false;
    std::string email_editor_id_;
    void notify_email_arrivals(double previous_time);
    Email selected_email();
    void select_email(int direction);
    void email_command(std::istringstream& input);
    void edit_email(const std::string& id);

    bool editor_active_ = false;
    std::string editor_host_;
    std::string editor_path_;
    std::string editor_buffer_;
    std::size_t editor_cursor_ = 0;
    int editor_offset_ = 0;
    bool editor_dirty_ = false;
    int editor_cursor_row_ = 0;
    int editor_cursor_column_ = 2;

    std::string selected_network_host_id_;

    double network_pan_x_ = 0.0;
    double network_pan_y_ = 0.0;
    double network_zoom_ = 1.0;
    bool network_details_focused_ = false;
    int network_detail_offset_ = 0;

    std::string file_host_id_;
    std::string file_directory_ = "/";
    std::string file_category_ = "all";
    std::string file_query_;
    std::vector<const FileEntry*> file_entries() const;
    void open_file_hosts();
    bool file_details_focused_ = false;
    void select_file_category(int direction);
    void create_category_file(const std::string& name);
    std::string selected_file_path_;
    int file_list_offset_ = 0;
    int file_content_offset_ = 0;

    struct NewsArticle {
        std::string id, category, headline, source, date, body;
        double release_seconds = 0.0;
    };
    std::vector<NewsArticle> news_articles_;
    std::string news_edition_;
    std::string news_filter_ = "all";
    int news_selected_ = 0;
    int news_body_offset_ = 0;
    bool news_details_focused_ = false;
    bool news_ticker_enabled_ = true;
    double news_ticker_seconds_ = 120.0;
    void load_news();
    std::vector<const NewsArticle*> available_news() const;
    void draw_news(int top, int bottom, int width);
    void draw_news_ticker(int row, int column, int width);
    void select_news(int direction);
    int ai_offset_ = 0;
    int upgrade_selected_ = 0;
    int operations_selected_ = 0;
    void draw_operations(int top, int bottom, int width);
    void open_operations();
    void open_scheduler();
    void draw_modifications(int top, int bottom, int width);
    void draw_ai(int top, int bottom, int width);
    void update_people();
    void advance_time(double seconds);
    void publish_update(const SimulationUpdate& update);
    std::string selected_person_id_ = "self:assistant";
    std::unordered_set<std::string> expanded_people_{"self:assistant"};
    std::unordered_set<std::string> expanded_dossier_sections_{"identity", "relationships"};
    bool people_reveal_section_ = true;
    bool people_details_focused_ = false, people_neighborhood_ = false;
    std::string people_filter_ = "all";
    std::string dossier_layout_ = "split", dossier_search_, dossier_filter_ = "all";
    std::vector<std::string> dossier_people() const;
    double people_pan_x_ = 0, people_pan_y_ = 0, people_zoom_ = 1;
    int people_detail_offset_ = 0, people_section_selected_ = 0;
    void draw_people(int top, int bottom, int width);
    void select_person(int direction);
    void toggle_people();
    void open_people_actions();
    void open_people_connections();
    void open_people_evidence();
    void open_observation(const std::string& id);
    bool navigation_bound(KeyAction action, int key) const;
    void toggle_pane_focus();
    struct DossierPosition {
        int offset = 0, section = 0;
        std::unordered_set<std::string> expanded{"identity"};
    };
    std::unordered_map<std::string, DossierPosition> dossier_positions_;
    std::unordered_map<std::string, std::size_t> people_map_slots_;
    void change_person(const std::string& id);
    std::vector<std::string> visible_people() const;

    Tab selected_tab_ = Tab::Chat;
    std::string chat_contact_id_ = "engineer";
    bool chat_contacts_focused_ = false;
    int chat_offset_ = 0, chat_choice_selected_ = 0;
    bool chat_follow_latest_ = true;
    std::string chat_message_id_;
    ChatLine displayed_chat_line_;
    std::vector<toml::table> displayed_chat_options_;
    void select_chat_message(int direction);
    ChatLine selected_chat_line() const;
    ChatLine selected_chat_line(const std::vector<ChatLine>& timeline) const;
    void draw_opening(int height, int width);
    void draw_chat(int top, int bottom, int width);
    void open_chat_actions();
    void select_chat(int direction);
    void choose_chat();
    std::vector<toml::table> chat_options() const;
    std::vector<toml::table> chat_options(const ChatLine& line) const;
    void respond_chat(const toml::table& choice);
    std::optional<double> next_chat_response_time() const;
    void open_chat_request(const std::string& id);
    void cycle_tab(int direction);
    bool tab_available(Tab tab) const;
    std::pair<int, std::string> file_access(const FileEntry&) const;
    bool paused_ = false;
    bool colors_enabled_ = false;

    enum class NoticeKind { Routine, Success, Error };
    std::deque<std::string> log_;
    std::string important_notice_;
    NoticeKind important_notice_kind_ = NoticeKind::Routine;
    std::chrono::steady_clock::time_point important_notice_until_{};
    void draw_notification(int row, int width);
    struct MenuAction {
        std::string label;
        std::function<void()> invoke;
        std::string disabled_reason;
        std::string detail = {};
    };
    bool menu_active_ = false;
    std::string menu_title_;
    std::vector<MenuAction> menu_actions_;
    int menu_selected_ = 0;
    int menu_offset_ = 0;
    std::function<void()> menu_back_;
    bool text_active_ = false;
    bool key_capture_active_ = false;
    std::string text_title_, text_hint_, text_value_;
    std::function<void(const std::string&)> text_submit_;
    std::function<void()> text_back_;
    bool quit_requested_ = false;
    void open_menu(const std::string&, std::vector<MenuAction>, std::function<void()> back = {});
    void open_text(const std::string&, const std::string&, std::function<void(const std::string&)>,
                   const std::string& initial = {}, std::function<void()> back = {});
    void open_actions();
    void open_game_menu();
    void draw_progression(int top, int bottom, int width);
    void open_money_tools();
    void open_upgrades();
    void draw_agents(int top, int bottom, int width);
    void open_agent_actions(const std::string& id);
    void open_agent_resources(const std::string& id);
    int agent_selected_ = 0;
    bool agent_reveal_ = false;
    std::string browser_address_ = "about:home";
    int web_selected_ = 0, web_offset_ = 0;
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

    // Vertical offsets count display rows, including expanded details.
    int process_offset_ = 0;
    int activity_offset_ = 0;

    int process_display_row_count_ = 0;

    int process_horizontal_offset_ = 0;

    std::string selected_process_id_;
    std::unordered_set<std::string> expanded_processes_;

    bool reveal_selection_ = true;

    void apply_settings();
    void draw();

    void draw_processes(int top, int bottom, int width);
    void draw_activity(int top, int bottom, int width);
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

    void separator(int row) const;
    void line_cell(int row, int column, char symbol, int color) const;

    void add_log(const std::string& message, NoticeKind kind = NoticeKind::Routine);
    bool execute(const std::string& command);

    void scroll_view(int direction);
    void select_process(int direction);
    void toggle_process();
    void scroll_process_columns(int direction);

    std::string clock_text() const;
    std::string activity_bar(double fraction) const;

    static std::string number(double value, int precision = 2);
    static double gib(std::uint64_t bytes);
};

} // namespace ai_sim::ui
