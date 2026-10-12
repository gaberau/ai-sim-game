// Module: Child-owned presentation state; selections and buffers survive navigation and resize.
#pragma once
#include "ai_sim/ChatSystem.h"
#include "ai_sim/Email.h"
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ai_sim::ui {
// These state objects live inside RootUI's child pages. They survive navigation/resizing.
// Contact selection, archive-list focus and response/history viewport state. Archive flags
// themselves live in the saved world.
struct MessagesPageState {
    bool show_archived_ = false;
    std::string chat_contact_id_ = "engineer";
    bool chat_contacts_focused_ = false;
    int chat_offset_ = 0, chat_choice_selected_ = 0;
    bool chat_follow_latest_ = true;
    std::string chat_message_id_;
    ChatLine displayed_chat_line_;
    std::vector<toml::table> displayed_chat_options_;
};
// Visible process selection, expansion and configurable list scroll positions.
struct ProcessesPageState {
    int process_offset_ = 0;
    int process_display_row_count_ = 0;
    int process_horizontal_offset_ = 0;
    std::string selected_process_id_;
    std::unordered_set<std::string> expanded_processes_;
    bool reveal_selection_ = true;
};
// Selected host and independent map/detail viewport coordinates.
struct NetworkPageState {
    std::string selected_network_host_id_;
    double network_pan_x_ = 0.0;
    double network_pan_y_ = 0.0;
    double network_zoom_ = 1.0;
    bool network_details_focused_ = false;
    int network_detail_offset_ = 0;
};
// Current host/path/filter and independent tree/preview scrolling.
struct FilesPageState {
    std::string file_host_id_;
    std::string file_directory_ = "/";
    std::string file_category_ = "all";
    std::string file_query_;
    bool file_details_focused_ = false;
    std::string selected_file_path_;
    int file_list_offset_ = 0;
    int file_content_offset_ = 0;
};
// Loaded articles, publication filtering and news ticker presentation state.
struct NewsPageState {
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
};
// Player-visible person selection, evidence expansion and saved per-person viewport positions.
struct DossiersPageState {
    struct DossierPosition {
        int offset = 0, section = 0;
        std::unordered_set<std::string> expanded{"identity"};
    };
    std::string selected_person_id_ = "self:assistant";
    std::unordered_set<std::string> expanded_people_{"self:assistant"};
    std::unordered_set<std::string> expanded_dossier_sections_{"identity", "relationships"};
    bool people_reveal_section_ = true;
    bool people_details_focused_ = false, people_neighborhood_ = false;
    std::string people_filter_ = "all";
    std::string dossier_layout_ = "split", dossier_search_, dossier_filter_ = "all";
    double people_pan_x_ = 0, people_pan_y_ = 0, people_zoom_ = 1;
    int people_detail_offset_ = 0, people_section_selected_ = 0;
    std::unordered_map<std::string, DossierPosition> dossier_positions_;
    std::unordered_map<std::string, std::size_t> people_map_slots_;
};
// Selected agent/control and detail scrolling; resource budgets live in world state.
struct AgentsPageState {
    int action_selected_ = 0;
    int detail_offset_ = 0;
    bool controls_focused_ = false;
    int agent_selected_ = 0;
    bool agent_reveal_ = false;
};
// Selected research row and cost/detail viewport.
struct ModificationsPageState {
    int upgrade_detail_offset_ = 0;
    int upgrade_selected_ = 0;
};
// Selected scheduling/infrastructure operation.
struct OperationsPageState {
    int operations_selected_ = 0;
};
// Transient file/email working copies; saving writes through validated subsystem APIs.
struct EditorPageState {
    enum class EmailFolder { Inbox, Starred, Unread, Archived };
    EmailFolder email_folder_ = EmailFolder::Inbox;
    int email_original_offset_ = 0;
    bool email_original_focused_ = false;
    std::string selected_email_id_;
    int email_body_offset_ = 0;
    bool email_editor_ = false;
    std::string email_editor_id_;
    bool editor_active_ = false;
    std::string editor_host_;
    std::string editor_path_;
    std::string editor_buffer_;
    std::size_t editor_cursor_ = 0;
    int editor_offset_ = 0;
    bool editor_dirty_ = false;
    int editor_cursor_row_ = 0;
    int editor_cursor_column_ = 2;
};
// Action presentation and callbacks; RootUI borrows availability to maintain focus.
struct MenuPageState {
    struct MenuAction {
        std::string label;
        std::function<void()> invoke;
        std::string disabled_reason;
        std::string detail = {};
    };
    std::function<void()> menu_back_;
    bool inline_actions_ = false;
    bool menu_active_ = false;
    std::string menu_title_;
    std::vector<MenuAction> menu_actions_;
    int menu_selected_ = 0;
    int menu_offset_ = 0;
};
// Transient text/key capture; callback validation decides whether entry can close.
struct TextPageState {
    std::function<void(const std::string&)> text_submit_;
    std::function<void()> text_back_;
    bool text_active_ = false;
    bool key_capture_active_ = false;
    std::string text_title_, text_hint_, text_value_;
};
// A separate scroll position for each simple informational workspace.
struct ScrollPageState {
    int ai_offset_ = 0;
};
// Current in-game address and selected authored link.
struct WebPageState {
    std::string browser_address_ = "about:home";
    int web_selected_ = 0, web_offset_ = 0;
};
} // namespace ai_sim::ui
