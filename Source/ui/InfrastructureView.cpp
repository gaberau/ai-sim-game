// Module: Present city facilities, dependency chains and currently authored access options.
#include "ai_sim/ui/TerminalUI.h"
#include "ai_sim/InfrastructureSystem.h"
#include <algorithm>
#include <stdexcept>
namespace ai_sim::ui {
std::string TerminalUI::infrastructure_url() const {
    return people_["infrastructure"]["website_url"].value_or(
        std::string{"https://city.sim/infrastructure"});
}
void TerminalUI::open_infrastructure() {
    web_ui_.browser_address_ = infrastructure_url();
    web_ui_.web_selected_ = web_ui_.web_offset_ = 0;
    select_tab(Tab::Web);
    menu_ui_.menu_active_ = text_ui_.text_active_ = false;
}
void TerminalUI::open_infrastructure_sector(const std::string& sector) {
    std::vector<MenuAction> actions;
    for (const auto& facility : InfrastructureSystem::facilities(people_))
        if (facility.sector == sector)
            actions.push_back({facility.name + " | " + facility.status + " | " +
                                   std::to_string(facility.capacity_percent) + "%",
                               [this, id = facility.id] {
                                   open_infrastructure_facility(id);
                               },
                               {},
                               facility.purpose});
    open_menu("City infrastructure | " + sector, std::move(actions), [this] {
        open_infrastructure();
    });
}
void TerminalUI::open_infrastructure_facility(const std::string& id) {
    const auto facilities = InfrastructureSystem::facilities(people_);
    const auto found =
        std::find_if(facilities.begin(), facilities.end(), [&](const auto& facility) {
            return facility.id == id;
        });
    if (found == facilities.end())
        throw std::runtime_error("Unknown facility");
    const auto facility = *found;
    std::string description =
        "Operator: " + facility.operator_name + ". " + facility.purpose +
        " Status: " + facility.status + "; capacity " + std::to_string(facility.capacity_percent) +
        "%. Boundary: " + facility.security +
        " Operations remain inaccessible. Future infrastructure access requires a story unlock.";
    description += " Depends on: ";
    const auto dependencies = InfrastructureSystem::dependencies(people_, id);
    for (const auto& dependency : dependencies)
        description += dependency + "; ";
    if (dependencies.empty())
        description += "no listed suppliers. ";
    description += "Downstream dependencies: ";
    const auto affected = InfrastructureSystem::affected_facilities(people_, id);
    for (const auto& affected_id : affected) {
        const auto named =
            std::find_if(facilities.begin(), facilities.end(), [&](const auto& entry) {
                return entry.id == affected_id;
            });
        if (named != facilities.end())
            description += named->name + "; ";
    }
    if (affected.empty())
        description += "none listed.";
    open_menu(facility.name,
              {{"Facility, dependencies, and access boundaries",
                [this, facility, description] {
                    show_message(facility.name, description);
                    menu_ui_.menu_back_ = [this, id = facility.id] {
                        open_infrastructure_facility(id);
                    };
                },
                {}},
               {facility.discovered ? "Public gateway already discovered"
                                    : "Discover public gateway on network map",
                [this, id] {
                    InfrastructureSystem::discover(people_, network_, id);
                    ChatSystem::reveal_tab(people_, "network");
                    add_log("Public gateway discovered. Operations access remains restricted.",
                            NoticeKind::Success);
                    open_infrastructure_facility(id);
                },
                facility.discovered ? "This facility's public gateway is already on your map." : "",
                "Reveals the public gateway and its WAN link; does not grant credentials or "
                "control access."}},
              [this, sector = facility.sector] {
                  open_infrastructure_sector(sector);
              });
}
} // namespace ai_sim::ui
