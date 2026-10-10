#include "ai_sim/ui/TerminalUI.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace ai_sim::ui {
namespace {

std::string shortened(std::string value, int width) {
    if (width <= 0) {
        return "";
    }

    if (value.size() > static_cast<std::size_t>(width)) {
        value = value.substr(0, width - 1) + "~";
    }

    return value;
}

// Clip a line to the map viewport before drawing it.
bool clip_line(double& x0, double& y0, double& x1, double& y1, double left, double top,
               double right, double bottom) {
    const double dx = x1 - x0;
    const double dy = y1 - y0;

    double first = 0.0;
    double last = 1.0;

    const auto boundary = [&](double p, double q) {
        if (p == 0.0) {
            return q >= 0.0;
        }

        const double ratio = q / p;

        if (p < 0.0) {
            if (ratio > last)
                return false;
            first = std::max(first, ratio);
        } else {
            if (ratio < first)
                return false;
            last = std::min(last, ratio);
        }

        return true;
    };

    if (!boundary(-dx, x0 - left) || !boundary(dx, right - x0) || !boundary(-dy, y0 - top) ||
        !boundary(dy, bottom - y0)) {
        return false;
    }

    const double original_x = x0;
    const double original_y = y0;

    x0 = original_x + first * dx;
    y0 = original_y + first * dy;
    x1 = original_x + last * dx;
    y1 = original_y + last * dy;

    return true;
}

} // namespace

void TerminalUI::select_network_host(int direction) {
    const auto& hosts = network_.hosts();
    std::vector<std::size_t> known;

    for (std::size_t i = 0; i < hosts.size(); ++i) {
        if (hosts[i].known) {
            known.push_back(i);
        }
    }

    if (known.empty()) {
        return;
    }

    const auto found = std::find_if(known.begin(), known.end(), [&](std::size_t i) {
        return hosts[i].id == selected_network_host_id_;
    });

    int position = 0;

    if (found != known.end()) {
        position = std::clamp(static_cast<int>(found - known.begin()) + direction, 0,
                              static_cast<int>(known.size()) - 1);
    } else {
        const auto local = std::find(known.begin(), known.end(), network_.local_host_index());

        if (local != known.end()) {
            position = static_cast<int>(local - known.begin());
        }
    }

    const auto& selected = hosts[known[position]];
    const auto& local = hosts[network_.local_host_index()];

    selected_network_host_id_ = selected.id;
    network_detail_offset_ = 0;

    network_pan_x_ = selected.x - local.x;
    network_pan_y_ = selected.y - local.y;
}

void TerminalUI::pan_network(int horizontal, int vertical) {
    const double step = static_cast<double>(settings_.scroll_step) / network_zoom_;

    network_pan_x_ = std::clamp(network_pan_x_ + horizontal * step, -20000.0, 20000.0);

    network_pan_y_ = std::clamp(network_pan_y_ + vertical * step, -20000.0, 20000.0);
}

void TerminalUI::draw_network(int top, int bottom, int width) {
    const auto& hosts = network_.hosts();
    const auto& links = network_.links();

    if (selected_network_host_id_.empty()) {
        select_network_host(0);
    }

    const auto selected_it = std::find_if(hosts.begin(), hosts.end(), [&](const auto& host) {
        return host.id == selected_network_host_id_;
    });

    if (selected_it == hosts.end()) {
        write(top, 1, "No known hosts.", Heading);
        return;
    }

    const auto selected_index = static_cast<std::size_t>(selected_it - hosts.begin());

    const auto& selected = *selected_it;
    const auto& local = hosts[network_.local_host_index()];

    const int panel_width = std::min(settings_.network_panel_width, width - 20);

    const int divider = width - panel_width - 2;
    const int map_left = 1;
    const int map_right = divider - 2;
    const int map_top = top + 1;
    const int map_bottom = bottom - 3;

    write(top, 1, "NETWORK | Zoom " + number(network_zoom_, 1) + "x",
          network_details_focused_ ? Heading : ActiveTab, true);

    if (map_bottom < map_top) {
        return;
    }

    for (int row = top; row < bottom; ++row) {
        line_cell(row, divider, '|', Border);
    }

    const double center_x = (map_left + map_right) / 2.0;

    const double center_y = (map_top + map_bottom) / 2.0;

    const double camera_x = local.x + network_pan_x_;
    const double camera_y = local.y + network_pan_y_;

    const auto project = [&](const NetworkHost& host) {
        return std::pair<double, double>{center_x + (host.x - camera_x) * network_zoom_,
                                         center_y + (host.y - camera_y) * network_zoom_};
    };

    const auto plot = [&](int x, int y, char symbol, int color) {
        if (x >= map_left && x <= map_right && y >= map_top && y <= map_bottom) {
            line_cell(y, x, symbol, color);
        }
    };

    // Links first, so host labels are drawn on top.
    for (const auto& link : links) {
        const auto& from = hosts[link.from];
        const auto& to = hosts[link.to];

        if (!link.known || !from.known || !to.known) {
            continue;
        }

        const auto start = project(from);
        const auto end = project(to);

        double x0 = start.first;
        double y0 = start.second;
        double x1 = end.first;
        double y1 = end.second;

        if (!clip_line(x0, y0, x1, y1, map_left, map_top, map_right, map_bottom)) {
            continue;
        }

        const bool operational = link.up && from.online && to.online;

        const bool connected_to_selection =
            link.from == selected_index || link.to == selected_index;

        const int color = connected_to_selection ? (!operational ? Protected : Heading) : Footer;

        const double dx = x1 - x0;
        const double dy = y1 - y0;

        const int steps =
            std::max(1, static_cast<int>(std::ceil(std::max(std::abs(dx), std::abs(dy)))));

        const char symbol = std::abs(dy) < 0.5   ? '-'
                            : std::abs(dx) < 0.5 ? '|'
                            : dx * dy >= 0.0     ? '\\'
                                                 : '/';

        for (int step = 0; step <= steps; ++step) {
            const double fraction = static_cast<double>(step) / steps;

            plot(static_cast<int>(std::lround(x0 + dx * fraction)),
                 static_cast<int>(std::lround(y0 + dy * fraction)), symbol, color);
        }

        if (!operational) {
            continue;
        }

        // Decorative activity markers follow simulation time.
        // They indicate traffic; they are not individual packets.
        const double phase =
            std::fmod(computer_.time_seconds() / 4.0 + static_cast<double>(link.from) * 0.17, 1.0);

        const auto pulse = [&](double fraction, char marker) {
            plot(static_cast<int>(std::lround(start.first + (end.first - start.first) * fraction)),
                 static_cast<int>(
                     std::lround(start.second + (end.second - start.second) * fraction)),
                 marker, connected_to_selection ? Heading : Footer);
        };

        if (link.forward_bits_per_second > 0.0) {
            pulse(phase, '>');
        }

        if (link.reverse_bits_per_second > 0.0) {
            pulse(1.0 - phase, '<');
        }
    }

    int known_count = 0;
    int onscreen_count = 0;

    struct Label {
        int x, y, width;
    };
    std::vector<Label> labels;
    std::vector<std::size_t> draw_order{selected_index};
    for (std::size_t i = 0; i < hosts.size(); ++i)
        if (i != selected_index)
            draw_order.push_back(i);
    for (const auto index : draw_order) {
        const auto& host = hosts[index];
        if (!host.known)
            continue;
        ++known_count;
        const auto point = project(host);
        const int x = static_cast<int>(std::lround(point.first));
        const int y = static_cast<int>(std::lround(point.second));
        if (x < map_left || x > map_right || y < map_top || y > map_bottom)
            continue;
        const bool is_selected = index == selected_index;
        const bool reachable = network_.reachable(index);
        const bool usable = host.online && reachable;
        const std::string status = !usable ? "x " : host.access_granted ? "+ " : "! ";
        const std::string label =
            std::string(is_selected ? "{" : "[") + status + (host.ai_present ? "* " : "") +
            shortened(host.name, std::min(settings_.map_label_width, map_right - map_left - 7)) +
            (is_selected ? "}" : "]");
        const int label_left = std::clamp(x - static_cast<int>(label.size()) / 2, map_left,
                                          map_right - static_cast<int>(label.size()) + 1);
        bool drawn = false;
        for (int distance = 0; distance <= map_bottom - map_top && !drawn; ++distance)
            for (const int sign : {1, -1}) {
                const int row = y + distance * sign;
                if (row < map_top || row > map_bottom)
                    continue;
                if (std::any_of(labels.begin(), labels.end(), [&](const auto& placed) {
                        return row == placed.y && label_left < placed.x + placed.width + 1 &&
                               label_left + static_cast<int>(label.size()) + 1 > placed.x;
                    }))
                    continue;
                if (row != y)
                    plot(x, y, '.', Border);
                const int color = !usable ? Protected : host.access_granted ? Stoppable : MapLocked;
                write(row, label_left, label, color, is_selected, !host.online);
                if (host.ai_present)
                    write(row, label_left + 3, "*", Player, is_selected, !host.online);
                labels.push_back({label_left, row, static_cast<int>(label.size())});
                ++onscreen_count;
                drawn = true;
                break;
            }
    }

    write(bottom - 2, map_left, "+ accessible", Stoppable);
    write(bottom - 2, map_left + 13, "! locked", MapLocked);
    write(bottom - 2, map_left + 23, "x down", Protected);
    write(bottom - 1, map_left, "* AI present", Player);
    write(bottom - 1, map_left + 14, ". moved label", Footer);

    // Selected-host details.
    const int panel_left = divider + 2;
    std::vector<std::pair<std::string, int>> details;
    const auto detail = [&](const std::string& text, int color) {
        // Wrap panel text rather than permanently truncating addresses/services.
        if (text.empty()) {
            details.emplace_back("", color);
            return;
        }
        const std::size_t line_width = panel_width - 1;
        for (std::size_t start = 0; start < text.size(); start += line_width) {
            details.emplace_back(text.substr(start, line_width), color);
        }
    };

    write(top, panel_left, shortened("[DETAILS] " + selected.name, panel_width - 1),
          network_details_focused_ ? ActiveTab : Heading, true);
    const bool selected_reachable = network_.reachable(selected_index);
    detail(!selected.online           ? "x Offline: server is powered off"
           : !selected_reachable      ? "x Unreachable: no observed route"
           : !selected.access_granted ? "! Locked: credentials required"
                                      : "+ Accessible: route and credentials available",
           !selected.online || !selected_reachable ? Protected
           : selected.access_granted               ? Stoppable
                                                   : MapLocked);
    detail("ID: " + selected.id, Plain);
    detail("Type: " + selected.kind, Plain);
    detail("Address: " + selected.address, Plain);
    detail(selected.online ? "Power: online" : "Power: offline", Plain);

    detail(network_.reachable(selected_index) ? "Observed route: reachable"
                                              : "Observed route: unreachable",
           Resources);

    detail(selected.access_granted ? "Credentials: available" : "Credentials: unavailable",
           selected.access_granted ? Stoppable : Plain);

    detail(selected.ai_present ? "AI presence: yes" : "AI presence: no",
           selected.ai_present ? Player : Plain);

    detail("", Plain);
    detail("SERVICES", Heading);

    if (selected.services.empty()) {
        detail("(none known)", Plain);
    } else {
        for (const auto& service : selected.services) {
            detail(service, Plain);
        }
    }

    detail("", Plain);
    detail("OBSERVED LINKS", Heading);

    for (const auto& link : links) {
        if (!link.known) {
            continue;
        }

        std::size_t other;

        if (link.from == selected_index) {
            other = link.to;
        } else if (link.to == selected_index) {
            other = link.from;
        } else {
            continue;
        }

        if (!hosts[other].known) {
            continue;
        }

        const bool operational = link.up && selected.online && hosts[other].online;

        detail(hosts[other].name + (operational ? "" : " [DOWN]"),
               operational ? Resources : Protected);

        detail("Cap " + number(link.capacity_bits_per_second / 1000000.0, 1) + " Mbit/s", Plain);

        const double sent = link.from == selected_index ? link.forward_bits_per_second
                                                        : link.reverse_bits_per_second;

        const double received = link.from == selected_index ? link.reverse_bits_per_second
                                                            : link.forward_bits_per_second;

        detail("Tx/Rx " + number(operational ? sent / 1000000.0 : 0.0, 2) + "/" +
                   number(operational ? received / 1000000.0 : 0.0, 2) + " Mbit/s",
               Plain);

        detail(number(link.latency_milliseconds, 1) + " ms | Loss " +
                   number(link.packet_loss_percent, 1) + "%",
               Plain);
    }

    detail("", Plain);
    detail(selected.description, Plain);
    const int panel_rows = std::max(0, bottom - top - 1);
    network_detail_offset_ = std::clamp(network_detail_offset_, 0,
                                        std::max(0, static_cast<int>(details.size()) - panel_rows));
    for (int row = 0; row < panel_rows; ++row) {
        const int index = network_detail_offset_ + row;
        if (index >= static_cast<int>(details.size()))
            break;
        write(top + 1 + row, panel_left, details[index].first, details[index].second);
    }

    write(bottom, 1,
          (settings_.binding_label(KeyAction::FocusLeft) + "/" +
           settings_.binding_label(KeyAction::FocusRight)) +
              ": focus | " + settings_.binding_label(KeyAction::SelectPrevious) + "/" +
              settings_.binding_label(KeyAction::SelectNext) +
              (network_details_focused_ ? ": read details | " : ": host | ") +
              settings_.binding_label(KeyAction::ScrollUp) + "/" +
              settings_.binding_label(KeyAction::ScrollDown) +
              (network_details_focused_ ? ": read" : ": pan"),
          Footer);
}

} // namespace ai_sim::ui