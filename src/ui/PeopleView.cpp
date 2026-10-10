#include "ai_sim/ui/TerminalUI.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <stdexcept>

namespace ai_sim::ui {
namespace {
const std::vector<std::string> sections{
    "identity", "employment", "motivations", "values", "ai_attitudes",
    "relationships", "observations", "unknowns"
};
std::string text(const toml::table& t, const char* key) { return t[key].value_or(std::string{}); }
std::string fit(const std::string& s, int width) {
    if (width < 1) return {};
    if (s.size() <= static_cast<std::size_t>(width)) return s;
    return s.substr(0, std::max(0, width - 1)) + "~";
}
}
std::vector<std::string> TerminalUI::visible_people() const {
    const auto all = PeopleSystem::known_ids(people_);
    if (!people_neighborhood_) return all;
    std::unordered_set<std::string> visible{selected_person_id_};
    std::vector<std::string> frontier{selected_person_id_};
    if (auto* edges = people_["known_relationships"].as_array()) while (!frontier.empty()) {
        const auto id = frontier.back();frontier.pop_back();
        if (!expanded_people_.contains(id)) continue;
        for (const auto& n : *edges) {
            const auto& r = *n.as_table();
            if (people_filter_ != "all" && text(r, "type") != people_filter_) continue;
            const auto a = text(r, "from"), b = text(r, "to");
            if (a != id && b != id) continue;
            const auto other = a == id ? b : a;
            if (visible.insert(other).second) frontier.push_back(other);
        }
    }
    std::vector<std::string> result;
    for (const auto& id : all) if (visible.contains(id)) result.push_back(id);
    return result;
}
void TerminalUI::change_person(const std::string& id) {
    if (id == selected_person_id_) return;
    dossier_positions_[selected_person_id_] = {people_detail_offset_, people_section_selected_, expanded_dossier_sections_};
    selected_person_id_ = id;
    const auto found = dossier_positions_.find(id);
    const DossierPosition saved = found == dossier_positions_.end() ? DossierPosition{} : found->second;
    people_detail_offset_ = saved.offset;people_section_selected_ = saved.section;
    expanded_dossier_sections_ = saved.expanded;
    people_reveal_section_ = false;
    people_pan_x_ = people_pan_y_ = 0;
}
void TerminalUI::select_person(int direction) {
    if (people_details_focused_) {
        people_reveal_section_ = true;
        people_section_selected_ = std::clamp(people_section_selected_ + direction, 0, static_cast<int>(sections.size()) - 1);
        return;
    }
    const auto ids = PeopleSystem::known_ids(people_);
    if (ids.empty()) return;
    const auto found = std::find(ids.begin(), ids.end(), selected_person_id_);
    const int current = found == ids.end() ? 0 : static_cast<int>(found - ids.begin());
    change_person(ids[std::clamp(current + direction, 0, static_cast<int>(ids.size()) - 1)]);
}
void TerminalUI::toggle_people() {
    if (people_details_focused_) {
        people_reveal_section_ = true;
        const auto& section = sections[people_section_selected_];
        if (!expanded_dossier_sections_.erase(section)) expanded_dossier_sections_.insert(section);
    } else {
        if (!expanded_people_.erase(selected_person_id_)) expanded_people_.insert(selected_person_id_);
        // Expanding a node selects the neighborhood mode so the change is visible.
        people_neighborhood_ = true;
    }
}
void TerminalUI::open_observation(const std::string& id) {
    const toml::table* observation = nullptr;
    for (const auto& n : *people_["observations"].as_array())
        if (text(*n.as_table(), "id") == id) observation = n.as_table();
    if (!observation) throw std::runtime_error("Supporting observation is unavailable");
    const auto record = *observation;
    std::string email_id;
    if (id.starts_with("email:")) email_id = id.substr(6);
    else if (text(record, "source").starts_with("Reply to ")) email_id = text(record, "source").substr(9);
    else if (text(record, "source").starts_with("email:")) email_id = text(record, "source").substr(6);
    open_menu("Evidence | " + text(record, "source"), {
        {"Read recorded observation", [this, record] {
            show_message("Recorded observation", text(record, "source") + " | +" + number(record["seconds"].value_or(0.0), 0) + " s | " + text(record, "direction") + " | " + text(record, "text"));
            menu_back_ = [this, id = text(record, "id")] {open_observation(id);};
        }, {}},
        {"Open supporting conversation", [this, email_id, record] {
            if(!email_id.empty())open_chat_request(email_id);
            else {chat_contact_id_=text(record,"person_id");selected_tab_=Tab::Chat;chat_follow_latest_=true;}
        }, email_id.empty() && !text(record,"id").starts_with("chat:") ? "This observation has no linked message." : ""}
    }, [this] {open_people_evidence();});
}
void TerminalUI::open_people_evidence() {
    std::vector<MenuAction> choices;
    if (const auto* d = PeopleSystem::dossier(people_, selected_person_id_))
        for (const auto& n : *(*d)["facts"].as_array()) {
            const auto fact = *n.as_table();
            choices.push_back({text(fact, "category") + " | " + text(fact, "field") + ": " + text(fact, "value"),
                [this, fact] {
                    std::vector<MenuAction> supporting;
                    for (const auto& e : *fact["evidence_ids"].as_array()) {
                        const auto id = e.value_or(std::string{});
                        for (const auto& n : *people_["observations"].as_array()) if (text(*n.as_table(), "id") == id)
                            supporting.push_back({text(*n.as_table(), "source") + " | +" + number((*n.as_table())["seconds"].value_or(0.0), 0) + " s",
                                [this, id] {open_observation(id);}, {}});
                    }
                    open_menu(text(fact, "claim_type") + " | confidence " + number(fact["confidence"].value_or(1.0), 2), std::move(supporting), [this] {open_people_evidence();});
                }, {}});
        }
    choices.push_back({"All recorded interactions...", [this] {
        std::vector<MenuAction> items;
        for (const auto& n : *people_["observations"].as_array()) {
            const auto& e = *n.as_table();
            if (text(e, "person_id") != selected_person_id_) continue;
            const auto id = text(e, "id");
            items.push_back({text(e, "direction") + " | " + text(e, "source"), [this, id] {open_observation(id);}, {}});
        }
        if (items.empty()) items.push_back({"No recorded interactions", {}, "No supporting observations yet."});
        open_menu("Recorded interactions", std::move(items), [this] {open_people_evidence();});
    }, {}});
    open_menu("Dossier evidence", std::move(choices), [this] {open_people_actions();});
}
void TerminalUI::open_people_connections() {
    std::vector<MenuAction> choices;
    if (auto* edges = people_["known_relationships"].as_array()) for (const auto& n : *edges) {
        const auto r = *n.as_table(); const auto from = text(r, "from"), to = text(r, "to");
        if (from != selected_person_id_ && to != selected_person_id_) continue;
        const auto other = from == selected_person_id_ ? to : from;
        const auto label = PeopleSystem::name(people_, from) + " -> " + PeopleSystem::name(people_, to) + ": " + text(r, "label");
        choices.push_back({label, [this, r, other, label] {
            open_menu("Relationship", {
                {"Read relationship summary", [this, r, label] {
                    show_message("Relationship summary", label + ". Type: " + text(r, "type") + ". Confidence: "
                        + number(r["confidence"].value_or(1.0), 2) + ". Last recorded interaction: +"
                        + number(r["last_seen_seconds"].value_or(0.0), 0) + " working seconds. Evidence records: "
                        + std::to_string(r["evidence_ids"].as_array()->size()) + ". Direction does not imply reciprocal trust.");
                    menu_back_ = [this] {open_people_connections();};
                }, {}},
                {"Open connected person's dossier", [this, other] {change_person(other);}, {}},
                {"Show relationship evidence", [this, r] {
                    std::vector<MenuAction> items;
                    for (const auto& e : *r["evidence_ids"].as_array()) {
                        const auto id = e.value_or(std::string{});
                        for (const auto& n : *people_["observations"].as_array()) if (text(*n.as_table(), "id") == id)
                            items.push_back({text(*n.as_table(), "source"), [this, id] {open_observation(id);}, {}});
                    }
                    open_menu("Relationship evidence", std::move(items), [this] {open_people_connections();});
                }, {}}
            }, [this] {open_people_connections();});
        }, {}});
    }
    if (choices.empty()) choices.push_back({"No discovered relationships", {}, "Undiscovered relationships remain hidden."});
    open_menu("Connections", std::move(choices), [this] {open_people_actions();});
}
void TerminalUI::open_people_actions() {
    std::vector<MenuAction> choices{
        {people_details_focused_ ? "Focus relationship map" : "Focus dossier sections", [this] {toggle_pane_focus();}, {}},
        {"Expand / collapse selected " + std::string(people_details_focused_ ? "section" : "person"), [this] {toggle_people();}, {}},
        {"Dossier sections...", [this] {
            std::vector<MenuAction> items;
            for (const auto& s : sections) items.push_back({std::string(expanded_dossier_sections_.contains(s) ? "Collapse " : "Expand ") + s,
                [this, s] {if (!expanded_dossier_sections_.erase(s)) expanded_dossier_sections_.insert(s);people_details_focused_ = true;people_section_selected_ = static_cast<int>(std::find(sections.begin(), sections.end(), s) - sections.begin());people_reveal_section_ = true;people_detail_offset_ = 0;}, {}});
            items.push_back({"Expand all", [this] {expanded_dossier_sections_.insert(sections.begin(), sections.end());people_details_focused_ = true;people_detail_offset_ = 0;}, {}});
            items.push_back({"Collapse all", [this] {expanded_dossier_sections_.clear();people_detail_offset_ = 0;}, {}});
            open_menu("Dossier sections", std::move(items), [this] {open_people_actions();});
        }, {}},
        {"Dossier evidence...", [this] {open_people_evidence();}, {}},
        {"Connections and interactions...", [this] {open_people_connections();}, {}},
        {"Filter connections...", [this] {
            std::vector<MenuAction> items;
            for (const auto& filter : {"all", "communication", "organization", "personal"}) items.push_back({filter, [this, filter] {people_filter_ = filter;}, {}});
            open_menu("Connection filter", std::move(items), [this] {open_people_actions();});
        }, {}},
        {people_neighborhood_ ? "Show all known people" : "Show expanded neighborhood", [this] {people_neighborhood_ = !people_neighborhood_;people_pan_x_ = people_pan_y_ = 0;}, {}},
        {"Center map", [this] {people_pan_x_ = people_pan_y_ = 0;}, {}},
        {"Zoom in", [this] {people_zoom_ = std::min(4.0, people_zoom_ * 1.25);}, {}},
        {"Zoom out", [this] {people_zoom_ = std::max(.5, people_zoom_ / 1.25);}, {}},
        {"Add missing people definitions", [this] {
            PeopleSystem::install(people_, definitions_.people.parent_path() / "social.toml");
            PeopleSystem::observe(people_, emails_, computer_.time_seconds());
            add_log("Missing person definitions and introductions added; existing state retained.");
        }, {}}
    };
    if (auto* list = people_["observations"].as_array()) for (std::size_t i = list->size(); i > 0; --i) {
        const auto& e = *(*list)[i - 1].as_table(); const auto source = text(e, "id");
        if (text(e, "person_id") != selected_person_id_ || !source.starts_with("email:")) continue;
        const auto id = source.substr(6);
        choices.insert(choices.begin(), {"Open latest conversation", [this, id] {
            open_chat_request(id);
        }, {}});
        break;
    }
    open_menu("People | " + PeopleSystem::name(people_, selected_person_id_), std::move(choices));
}
void TerminalUI::draw_people(int top, int bottom, int width) {
    const auto known = PeopleSystem::known_ids(people_);
    if (known.empty()) {write(top, 1, "No people known yet.", Heading);return;}
    if (!PeopleSystem::dossier(people_, selected_person_id_)) change_person(known.front());
    const auto ids = visible_people();
    const int panel = width >= 110 ? 44 : 34, divider = width - panel - 1;
    const int left = 1, right = divider - 2, map_top = top + 2, map_bottom = bottom - 1;
    write(top, 1, "PEOPLE | " + std::to_string(known.size() - 1) + " contacts | " + people_filter_, people_details_focused_ ? Heading : ActiveTab, true);
    write(top + 1, 1, fit(std::string(people_details_focused_ ? "Focus: dossier | " : "Focus: map | ") + (settings_.binding_label(KeyAction::FocusLeft)+"/"+settings_.binding_label(KeyAction::FocusRight)) + ": switch", right), Footer);
    for (int y = top; y < bottom; ++y) write(y, divider, "|", Border);
    const double cx = (left + right) / 2.0, cy = (map_top + map_bottom) / 2.0;
    std::map<std::string, std::pair<double, double>> positions;
    std::vector<std::string> others;
    for (const auto& id : ids) if (id != selected_person_id_) others.push_back(id);
    positions[selected_person_id_] = {cx - people_pan_x_ * people_zoom_, cy - people_pan_y_ * people_zoom_};
    const double rx = std::max(8.0, (right - left) * .35), ry = std::max(3.0, (map_bottom - map_top) * .38);
    for (std::size_t i = 0; i < others.size(); ++i) {
        const double angle = 6.283185307 * i / std::max<std::size_t>(1, others.size());
        const double ring = 1.0 + static_cast<double>(i / 12) * .7;
        positions[others[i]] = {cx + (rx * ring * std::cos(angle) - people_pan_x_) * people_zoom_,
            cy + (ry * ring * std::sin(angle) - people_pan_y_) * people_zoom_};
    }
    auto plot = [&](double x, double y, char c, int color) {
        const int px = static_cast<int>(std::lround(x)), py = static_cast<int>(std::lround(y));
        if (px >= left && px <= right && py >= map_top && py <= map_bottom) write(py, px, std::string(1, c), color);
    };
    if (auto* edges = people_["known_relationships"].as_array()) for (const auto& n : *edges) {
        const auto& e = *n.as_table();const auto from = text(e, "from"), to = text(e, "to");
        if (people_filter_ != "all" && text(e, "type") != people_filter_) continue;
        if (!positions.contains(from) || !positions.contains(to)) continue;
        const auto [x0, y0] = positions[from];const auto [x1, y1] = positions[to];
        const double confidence = e["confidence"].value_or(1.0);
        const int steps = std::min(1000, std::max(1, static_cast<int>(std::ceil(std::max(std::abs(x1 - x0), std::abs(y1 - y0))))));
        const bool recent = text(e, "type") == "communication" && computer_.time_seconds() - e["last_seen_seconds"].value_or(0.0) < 90;
        const int color = confidence < .8 ? Footer : recent ? Resources : Border;
        const char c = confidence < .8 ? ':' : std::abs(y1 - y0) < 1 ? '-' : std::abs(x1 - x0) < 1 ? '|' : (x1 - x0) * (y1 - y0) >= 0 ? '\\' : '/';
        for (int i = 0; i <= steps; ++i) plot(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps, c, color);
        const double f = recent ? .2 + .6 * std::fmod((computer_.time_seconds() - e["last_seen_seconds"].value_or(0.0)) / 6.0, 1.0) : .65;
        plot(x0 + (x1 - x0) * f, y0 + (y1 - y0) * f, std::abs(x1 - x0) >= std::abs(y1 - y0) ? (x1 >= x0 ? '>' : '<') : (y1 >= y0 ? 'v' : '^'), color);
    }
    struct Label {int x, y, width;};
    std::vector<Label> labels;
    std::vector<std::string> draw_order{selected_person_id_};
    for (const auto& id : ids) if (id != selected_person_id_) draw_order.push_back(id);
    int hidden_labels = 0;
    for (const auto& id : draw_order) {
        const auto [x, y] = positions[id];
        const bool selected = id == selected_person_id_;
        const int px = std::clamp(static_cast<int>(std::lround(x)), left, right);
        const int py = std::clamp(static_cast<int>(std::lround(y)), map_top, map_bottom);
        if (!selected && (x < left || x > right || y < map_top || y > map_bottom)) {++hidden_labels;continue;}
        const auto full = std::string(expanded_people_.contains(id) ? "[-] " : "[+] ") + PeopleSystem::name(people_, id);
        const auto label = fit(full, std::min(23, right - left + 1));
        const int lx = std::clamp(px - static_cast<int>(label.size()) / 2, left, right - static_cast<int>(label.size()) + 1);
        bool drawn = false;
        for (int distance = 0; distance <= map_bottom - map_top && !drawn; ++distance) {
            for (int sign : {1, -1}) {
                const int ly = py + distance * sign;
                if (ly < map_top || ly > map_bottom) continue;
                const bool collision = std::any_of(labels.begin(), labels.end(), [&](const Label& l) {
                    return l.y == ly && lx < l.x + l.width + 1 && lx + static_cast<int>(label.size()) + 1 > l.x;
                });
                if (collision) continue;
                // Keep labels away from each other; a dot marks displaced node anchors.
                if (ly != py) plot(x, y, '.', Border);
                write(ly, lx, label, selected ? ActiveTab : id == "self:assistant" ? Player : Heading, selected);
                labels.push_back({lx, ly, static_cast<int>(label.size())});
                drawn = true;break;
            }
        }
        if (!drawn) ++hidden_labels;
    }
    if (hidden_labels) write(top, 1, fit("PEOPLE | " + std::to_string(known.size() - 1) + " contacts | " + std::to_string(hidden_labels) + " hidden labels", right), people_details_focused_ ? Heading : ActiveTab, true);
    const int detail_x = divider + 2, detail_width = width - detail_x - 1;
    write(top, detail_x, fit(PeopleSystem::name(people_, selected_person_id_), detail_width), people_details_focused_ ? ActiveTab : Heading, true);
    std::vector<std::pair<std::string, int>> lines;
    auto add = [&](const std::string& s, int color = Plain) {
        if (s.empty()) {lines.emplace_back("", color);return;}
        std::istringstream input(s);std::string row;
        while (std::getline(input, row)) for (std::size_t start = 0; start < row.size() || (row.empty() && start == 0); start += std::max(1, detail_width - 2)) {
            lines.emplace_back("  " + row.substr(start, std::max(1, detail_width - 2)), color); if (row.empty()) break;
        }
    };
    std::vector<int> section_rows;
    const auto* d = PeopleSystem::dossier(people_, selected_person_id_);
    for (std::size_t i = 0; i < sections.size(); ++i) {
        const auto& section = sections[i];section_rows.push_back(static_cast<int>(lines.size()));
        lines.emplace_back(std::string(people_details_focused_ && static_cast<int>(i) == people_section_selected_ ? "> " : "  ")
            + (expanded_dossier_sections_.contains(section) ? "[-] " : "[+] ") + section, Heading);
        if (!expanded_dossier_sections_.contains(section)) continue;
        bool found = false;
        if (section == "relationships") {
            for (const auto& n : *people_["known_relationships"].as_array()) {
                const auto& r = *n.as_table();
                if (text(r, "from") != selected_person_id_ && text(r, "to") != selected_person_id_) continue;
                found = true;add(PeopleSystem::name(people_, text(r, "from")) + " -> " + PeopleSystem::name(people_, text(r, "to")) + ": " + text(r, "label"));
            }
        } else if (section == "observations") {
            for (const auto& n : *people_["observations"].as_array()) {
                const auto& e = *n.as_table();if (text(e, "person_id") != selected_person_id_) continue;
                found = true;add("+" + number(e["seconds"].value_or(0.0), 0) + " s | " + text(e, "direction") + " | " + text(e, "source"), Footer);add(text(e, "text"));
            }
        } else if (section == "unknowns") {
            found = true;add("Private state is not visible. Missing facts remain unknown.");
            if (auto* facts = (*d)["facts"].as_array()) for (std::size_t a = 0; a < facts->size(); ++a) for (std::size_t b = a + 1; b < facts->size(); ++b) {
                const auto& one = *(*facts)[a].as_table();const auto& two = *(*facts)[b].as_table();
                if (text(one, "category") == text(two, "category") && text(one, "field") == text(two, "field") && text(one, "value") != text(two, "value"))
                    add("Conflicting evidence: " + text(one, "field") + " = " + text(one, "value") + " / " + text(two, "value"), Protected);
            }
        } else if (auto* facts = (*d)["facts"].as_array()) for (const auto& n : *facts) {
            const auto& f = *n.as_table();if (text(f, "category") != section) continue;
            found = true;add(text(f, "field") + ": " + text(f, "value"));
            add(text(f, "claim_type") + " | confidence " + number(f["confidence"].value_or(1.0), 2) + " | +" + number(f["last_seen_seconds"].value_or(0.0), 0) + " s", Footer);
            for (const auto& ev : *f["evidence_ids"].as_array()) add("Evidence: " + ev.value_or(std::string{}), Footer);
        }
        if (!found) add("Unknown; no supporting observations.", Footer);
    }
    const int visible = std::max(1, bottom - top - 1);
    if (people_details_focused_ && people_reveal_section_) {
        people_reveal_section_ = false;
        const int cursor = section_rows[people_section_selected_];
        if (cursor < people_detail_offset_) people_detail_offset_ = cursor;
        else if (cursor >= people_detail_offset_ + visible) people_detail_offset_ = cursor - visible + 1;
    }
    people_detail_offset_ = std::clamp(people_detail_offset_, 0, std::max(0, static_cast<int>(lines.size()) - visible));
    for (int i = 0; i < visible && people_detail_offset_ + i < static_cast<int>(lines.size()); ++i) {
        const auto& [s, color] = lines[people_detail_offset_ + i];write(top + 1 + i, detail_x, fit(s, detail_width), color);
    }
    write(bottom, 1, fit((settings_.binding_label(KeyAction::FocusLeft)+"/"+settings_.binding_label(KeyAction::FocusRight)) + ": focus | " + settings_.binding_label(KeyAction::SelectPrevious) + "/" + settings_.binding_label(KeyAction::SelectNext) + (people_details_focused_ ? ": section | PgUp/PgDn: read | " : ": person | ") + settings_.binding_label(KeyAction::Submit) + ": actions", width - 2), Footer);
}
}
