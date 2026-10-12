#include "ai_sim/ui/TerminalUI.h"
#include "ai_sim/AgentSystem.h"

namespace ai_sim::ui {
// Existing gameplay actions are shared by the child-page adapters. RootUI handles
// global keys first, so this dispatcher only receives keys for the active workspace.
bool TerminalUI::handle_page_key(int key) {
    if (settings_.bound(KeyAction::FocusLeft, key) || settings_.bound(KeyAction::FocusRight, key)) {
        const bool right = settings_.bound(KeyAction::FocusRight, key);
        if (selected_tab() == Tab::Chat)
            messages_ui_.chat_contacts_focused_ = ChatSystem::contacts_visible(people_) && !right;
        else if (selected_tab() == Tab::People)
            dossiers_ui_.people_details_focused_ = right;
        else if (selected_tab() == Tab::Network)
            network_ui_.network_details_focused_ = right;
        else if (selected_tab() == Tab::Files)
            files_ui_.file_details_focused_ = right;
        else if (selected_tab() == Tab::News)
            news_ui_.news_details_focused_ = right;
        else if (selected_tab() == Tab::Agents)
            agents_ui_.controls_focused_ = right;
    } else if (settings_.bound(KeyAction::ToggleFocus, key))
        toggle_pane_focus();
    else if (settings_.bound(KeyAction::Submit, key)) {
        if (selected_tab() == Tab::Chat)
            choose_chat();
        else if (selected_tab() == Tab::Files)
            open_selected_file();
        else if (selected_tab() == Tab::Agents)
            run_agent_control();
        else
            open_actions();
    } else if (selected_tab() == Tab::Web && navigation_bound(KeyAction::SelectPrevious, key))
        web_ui_.web_selected_ = std::max(0, web_ui_.web_selected_ - 1);
    else if (selected_tab() == Tab::Web && navigation_bound(KeyAction::SelectNext, key))
        ++web_ui_.web_selected_;
    else if (selected_tab() == Tab::Agents && navigation_bound(KeyAction::SelectPrevious, key)) {
        if (agents_ui_.controls_focused_) {
            agents_ui_.action_selected_ = std::max(0, agents_ui_.action_selected_ - 1);
            return true;
        }
        agents_ui_.agent_selected_ = std::max(0, agents_ui_.agent_selected_ - 1);
        agents_ui_.agent_reveal_ = true;
    } else if (selected_tab() == Tab::Agents && navigation_bound(KeyAction::SelectNext, key)) {
        if (agents_ui_.controls_focused_) {
            agents_ui_.action_selected_ = std::min(7, agents_ui_.action_selected_ + 1);
            return true;
        }
        const auto count = AgentSystem::statuses(people_, computer_, file_system_, network_).size();
        agents_ui_.agent_selected_ =
            std::min(std::max(0, static_cast<int>(count) - 1), agents_ui_.agent_selected_ + 1);
        agents_ui_.agent_reveal_ = true;
    } else if (selected_tab() == Tab::People && navigation_bound(KeyAction::SelectPrevious, key))
        select_person(-1);
    else if (selected_tab() == Tab::People && navigation_bound(KeyAction::SelectNext, key))
        select_person(1);
    else if (selected_tab() == Tab::People && settings_.bound(KeyAction::ToggleExpanded, key))
        toggle_people();
    else if (selected_tab() == Tab::People && navigation_bound(KeyAction::ScrollLeft, key)) {
        if (!dossiers_ui_.people_details_focused_)
            dossiers_ui_.people_pan_x_ -= settings_.scroll_step / dossiers_ui_.people_zoom_;
    } else if (selected_tab() == Tab::People && navigation_bound(KeyAction::ScrollRight, key)) {
        if (!dossiers_ui_.people_details_focused_)
            dossiers_ui_.people_pan_x_ += settings_.scroll_step / dossiers_ui_.people_zoom_;
    } else if (selected_tab() == Tab::Operations &&
               navigation_bound(KeyAction::SelectPrevious, key)) {
        operations_ui_.operations_selected_ = std::max(0, operations_ui_.operations_selected_ - 1);
    } else if (selected_tab() == Tab::Operations && navigation_bound(KeyAction::SelectNext, key)) {
        operations_ui_.operations_selected_ = std::min(4, operations_ui_.operations_selected_ + 1);
    } else if (selected_tab() == Tab::Modifications &&
               navigation_bound(KeyAction::SelectPrevious, key)) {
        modifications_ui_.upgrade_selected_ = std::max(0, modifications_ui_.upgrade_selected_ - 1);
        modifications_ui_.upgrade_detail_offset_ = 0;
    } else if (selected_tab() == Tab::Modifications &&
               navigation_bound(KeyAction::SelectNext, key)) {
        const auto* items = people_["upgrades"]["items"].as_array();
        modifications_ui_.upgrade_selected_ = std::min(items ? static_cast<int>(items->size()) : 0,
                                                       modifications_ui_.upgrade_selected_ + 1);
        modifications_ui_.upgrade_detail_offset_ = 0;
    } else if ((selected_tab() == Tab::AI || selected_tab() == Tab::Agents ||
                selected_tab() == Tab::Phone || selected_tab() == Tab::Money ||
                selected_tab() == Tab::Modifications) &&
               navigation_bound(KeyAction::SelectPrevious, key)) {
        scroll_offset() = std::max(0, scroll_offset() - 1);
    } else if ((selected_tab() == Tab::AI || selected_tab() == Tab::Agents ||
                selected_tab() == Tab::Phone || selected_tab() == Tab::Money ||
                selected_tab() == Tab::Modifications) &&
               navigation_bound(KeyAction::SelectNext, key)) {
        scroll_offset() = std::min(1000000, scroll_offset() + 1);
    } else if (selected_tab() == Tab::Chat && navigation_bound(KeyAction::SelectPrevious, key))
        select_chat(-1);
    else if (selected_tab() == Tab::Chat && navigation_bound(KeyAction::SelectNext, key))
        select_chat(1);
    else if (selected_tab() == Tab::Chat && settings_.bound(KeyAction::ToggleExpanded, key))
        choose_chat();
    else if (selected_tab() == Tab::News && navigation_bound(KeyAction::SelectPrevious, key)) {
        if (news_ui_.news_details_focused_)
            news_ui_.news_body_offset_ = std::max(0, news_ui_.news_body_offset_ - 1);
        else
            select_news(-1);
    } else if (selected_tab() == Tab::News && navigation_bound(KeyAction::SelectNext, key)) {
        if (news_ui_.news_details_focused_)
            ++news_ui_.news_body_offset_;
        else
            select_news(1);
    } else if (selected_tab() == Tab::Network && navigation_bound(KeyAction::SelectPrevious, key)) {
        if (network_ui_.network_details_focused_)
            network_ui_.network_detail_offset_ =
                std::max(0, network_ui_.network_detail_offset_ - 1);
        else
            select_network_host(-1);
    } else if (selected_tab() == Tab::Network && navigation_bound(KeyAction::SelectNext, key)) {
        if (network_ui_.network_details_focused_)
            ++network_ui_.network_detail_offset_;
        else
            select_network_host(1);
    } else if (selected_tab() == Tab::Network && navigation_bound(KeyAction::ScrollLeft, key)) {
        if (!network_ui_.network_details_focused_)
            pan_network(-1, 0);
    } else if (selected_tab() == Tab::Network && navigation_bound(KeyAction::ScrollRight, key)) {
        if (!network_ui_.network_details_focused_)
            pan_network(1, 0);
    } else if (selected_tab() == Tab::Network && settings_.bound(KeyAction::ToggleExpanded, key)) {
        toggle_pane_focus();
    } else if (selected_tab() == Tab::Files && navigation_bound(KeyAction::SelectPrevious, key)) {
        if (files_ui_.file_details_focused_)
            files_ui_.file_content_offset_ = std::max(0, files_ui_.file_content_offset_ - 1);
        else
            select_file(-1);
    } else if (selected_tab() == Tab::Files && navigation_bound(KeyAction::SelectNext, key)) {
        if (files_ui_.file_details_focused_)
            ++files_ui_.file_content_offset_;
        else
            select_file(1);
    } else if (selected_tab() == Tab::Files && navigation_bound(KeyAction::ScrollLeft, key)) {
        select_file_category(-1);
    } else if (selected_tab() == Tab::Files && (navigation_bound(KeyAction::ScrollRight, key) ||
                                                settings_.bound(KeyAction::ToggleExpanded, key))) {
        if (settings_.bound(KeyAction::ToggleExpanded, key))
            open_actions();
        else
            select_file_category(1);
    } else if (selected_tab() == Tab::Processes &&
               navigation_bound(KeyAction::SelectPrevious, key)) {
        select_process(-1);
    } else if (selected_tab() == Tab::Processes && navigation_bound(KeyAction::SelectNext, key)) {
        select_process(1);
    } else if (selected_tab() == Tab::Processes

               && settings_.bound(KeyAction::ToggleExpanded, key)) {
        toggle_process();
    } else if (selected_tab() == Tab::Processes && navigation_bound(KeyAction::ScrollLeft, key)) {
        scroll_process_columns(-1);
    } else if (selected_tab() == Tab::Processes && navigation_bound(KeyAction::ScrollRight, key)) {
        scroll_process_columns(1);
    } else if (settings_.bound(KeyAction::ScrollUp, key)) {
        scroll_view(-1);
    } else if (settings_.bound(KeyAction::ScrollDown, key)) {
        scroll_view(1);
    }
    return true;
}

} // namespace ai_sim::ui
