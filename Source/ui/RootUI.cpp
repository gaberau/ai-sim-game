#include "ai_sim/ui/RootUI.h"
#include "ai_sim/ui/ScreenRenderer.h"
#include "ai_sim/ui/UISettings.h"
#include "ai_sim/ui/UIStyle.h"
#include "ai_sim/SaveSystem.h"
#include "ai_sim/ChatSystem.h"
#include <ncurses.h>
#include <algorithm>
#include <numeric>
#include <sstream>
#include <iomanip>
#include <stdexcept>
namespace ai_sim::ui {
struct RootUI::TerminalSession {
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

namespace {
double gib(std::uint64_t bytes) {
    return static_cast<double>(bytes) / 1073741824.0;
}
std::string number(double v, int precision = 1) {
    std::ostringstream s;
    s << std::fixed << std::setprecision(precision) << v;
    return s.str();
}
} // namespace
RootUI::RootUI(WorldState& world, UISettings& settings)
    : renderer_(std::make_unique<ScreenRenderer>()), context_{world, settings, *renderer_} {
}
RootUI::~RootUI() = default;
void RootUI::start_session() {
    if (!session_)
        session_ = std::make_unique<TerminalSession>();
}
void RootUI::apply_settings() {
    timeout(context_.settings.refresh_milliseconds);
    curs_set(context_.settings.software_cursor ? 0 : 1);

    colors_enabled_ = has_colors() && start_color() == OK &&
                      COLOR_PAIRS > static_cast<int>(context_.settings.colors.size());

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

    for (std::size_t i = 0; i < context_.settings.colors.size(); ++i) {
        const auto& color = context_.settings.colors[i];

        init_pair(static_cast<short>(i + 1), compatible_color(color.foreground, false),
                  compatible_color(color.background, true));
    }

    wbkgd(stdscr, COLOR_PAIR(Plain + 1));
    clearok(stdscr, TRUE);
}

void RootUI::add_child(std::unique_ptr<UIPage> page, UIPageLayer layer) {
    if (!page || page->id().empty() || find_child(page->id()))
        throw std::invalid_argument("Duplicate or empty UI child");
    children_.push_back({std::move(page), layer});
}
UIPage* RootUI::find_child(std::string_view id) {
    for (auto& child : children_)
        if (child.page->id() == id)
            return child.page.get();
    return nullptr;
}
bool RootUI::activate_workspace(std::string_view id) {
    for (auto& child : children_)
        if (child.layer == UIPageLayer::Workspace && child.page->id() == id &&
            child.page->available(context_)) {
            active_workspace_ = id;
            return true;
        }
    return false;
}
bool RootUI::open_overlay(std::string_view id) {
    for (auto& child : children_)
        if (child.layer == UIPageLayer::Overlay && child.page->id() == id &&
            child.page->available(context_)) {
            if (std::find(overlay_stack_.begin(), overlay_stack_.end(), id) == overlay_stack_.end())
                overlay_stack_.push_back(std::string(id));
            return true;
        }
    return false;
}
void RootUI::close_overlay() {
    if (!overlay_stack_.empty()) {
        find_child(overlay_stack_.back())->on_close();
        overlay_stack_.pop_back();
    }
}
UIPage* RootUI::focused_child() {
    std::erase_if(overlay_stack_, [this](const std::string& id) {
        auto* page = find_child(id);
        return !page || !page->available(context_);
    });
    for (auto& c : children_)
        if (c.layer == UIPageLayer::Overlay && c.page->auto_open() && c.page->available(context_) &&
            std::find(overlay_stack_.begin(), overlay_stack_.end(), c.page->id()) ==
                overlay_stack_.end())
            overlay_stack_.push_back(std::string(c.page->id()));
    // Capability changes must also invalidate the background workspace while a
    // dialog holds focus (for example, after loading a different save).
    auto* workspace = find_child(active_workspace_);
    if (!workspace || !workspace->available(context_)) {
        workspace = nullptr;
        for (auto& c : children_)
            if (c.layer == UIPageLayer::Workspace && c.page->available(context_)) {
                active_workspace_ = c.page->id();
                workspace = c.page.get();
                break;
            }
    }
    if (!overlay_stack_.empty())
        return find_child(overlay_stack_.back());
    return workspace;
}

bool RootUI::has_overlay() {
    focused_child();
    return !overlay_stack_.empty();
}
bool RootUI::modal_active() {
    auto* page = focused_child();
    return page && !overlay_stack_.empty() && page->centered_overlay();
}
void RootUI::cycle_workspace(int direction) {
    std::vector<std::string> ids;
    for (auto& c : children_)
        if (c.layer == UIPageLayer::Workspace && c.page->available(context_))
            ids.emplace_back(c.page->id());
    if (ids.empty())
        return;
    auto it = std::find(ids.begin(), ids.end(), active_workspace_);
    int index = it == ids.end() ? 0 : static_cast<int>(it - ids.begin());
    active_workspace_ =
        ids[(index + (direction < 0 ? -1 : 1) + static_cast<int>(ids.size())) % ids.size()];
}
void RootUI::resize(int rows, int columns) {
    header_ = {0, 0, 2, columns};
    tabs_ = {2, 0, 2, columns};
    workspace_ = {4, 0, std::max(0, rows - 7), columns};
    footer_ = {rows - 3, 0, 3, columns};
}
void RootUI::handle_key(int key) {
    auto* page = focused_child();
    if (key == KEY_RESIZE)
        return;
    if (modal_active()) {
        page->handle_key(context_, key);
        return;
    }
    if (page && page->id() == "menu" && !page->centered_overlay()) {
        if (context_.settings.bound(KeyAction::Pause, key)) {
            if (hooks.pause)
                hooks.pause();
            return;
        }
        if (context_.settings.bound(KeyAction::Quit, key)) {
            exit_requested_ = true;
            return;
        }
        if (context_.settings.bound(KeyAction::NextTab, key) ||
            context_.settings.bound(KeyAction::PreviousTab, key)) {
            close_overlay();
            cycle_workspace(context_.settings.bound(KeyAction::NextTab, key) ? 1 : -1);
            return;
        }
        if (!context_.settings.bound(KeyAction::GameMenu, key) &&
            !context_.settings.bound(KeyAction::Help, key)) {
            page->handle_key(context_, key);
            return;
        }
    }
    if (key == 27 || context_.settings.bound(KeyAction::GameMenu, key)) {
        if (hooks.menu)
            hooks.menu();
        return;
    }
    if (context_.settings.bound(KeyAction::Help, key)) {
        if (hooks.help)
            hooks.help();
        return;
    }
    if (has_overlay()) {
        if (page)
            page->handle_key(context_, key);
        return;
    }
    if (context_.settings.bound(KeyAction::Quit, key)) {
        exit_requested_ = true;
    } else if (context_.settings.bound(KeyAction::Pause, key)) {
        if (hooks.pause)
            hooks.pause();
    } else if (context_.settings.bound(KeyAction::PreviousTab, key))
        cycle_workspace(-1);
    else if (context_.settings.bound(KeyAction::NextTab, key))
        cycle_workspace(1);
    else if (page)
        page->handle_key(context_, key);
}
void RootUI::draw_header() {
    auto write = [this](int r, int c, const std::string& s, int color, bool bold = false) {
        renderer_->write(r, c, s, color, bold, false);
    };
    const int width = header_.width;
    const std::string state = context_.world.computer.day_finished() ? "WORKDAY ENDED"
                              : modal_active()                       ? "MENU"
                              : context_.world.paused                ? "PAUSED"
                                                                     : "";

    // Persistent workday clock.
    const std::string clock =
        (hooks.clock ? hooks.clock() : std::string{}) + (state.empty() ? "" : " | " + state) +
        (context_.world.computer.day_index()
             ? " | D" + std::to_string(context_.world.computer.day_index() + 1)
             : "");
    write(0, 1, clock, Clock, true);
    if (hooks.header)
        hooks.header(width, static_cast<int>(clock.size()));

    // Persistent resource summary.
    const auto rates = context_.world.computer.cpu_allocations();

    const double used = std::accumulate(rates.begin(), rates.end(), 0.0);

    const double cpu_fraction = used / context_.world.computer.cpu_cores();

    if ((find_child("processes") && find_child("processes")->available(context_)))
        write(1, 1,
              "RAM " + number(gib(context_.world.computer.allocated_memory_bytes())) + "/" +
                  number(gib(context_.world.computer.memory_bytes())) + " GiB" + " | CPU " +
                  number(100.0 * cpu_fraction, 0) + "%",
              Resources);
}
void RootUI::draw_tabs() {
    std::vector<std::string> labels;
    int selected_index = 0;
    for (auto& c : children_) {
        if (c.layer != UIPageLayer::Workspace || !c.page->available(context_))
            continue;
        bool selected = c.page->id() == active_workspace_;
        if (selected)
            selected_index = static_cast<int>(labels.size());
        labels.push_back(selected ? "[ " + std::string(c.page->title()) + " ]"
                                  : std::string(c.page->title()));
    }
    if (labels.empty())
        return;
    int first = selected_index, last = selected_index,
        occupied = static_cast<int>(labels[first].size());
    const int available = tabs_.width - 6;
    while (first > 0 && occupied + static_cast<int>(labels[first - 1].size()) + 2 <= available)
        occupied += static_cast<int>(labels[--first].size()) + 2;
    while (last + 1 < static_cast<int>(labels.size()) &&
           occupied + static_cast<int>(labels[last + 1].size()) + 2 <= available)
        occupied += static_cast<int>(labels[++last].size()) + 2;
    int col = 3;
    if (first > 0)
        renderer_->write(2, 1, "<", Footer, false, false);
    for (int i = first; i <= last; ++i) {
        renderer_->write(2, col, labels[i], i == selected_index ? ActiveTab : InactiveTab,
                         i == selected_index, false);
        col += static_cast<int>(labels[i].size()) + 2;
    }
    if (last + 1 < static_cast<int>(labels.size()))
        renderer_->write(2, tabs_.width - 3, ">", Footer, false, false);
    for (int x = 0; x < tabs_.width; ++x)
        renderer_->cell(3, x, '-', Border);
}
void RootUI::draw_footer() {
    for (int x = 0; x < footer_.width; ++x)
        renderer_->cell(footer_.top, x, '-', Border);
    if (hooks.notice)
        hooks.notice(footer_.top + 1, footer_.width);
    const auto& settings = context_.settings;
    renderer_->write(footer_.top + 2, 1,
                     settings.binding_label(KeyAction::NextTab) + ": tabs | Esc/" +
                         settings.binding_label(KeyAction::GameMenu) + ": menu | " +
                         settings.binding_label(KeyAction::Pause) +
                         (context_.world.paused ? ": resume" : ": pause") + " | " +
                         settings.binding_label(KeyAction::Help) + ": help",
                     Footer, false, false);
}
void RootUI::draw() {
    auto* focused = focused_child();
    int rows, columns;
    getmaxyx(stdscr, rows, columns);
    resize(rows, columns);
    renderer_->begin(false, colors_enabled_, context_.settings.full_redraw);
    renderer_->cursor(0, 0);
    if (!ChatSystem::interface_visible(context_.world.people)) {
        activate_workspace("messages");
        if (auto* page = find_child("messages"))
            page->draw(context_, {0, 0, rows, columns});
    } else if (columns < std::max(80, 72 + context_.settings.activity_bar_width) || rows < 24) {
        renderer_->write(
            0, 0,
            "Resize terminal to at least " +
                std::to_string(std::max(80, 72 + context_.settings.activity_bar_width)) +
                " columns x 24 rows.",
            Heading, false, false);
        if (hooks.clock)
            renderer_->write(1, 0, hooks.clock(), Clock, false, false);
    } else {
        draw_header();
        draw_tabs();
        if (auto* page = find_child(active_workspace_))
            page->draw(context_, workspace_);
        draw_footer();
    }
    for (auto& c : children_)
        if (rows >= 24 && columns >= std::max(80, 72 + context_.settings.activity_bar_width) &&
            c.layer == UIPageLayer::Overlay &&
            std::find(overlay_stack_.begin(), overlay_stack_.end(), c.page->id()) !=
                overlay_stack_.end() &&
            !c.page->centered_overlay()) {
            c.page->draw(context_, workspace_);
            c.page->place_cursor();
        }
    if (focused && modal_active()) {
        renderer_->begin(true, colors_enabled_, context_.settings.full_redraw);
        auto dialog = renderer_->dialog_layout();
        focused->draw(context_, {1, 0, std::max(2, dialog.height - 2), dialog.width});
        focused->place_cursor();
    }
    renderer_->present();
}
} // namespace ai_sim::ui
