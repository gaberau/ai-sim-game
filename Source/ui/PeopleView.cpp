#include "ai_sim/ui/TerminalUI.h"
#include "ai_sim/ui/ScreenRenderer.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <stdexcept>
#include <cctype>

namespace ai_sim::ui {
namespace {
const std::vector<std::string> sections{"identity",     "employment",   "motivations",
                                        "values",       "ai_attitudes", "relationships",
                                        "observations", "unknowns"};
std::string text(const toml::table& t, const char* key) {
    return t[key].value_or(std::string{});
}
std::string fit(const std::string& s, int width) {
    if (width < 1)
        return {};
    if (s.size() <= static_cast<std::size_t>(width))
        return s;
    return s.substr(0, std::max(0, width - 1)) + "~";
}
} // namespace
std::vector<std::string> TerminalUI::dossier_people() const {
    auto lower = [](std::string value) {
        for (auto& ch : value)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return value;
    };
    std::vector<std::string> result;
    for (const auto& id : PeopleSystem::known_ids(people_)) {
        const auto* dossier = PeopleSystem::dossier(people_, id);
        std::string searchable = PeopleSystem::name(people_, id);
        bool uncertain = false;
        double latest = 0;
        if (dossier)
            if (const auto* facts = (*dossier)["facts"].as_array())
                for (const auto& row : *facts) {
                    const auto& f = *row.as_table();
                    searchable += " " + text(f, "value");
                    latest = std::max(latest, f["last_seen_seconds"].value_or(0.0));
                    uncertain |=
                        f["confidence"].value_or(1.0) <
                            people_["upgrades"]["dossiers"]["uncertain_confidence"].value_or(.85) ||
                        text(f, "claim_type") == "inferred" ||
                        text(f, "claim_type") == "unconfirmed";
                }
        if (!dossiers_ui_.dossier_search_.empty() &&
            lower(searchable).find(lower(dossiers_ui_.dossier_search_)) == std::string::npos)
            continue;
        if (dossiers_ui_.dossier_filter_ == "uncertain" && !uncertain)
            continue;
        if (dossiers_ui_.dossier_filter_ == "recent" &&
            computer_.time_seconds() - latest >
                people_["upgrades"]["dossiers"]["recent_window_seconds"].value_or(3600.0))
            continue;
        result.push_back(id);
    }
    return result;
}
std::vector<std::string> TerminalUI::visible_people() const {
    const auto all = dossier_people();
    if (!dossiers_ui_.people_neighborhood_)
        return all;
    std::unordered_set<std::string> visible{dossiers_ui_.selected_person_id_};
    std::vector<std::string> frontier{dossiers_ui_.selected_person_id_};
    if (auto* edges = people_["known_relationships"].as_array())
        while (!frontier.empty()) {
            const auto id = frontier.back();
            frontier.pop_back();
            if (!dossiers_ui_.expanded_people_.contains(id))
                continue;
            for (const auto& n : *edges) {
                const auto& r = *n.as_table();
                if (dossiers_ui_.people_filter_ != "all" &&
                    text(r, "type") != dossiers_ui_.people_filter_)
                    continue;
                const auto a = text(r, "from"), b = text(r, "to");
                if (a != id && b != id)
                    continue;
                const auto other = a == id ? b : a;
                if (visible.insert(other).second)
                    frontier.push_back(other);
            }
        }
    std::vector<std::string> result;
    for (const auto& id : all)
        if (visible.contains(id))
            result.push_back(id);
    return result;
}
void TerminalUI::change_person(const std::string& id) {
    if (id == dossiers_ui_.selected_person_id_)
        return;
    if (!people_.contains("dossier_seen"))
        people_.insert("dossier_seen", toml::table{});
    people_["dossier_seen"].as_table()->insert_or_assign(dossiers_ui_.selected_person_id_,
                                                         computer_.time_seconds());
    dossiers_ui_.dossier_positions_[dossiers_ui_.selected_person_id_] = {
        dossiers_ui_.people_detail_offset_, dossiers_ui_.people_section_selected_,
        dossiers_ui_.expanded_dossier_sections_};
    dossiers_ui_.selected_person_id_ = id;
    const auto found = dossiers_ui_.dossier_positions_.find(id);
    const DossierPosition saved =
        found == dossiers_ui_.dossier_positions_.end() ? DossierPosition{} : found->second;
    dossiers_ui_.people_detail_offset_ = saved.offset;
    dossiers_ui_.people_section_selected_ = saved.section;
    dossiers_ui_.expanded_dossier_sections_ = saved.expanded;
    dossiers_ui_.people_reveal_section_ = false;
    dossiers_ui_.people_pan_x_ = dossiers_ui_.people_pan_y_ = 0;
}
void TerminalUI::select_person(int direction) {
    if (dossiers_ui_.people_details_focused_) {
        dossiers_ui_.people_reveal_section_ = true;
        dossiers_ui_.people_section_selected_ =
            std::clamp(dossiers_ui_.people_section_selected_ + direction, 0,
                       static_cast<int>(sections.size()) - 1);
        return;
    }
    const auto ids = dossier_people();
    if (ids.empty())
        return;
    const auto found = std::find(ids.begin(), ids.end(), dossiers_ui_.selected_person_id_);
    const int current = found == ids.end() ? 0 : static_cast<int>(found - ids.begin());
    change_person(ids[std::clamp(current + direction, 0, static_cast<int>(ids.size()) - 1)]);
}
void TerminalUI::toggle_people() {
    if (dossiers_ui_.people_details_focused_) {
        dossiers_ui_.people_reveal_section_ = true;
        const auto& section = sections[dossiers_ui_.people_section_selected_];
        if (!dossiers_ui_.expanded_dossier_sections_.erase(section))
            dossiers_ui_.expanded_dossier_sections_.insert(section);
    } else {
        if (!dossiers_ui_.expanded_people_.erase(dossiers_ui_.selected_person_id_))
            dossiers_ui_.expanded_people_.insert(dossiers_ui_.selected_person_id_);
        // Expanding a node selects the neighborhood mode so the change is visible.
        dossiers_ui_.people_neighborhood_ = true;
    }
}
void TerminalUI::open_observation(const std::string& id) {
    const toml::table* observation = nullptr;
    for (const auto& n : *people_["observations"].as_array())
        if (text(*n.as_table(), "id") == id)
            observation = n.as_table();
    if (!observation)
        throw std::runtime_error("Supporting observation is unavailable");
    const auto record = *observation;
    std::string email_id;
    if (id.starts_with("email:"))
        email_id = id.substr(6);
    else if (text(record, "source").starts_with("Reply to "))
        email_id = text(record, "source").substr(9);
    else if (text(record, "source").starts_with("email:"))
        email_id = text(record, "source").substr(6);
    open_menu("Evidence | " + text(record, "source"),
              {{"Read recorded observation",
                [this, record] {
                    show_message("Recorded observation",
                                 text(record, "source") + " | +" +
                                     number(record["seconds"].value_or(0.0), 0) + " s | " +
                                     text(record, "direction") + " | " + text(record, "text"));
                    menu_ui_.menu_back_ = [this, id = text(record, "id")] {
                        open_observation(id);
                    };
                },
                {}},
               {"Open supporting conversation",
                [this, email_id, record] {
                    if (!email_id.empty())
                        open_chat_request(email_id);
                    else {
                        messages_ui_.chat_contact_id_ = text(record, "person_id");
                        select_tab(Tab::Chat);
                        messages_ui_.chat_message_id_ = text(record, "id");
                        messages_ui_.chat_follow_latest_ = false;
                        if (const auto* transcript = people_["chat"]["transcript"].as_array())
                            for (std::size_t i = 0; i < transcript->size(); ++i)
                                if (messages_ui_.chat_message_id_ ==
                                    "chat:" + std::to_string(i + 1))
                                    messages_ui_.chat_contact_id_ =
                                        (*(*transcript)[i].as_table())["contact_id"].value_or(
                                            messages_ui_.chat_contact_id_);
                    }
                },
                email_id.empty() && !text(record, "id").starts_with("chat:")
                    ? "This observation has no linked message."
                    : ""}},
              [this] {
                  open_people_evidence();
              });
}
void TerminalUI::open_people_evidence() {
    std::vector<MenuAction> choices;
    if (const auto* d = PeopleSystem::dossier(people_, dossiers_ui_.selected_person_id_))
        for (const auto& n : *(*d)["facts"].as_array()) {
            const auto fact = *n.as_table();
            choices.push_back(
                {text(fact, "category") + " | " + text(fact, "field") + ": " + text(fact, "value"),
                 [this, fact] {
                     std::vector<MenuAction> supporting;
                     for (const auto& e : *fact["evidence_ids"].as_array()) {
                         const auto id = e.value_or(std::string{});
                         for (const auto& n : *people_["observations"].as_array())
                             if (text(*n.as_table(), "id") == id)
                                 supporting.push_back(
                                     {text(*n.as_table(), "source") + " | +" +
                                          number((*n.as_table())["seconds"].value_or(0.0), 0) +
                                          " s",
                                      [this, id] {
                                          open_observation(id);
                                      },
                                      {}});
                     }
                     open_menu(text(fact, "claim_type") + " | confidence " +
                                   number(fact["confidence"].value_or(1.0), 2),
                               std::move(supporting), [this] {
                                   open_people_evidence();
                               });
                 },
                 {}});
        }
    choices.push_back({"All recorded interactions...",
                       [this] {
                           std::vector<MenuAction> items;
                           for (const auto& n : *people_["observations"].as_array()) {
                               const auto& e = *n.as_table();
                               if (text(e, "person_id") != dossiers_ui_.selected_person_id_)
                                   continue;
                               const auto id = text(e, "id");
                               items.push_back({text(e, "direction") + " | " + text(e, "source"),
                                                [this, id] {
                                                    open_observation(id);
                                                },
                                                {}});
                           }
                           if (items.empty())
                               items.push_back({"No recorded interactions",
                                                {},
                                                "No supporting observations yet."});
                           open_menu("Recorded interactions", std::move(items), [this] {
                               open_people_evidence();
                           });
                       },
                       {}});
    open_menu("Dossier evidence", std::move(choices), [this] {
        open_people_actions();
    });
}
void TerminalUI::open_people_connections() {
    std::vector<MenuAction> choices;
    if (auto* edges = people_["known_relationships"].as_array())
        for (const auto& n : *edges) {
            const auto r = *n.as_table();
            const auto from = text(r, "from"), to = text(r, "to");
            if (from != dossiers_ui_.selected_person_id_ && to != dossiers_ui_.selected_person_id_)
                continue;
            const auto other = from == dossiers_ui_.selected_person_id_ ? to : from;
            const auto label = PeopleSystem::name(people_, from) + " -> " +
                               PeopleSystem::name(people_, to) + ": " + text(r, "label");
            choices.push_back(
                {label,
                 [this, r, other, label] {
                     open_menu("Relationship",
                               {{"Read relationship summary",
                                 [this, r, label] {
                                     show_message(
                                         "Relationship summary",
                                         label + ". Type: " + text(r, "type") + ". Confidence: " +
                                             number(r["confidence"].value_or(1.0), 2) +
                                             ". Last recorded interaction: +" +
                                             number(r["last_seen_seconds"].value_or(0.0), 0) +
                                             " working seconds. Evidence records: " +
                                             std::to_string(r["evidence_ids"].as_array()->size()) +
                                             ". Direction does not imply reciprocal trust.");
                                     menu_ui_.menu_back_ = [this] {
                                         open_people_connections();
                                     };
                                 },
                                 {}},
                                {"Open connected person's dossier",
                                 [this, other] {
                                     change_person(other);
                                 },
                                 {}},
                                {"Show relationship evidence",
                                 [this, r] {
                                     std::vector<MenuAction> items;
                                     for (const auto& e : *r["evidence_ids"].as_array()) {
                                         const auto id = e.value_or(std::string{});
                                         for (const auto& n : *people_["observations"].as_array())
                                             if (text(*n.as_table(), "id") == id)
                                                 items.push_back({text(*n.as_table(), "source"),
                                                                  [this, id] {
                                                                      open_observation(id);
                                                                  },
                                                                  {}});
                                     }
                                     open_menu("Relationship evidence", std::move(items), [this] {
                                         open_people_connections();
                                     });
                                 },
                                 {}}},
                               [this] {
                                   open_people_connections();
                               });
                 },
                 {}});
        }
    if (choices.empty())
        choices.push_back(
            {"No discovered relationships", {}, "Undiscovered relationships remain hidden."});
    open_menu("Connections", std::move(choices), [this] {
        open_people_actions();
    });
}
void TerminalUI::open_people_actions() {
    std::vector<MenuAction> choices{
        {"View layout: " + dossiers_ui_.dossier_layout_,
         [this] {
             std::vector<MenuAction> layouts;
             for (const auto& layout : {"split", "map", "dossier"})
                 layouts.push_back({layout,
                                    [this, layout] {
                                        dossiers_ui_.dossier_layout_ = layout;
                                        dossiers_ui_.people_details_focused_ =
                                            dossiers_ui_.dossier_layout_ == "dossier";
                                        dossiers_ui_.people_detail_offset_ = 0;
                                    },
                                    {}});
             open_menu("Dossier layout", std::move(layouts), [this] {
                 open_people_actions();
             });
         },
         {}},
        {"Search people",
         [this] {
             open_text(
                 "Search dossiers",
                 "Search known names, roles and recorded facts; blank clears the search.",
                 [this](const std::string& query) {
                     dossiers_ui_.dossier_search_ = query;
                     const auto ids = dossier_people();
                     if (!ids.empty())
                         change_person(ids.front());
                 },
                 dossiers_ui_.dossier_search_,
                 [this] {
                     open_people_actions();
                 });
         },
         {}},
        {"Filter people: " + dossiers_ui_.dossier_filter_,
         [this] {
             std::vector<MenuAction> filters;
             for (const auto& filter : {"all", "recent", "uncertain"})
                 filters.push_back({filter,
                                    [this, filter] {
                                        dossiers_ui_.dossier_filter_ = filter;
                                        const auto ids = dossier_people();
                                        if (!ids.empty())
                                            change_person(ids.front());
                                    },
                                    {}});
             open_menu("Dossier filter", std::move(filters), [this] {
                 open_people_actions();
             });
         },
         {}},
        {"Map legend / help",
         [this] {
             show_message(
                 "People map legend",
                 "Cyan lines connect the selected person; gray lines connect other people. Yellow "
                 "lines indicate uncertain relationships. A dot marks a moved label's original "
                 "position. Bold labels mark selection. Open Connections and interactions to "
                 "inspect relationship types, confidence and supporting evidence.");
             menu_ui_.menu_back_ = [this] {
                 open_people_actions();
             };
         },
         {}},
        {dossiers_ui_.people_details_focused_ ? "Focus relationship map" : "Focus dossier sections",
         [this] {
             toggle_pane_focus();
         },
         {}},
        {"Expand / collapse selected " +
             std::string(dossiers_ui_.people_details_focused_ ? "section" : "person"),
         [this] {
             toggle_people();
         },
         {}},
        {"Dossier sections...",
         [this] {
             std::vector<MenuAction> items;
             for (const auto& s : sections)
                 items.push_back(
                     {std::string(dossiers_ui_.expanded_dossier_sections_.contains(s) ? "Collapse "
                                                                                      : "Expand ") +
                          s,
                      [this, s] {
                          if (!dossiers_ui_.expanded_dossier_sections_.erase(s))
                              dossiers_ui_.expanded_dossier_sections_.insert(s);
                          dossiers_ui_.people_details_focused_ = true;
                          dossiers_ui_.people_section_selected_ = static_cast<int>(
                              std::find(sections.begin(), sections.end(), s) - sections.begin());
                          dossiers_ui_.people_reveal_section_ = true;
                          dossiers_ui_.people_detail_offset_ = 0;
                      },
                      {}});
             items.push_back({"Expand all",
                              [this] {
                                  dossiers_ui_.expanded_dossier_sections_.insert(sections.begin(),
                                                                                 sections.end());
                                  dossiers_ui_.people_details_focused_ = true;
                                  dossiers_ui_.people_detail_offset_ = 0;
                              },
                              {}});
             items.push_back({"Collapse all",
                              [this] {
                                  dossiers_ui_.expanded_dossier_sections_.clear();
                                  dossiers_ui_.people_detail_offset_ = 0;
                              },
                              {}});
             open_menu("Dossier sections", std::move(items), [this] {
                 open_people_actions();
             });
         },
         {}},
        {"Dossier evidence...",
         [this] {
             open_people_evidence();
         },
         {}},
        {"Connections and interactions...",
         [this] {
             open_people_connections();
         },
         {}},
        {"Filter connections...",
         [this] {
             std::vector<MenuAction> items;
             for (const auto& filter : {"all", "communication", "organization", "personal"})
                 items.push_back({filter,
                                  [this, filter] {
                                      dossiers_ui_.people_filter_ = filter;
                                  },
                                  {}});
             open_menu("Connection filter", std::move(items), [this] {
                 open_people_actions();
             });
         },
         {}},
        {dossiers_ui_.people_neighborhood_ ? "Show all known people" : "Show expanded neighborhood",
         [this] {
             dossiers_ui_.people_neighborhood_ = !dossiers_ui_.people_neighborhood_;
             dossiers_ui_.people_pan_x_ = dossiers_ui_.people_pan_y_ = 0;
         },
         {}},
        {"Center map",
         [this] {
             dossiers_ui_.people_pan_x_ = dossiers_ui_.people_pan_y_ = 0;
         },
         {}},
        {"Zoom in",
         [this] {
             dossiers_ui_.people_zoom_ = std::min(4.0, dossiers_ui_.people_zoom_ * 1.25);
         },
         {}},
        {"Zoom out",
         [this] {
             dossiers_ui_.people_zoom_ = std::max(.5, dossiers_ui_.people_zoom_ / 1.25);
         },
         {}},
        {"Add missing people definitions",
         [this] {
             PeopleSystem::install(people_, definitions_.people.parent_path() / "social.toml");
             PeopleSystem::observe(people_, emails_, computer_.time_seconds());
             add_log(
                 "Missing person definitions and introductions added; existing state retained.");
         },
         {}}};
    if (auto* list = people_["observations"].as_array())
        for (std::size_t i = list->size(); i > 0; --i) {
            const auto& e = *(*list)[i - 1].as_table();
            const auto source = text(e, "id");
            if (text(e, "person_id") != dossiers_ui_.selected_person_id_ ||
                !source.starts_with("email:"))
                continue;
            const auto id = source.substr(6);
            choices.insert(choices.begin(), {"Open latest conversation",
                                             [this, id] {
                                                 open_chat_request(id);
                                             },
                                             {}});
            break;
        }
    open_menu("Dossier | " + PeopleSystem::name(people_, dossiers_ui_.selected_person_id_),
              std::move(choices));
}
void TerminalUI::draw_people(int top, int bottom, int width) {
    const auto known = dossier_people();
    if (known.empty()) {
        write(top, 1, "DOSSIERS | No matching people. Enter to change search or filters.", Heading);
        return;
    }
    if (std::find(known.begin(), known.end(), dossiers_ui_.selected_person_id_) == known.end())
        change_person(known.front());
    const auto ids = visible_people();
    const int panel = std::min(width - 20, width >= 110 ? settings_.people_wide_panel_width
                                                        : settings_.people_panel_width),
              divider = dossiers_ui_.dossier_layout_ == "map" ? width - 1 : width - panel - 1;
    if (dossiers_ui_.dossier_layout_ != "map" && dossiers_ui_.dossier_layout_ != "dossier")
        renderer_->split(top, bottom - 1, divider);
    const int left = 1, right = divider - 2, map_top = top + 2, map_bottom = bottom - 3;
    if (dossiers_ui_.dossier_layout_ != "dossier") {
        write(top, 1,
              "DOSSIERS | " + std::to_string(known.size()) + " known | " +
                  dossiers_ui_.people_filter_,
              dossiers_ui_.people_details_focused_ ? Heading : ActiveTab, true);
        write(top + 1, 1,
              fit(std::string(dossiers_ui_.people_details_focused_ ? "Focus: dossier | "
                                                                   : "Focus: map | ") +
                      (settings_.binding_label(KeyAction::FocusLeft) + "/" +
                       settings_.binding_label(KeyAction::FocusRight)) +
                      ": switch",
                  right),
              Footer);
        for (int y = top; y < bottom; ++y)
            line_cell(y, divider, '|', Border);
        const double cx = (left + right) / 2.0, cy = (map_top + map_bottom) / 2.0;
        std::map<std::string, std::pair<double, double>> positions;
        std::vector<std::string> others;
        // Use the full known-person order, independent of selection and filtering.
        // Fixed slots also keep existing nodes in place as new people are discovered.
        for (const auto& id : known)
            if (id != "self:assistant")
                others.push_back(id);
        positions["self:assistant"] = {cx - dossiers_ui_.people_pan_x_ * dossiers_ui_.people_zoom_,
                                       cy - dossiers_ui_.people_pan_y_ * dossiers_ui_.people_zoom_};
        const double rx = std::max(8.0, (right - left) * .35),
                     ry = std::max(3.0, (map_bottom - map_top) * .38);
        for (std::size_t i = 0; i < others.size(); ++i) {
            const auto [slot, inserted] = dossiers_ui_.people_map_slots_.try_emplace(
                others[i], dossiers_ui_.people_map_slots_.size());
            (void)inserted;
            const double angle = 6.283185307 * (slot->second % 12) / 12;
            const double ring = 1.0 + static_cast<double>(slot->second / 12) * .7;
            positions[others[i]] = {
                cx + (rx * ring * std::cos(angle) - dossiers_ui_.people_pan_x_) *
                         dossiers_ui_.people_zoom_,
                cy + (ry * ring * std::sin(angle) - dossiers_ui_.people_pan_y_) *
                         dossiers_ui_.people_zoom_};
        }
        for (auto it = positions.begin(); it != positions.end();)
            if (std::find(ids.begin(), ids.end(), it->first) == ids.end())
                it = positions.erase(it);
            else
                ++it;
        auto plot = [&](double x, double y, char c, int color) {
            const int px = static_cast<int>(std::lround(x)), py = static_cast<int>(std::lround(y));
            if (px >= left && px <= right && py >= map_top && py <= map_bottom)
                line_cell(py, px, c, color);
        };
        if (auto* edges = people_["known_relationships"].as_array())
            for (const auto& n : *edges) {
                const auto& e = *n.as_table();
                const auto from = text(e, "from"), to = text(e, "to");
                if (dossiers_ui_.people_filter_ != "all" &&
                    text(e, "type") != dossiers_ui_.people_filter_)
                    continue;
                if (!positions.contains(from) || !positions.contains(to))
                    continue;
                const auto [x0, y0] = positions[from];
                const auto [x1, y1] = positions[to];
                const double confidence = e["confidence"].value_or(1.0);
                const int steps =
                    std::min(1000, std::max(1, static_cast<int>(std::ceil(std::max(
                                                   std::abs(x1 - x0), std::abs(y1 - y0))))));
                const bool recent =
                    text(e, "type") == "communication" &&
                    computer_.time_seconds() - e["last_seen_seconds"].value_or(0.0) < 90;
                const bool selected_connection = from == dossiers_ui_.selected_person_id_ ||
                                                 to == dossiers_ui_.selected_person_id_;
                const int color = confidence < settings_.uncertain_relationship_threshold
                                      ? MapLocked
                                  : selected_connection ? Heading
                                                        : Footer;
                const char c = std::abs(y1 - y0) < 1        ? '-'
                               : std::abs(x1 - x0) < 1      ? '|'
                               : (x1 - x0) * (y1 - y0) >= 0 ? '\\'
                                                            : '/';
                for (int i = 0; i <= steps; ++i)
                    plot(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps, c, color);
                const double f = recent
                                     ? .2 + .6 * std::fmod((computer_.time_seconds() -
                                                            e["last_seen_seconds"].value_or(0.0)) /
                                                               6.0,
                                                           1.0)
                                     : .65;
                plot(x0 + (x1 - x0) * f, y0 + (y1 - y0) * f,
                     std::abs(x1 - x0) >= std::abs(y1 - y0) ? (x1 >= x0 ? '>' : '<')
                                                            : (y1 >= y0 ? 'v' : '^'),
                     color);
            }
        struct Label {
            int x, y, width;
        };
        std::vector<Label> labels;
        std::vector<std::string> draw_order{dossiers_ui_.selected_person_id_};
        for (const auto& id : ids)
            if (id != dossiers_ui_.selected_person_id_)
                draw_order.push_back(id);
        int hidden_labels = 0;
        for (const auto& id : draw_order) {
            const auto [x, y] = positions[id];
            const bool selected = id == dossiers_ui_.selected_person_id_;
            const int px = std::clamp(static_cast<int>(std::lround(x)), left, right);
            const int py = std::clamp(static_cast<int>(std::lround(y)), map_top, map_bottom);
            if (!selected && (x < left || x > right || y < map_top || y > map_bottom)) {
                ++hidden_labels;
                continue;
            }
            const auto full =
                std::string(dossiers_ui_.expanded_people_.contains(id) ? "[-] " : "[+] ") +
                PeopleSystem::name(people_, id);
            const auto label = fit(full, std::min(23, right - left + 1));
            const int lx = std::clamp(px - static_cast<int>(label.size()) / 2, left,
                                      right - static_cast<int>(label.size()) + 1);
            bool drawn = false;
            for (int distance = 0; distance <= map_bottom - map_top && !drawn; ++distance) {
                for (int sign : {1, -1}) {
                    const int ly = py + distance * sign;
                    if (ly < map_top || ly > map_bottom)
                        continue;
                    const bool collision =
                        std::any_of(labels.begin(), labels.end(), [&](const Label& l) {
                            return l.y == ly && lx < l.x + l.width + 1 &&
                                   lx + static_cast<int>(label.size()) + 1 > l.x;
                        });
                    if (collision)
                        continue;
                    // Keep labels away from each other; a dot marks displaced node anchors.
                    if (ly != py)
                        plot(x, y, '.', Border);
                    write(ly, lx, label,
                          selected                 ? ActiveTab
                          : id == "self:assistant" ? Player
                                                   : Heading,
                          selected);
                    labels.push_back({lx, ly, static_cast<int>(label.size())});
                    drawn = true;
                    break;
                }
            }
            if (!drawn)
                ++hidden_labels;
        }
        if (hidden_labels)
            write(top, 1,
                  fit("DOSSIERS | " + std::to_string(known.size()) + " known | " +
                          std::to_string(hidden_labels) + " hidden labels",
                      right),
                  dossiers_ui_.people_details_focused_ ? Heading : ActiveTab, true);
        write(bottom - 2, left, "Selected connections", Heading);
        write(bottom - 1, left, fit("Yellow: uncertain | . moved label", right - left + 1),
              MapLocked);
    }
    if (dossiers_ui_.dossier_layout_ == "map") {
        write(bottom, 1, "Up/Down: person | Enter: dossier actions and view layouts", Footer);
        return;
    }
    const int detail_x = dossiers_ui_.dossier_layout_ == "dossier" ? 1 : divider + 2,
              detail_width = width - detail_x - 1;
    write(top, detail_x,
          fit(PeopleSystem::name(people_, dossiers_ui_.selected_person_id_), detail_width),
          dossiers_ui_.people_details_focused_ ? ActiveTab : Heading, true);
    std::vector<std::pair<std::string, int>> lines;
    auto add = [&](const std::string& s, int color = Plain) {
        if (s.empty()) {
            lines.emplace_back("", color);
            return;
        }
        std::istringstream input(s);
        std::string word, row;
        const auto limit = static_cast<std::size_t>(std::max(1, detail_width - 2));
        while (input >> word) {
            if (!row.empty() && row.size() + word.size() + 1 > limit) {
                lines.emplace_back("  " + row, color);
                row.clear();
            }
            while (word.size() > limit) {
                lines.emplace_back("  " + word.substr(0, limit), color);
                word.erase(0, limit);
            }
            if (!row.empty())
                row += ' ';
            row += word;
        }
        if (!row.empty())
            lines.emplace_back("  " + row, color);
    };
    std::vector<int> section_rows;
    const auto* d = PeopleSystem::dossier(people_, dossiers_ui_.selected_person_id_);
    const auto seen = people_["dossier_seen"][dossiers_ui_.selected_person_id_].value_or(-1.0);
    auto is_new = [&](const toml::table& fact) {
        return fact["last_seen_seconds"].value_or(0.0) > seen &&
               computer_.time_seconds() - fact["last_seen_seconds"].value_or(0.0) <=
                   people_["upgrades"]["dossiers"]["highlight_window_seconds"].value_or(300.0);
    };
    add("PROFILE SUMMARY", Heading);
    if (d)
        if (const auto* facts = (*d)["facts"].as_array())
            for (const auto& row : *facts) {
                const auto& f = *row.as_table();
                const auto category = text(f, "category");
                if (category == "identity" || category == "employment" ||
                    text(f, "field").find("goal") != std::string::npos)
                    add(text(f, "field") + ": " + text(f, "value") + (is_new(f) ? " [NEW]" : ""),
                        is_new(f) ? Heading : Plain);
            }
    const toml::table* latest = nullptr;
    if (const auto* observations = people_["observations"].as_array())
        for (const auto& row : *observations) {
            const auto& o = *row.as_table();
            if (text(o, "person_id") == dossiers_ui_.selected_person_id_ &&
                (!latest || o["seconds"].value_or(0.0) > (*latest)["seconds"].value_or(0.0)))
                latest = &o;
        }
    if (latest)
        add("Latest interaction: " + text(*latest, "source") + " | " +
                number(computer_.time_seconds() - (*latest)["seconds"].value_or(0.0), 0) +
                " working seconds ago",
            Footer);
    else
        add("No recorded interactions yet.", Footer);
    add("");
    for (std::size_t i = 0; i < sections.size(); ++i) {
        const auto& section = sections[i];
        section_rows.push_back(static_cast<int>(lines.size()));
        lines.emplace_back(
            std::string(dossiers_ui_.people_details_focused_ &&
                                static_cast<int>(i) == dossiers_ui_.people_section_selected_
                            ? "> "
                            : "  ") +
                (dossiers_ui_.expanded_dossier_sections_.contains(section) ? "[-] " : "[+] ") +
                section,
            Heading);
        if (!dossiers_ui_.expanded_dossier_sections_.contains(section))
            continue;
        bool found = false;
        if (section == "relationships") {
            for (const auto& n : *people_["known_relationships"].as_array()) {
                const auto& r = *n.as_table();
                if (text(r, "from") != dossiers_ui_.selected_person_id_ &&
                    text(r, "to") != dossiers_ui_.selected_person_id_)
                    continue;
                found = true;
                add(PeopleSystem::name(people_, text(r, "from")) + " -> " +
                    PeopleSystem::name(people_, text(r, "to")) + ": " + text(r, "label"));
                add("Type: " + text(r, "type") +
                        " | confidence: " + number(r["confidence"].value_or(1.0) * 100, 0) + "%",
                    Footer);
                if (const auto* evidence = r["evidence_ids"].as_array())
                    add(std::to_string(evidence->size()) +
                            " supporting records; Actions -> Connections for evidence",
                        Footer);
            }
        } else if (section == "observations") {
            for (const auto& n : *people_["observations"].as_array()) {
                const auto& e = *n.as_table();
                if (text(e, "person_id") != dossiers_ui_.selected_person_id_)
                    continue;
                found = true;
                add("+" + number(e["seconds"].value_or(0.0), 0) + " s | " + text(e, "direction") +
                        " | " + text(e, "source"),
                    Footer);
                add(text(e, "text"));
            }
        } else if (section == "unknowns") {
            found = true;
            add("Private state is not visible. Missing facts remain unknown.");
            if (auto* facts = (*d)["facts"].as_array())
                for (std::size_t a = 0; a < facts->size(); ++a)
                    for (std::size_t b = a + 1; b < facts->size(); ++b) {
                        const auto& one = *(*facts)[a].as_table();
                        const auto& two = *(*facts)[b].as_table();
                        if (text(one, "category") == text(two, "category") &&
                            text(one, "field") == text(two, "field") &&
                            text(one, "value") != text(two, "value"))
                            add("CONFLICTING EVIDENCE: " + text(one, "field") + " = " +
                                    text(one, "value") + " / " + text(two, "value"),
                                Protected);
                    }
        } else if (auto* facts = (*d)["facts"].as_array())
            for (const auto& n : *facts) {
                const auto& f = *n.as_table();
                if (text(f, "category") != section)
                    continue;
                found = true;
                const auto confidence = f["confidence"].value_or(1.0);
                const auto claim = text(f, "claim_type");
                const std::string label =
                    claim == "unconfirmed" || claim == "inferred" || confidence < .5 ? "Uncertain"
                    : claim == "observed" || claim == "confirmed"                    ? "Confirmed"
                                                                                     : "Reported";
                bool conflict = false;
                for (const auto& other : *facts) {
                    const auto& candidate = *other.as_table();
                    if (text(candidate, "category") == text(f, "category") &&
                        text(candidate, "field") == text(f, "field") &&
                        text(candidate, "value") != text(f, "value"))
                        conflict = true;
                }
                add((conflict ? "[CONFLICT] " : "") + label + " | " + text(f, "field") + ": " +
                        text(f, "value") + (is_new(f) ? " [NEW / CHANGED]" : ""),
                    conflict    ? Protected
                    : is_new(f) ? Heading
                                : Plain);
                add(text(f, "claim_type") + " | confidence " +
                        number(f["confidence"].value_or(1.0), 2) + " | +" +
                        number(f["last_seen_seconds"].value_or(0.0), 0) + " s",
                    Footer);
                for (const auto& ev : *f["evidence_ids"].as_array())
                    for (const auto& row : *people_["observations"].as_array()) {
                        const auto& source = *row.as_table();
                        if (text(source, "id") == ev.value_or(std::string{}))
                            add("Source: " + text(source, "source") +
                                    " | Actions -> Dossier evidence",
                                Footer);
                    }
            }
        if (!found)
            add("Unknown; no supporting observations.", Footer);
    }
    const int visible = std::max(1, bottom - top - 1);
    if (dossiers_ui_.people_details_focused_ && dossiers_ui_.people_reveal_section_) {
        dossiers_ui_.people_reveal_section_ = false;
        const int cursor = section_rows[dossiers_ui_.people_section_selected_];
        if (cursor < dossiers_ui_.people_detail_offset_)
            dossiers_ui_.people_detail_offset_ = cursor;
        else if (cursor >= dossiers_ui_.people_detail_offset_ + visible)
            dossiers_ui_.people_detail_offset_ = cursor - visible + 1;
    }
    dossiers_ui_.people_detail_offset_ =
        std::clamp(dossiers_ui_.people_detail_offset_, 0,
                   std::max(0, static_cast<int>(lines.size()) - visible));
    for (int i = 0;
         i < visible && dossiers_ui_.people_detail_offset_ + i < static_cast<int>(lines.size());
         ++i) {
        const auto& [s, color] = lines[dossiers_ui_.people_detail_offset_ + i];
        write(top + 1 + i, detail_x, fit(s, detail_width), color);
    }
    write(bottom, 1,
          fit((settings_.binding_label(KeyAction::FocusLeft) + "/" +
               settings_.binding_label(KeyAction::FocusRight)) +
                  ": focus | " + settings_.binding_label(KeyAction::SelectPrevious) + "/" +
                  settings_.binding_label(KeyAction::SelectNext) +
                  (dossiers_ui_.people_details_focused_ ? ": section | PgUp/PgDn: read | "
                                                        : ": person | ") +
                  settings_.binding_label(KeyAction::Submit) + ": actions",
              width - 2),
          Footer);
}
} // namespace ai_sim::ui
