#include "ai_sim/ui/ScreenRenderer.h"

#include <ncurses.h>
#include <panel.h>
#include <algorithm>
#include <stdexcept>
#include <unordered_map>

namespace ai_sim::ui {
namespace {
struct Surface {
    WINDOW* window = nullptr;
    PANEL* panel = nullptr;
    ~Surface() {
        reset();
    }
    void reset() {
        if (panel)
            del_panel(panel);
        if (window)
            delwin(window);
        panel = nullptr;
        window = nullptr;
    }
    void own(WINDOW* value, bool layered = false) {
        reset();
        window = value;
        if (!window)
            throw std::runtime_error("Cannot allocate terminal window");
        leaveok(window, TRUE);
        if (layered) {
            panel = new_panel(window);
            if (!panel)
                throw std::runtime_error("Cannot allocate terminal panel");
        }
    }
};
std::string sanitized(std::string text) {
    for (char& c : text)
        if (static_cast<unsigned char>(c) < 32 || c == 127)
            c = ' ';
    return text;
}
} // namespace
struct ScreenRenderer::Impl {
    // Children are destroyed before their shared parent buffer.
    Surface root, overlay, header, body, footer, left, right;
    std::unordered_map<std::string, std::unique_ptr<Surface>> pads;
    int height = 0, width = 0, cursor_row = 0, cursor_column = 0;
    bool modal = false, colors = false, divided = false;
    WINDOW* active() const {
        return modal ? overlay.window : root.window;
    }
    WINDOW* at(int row, int column) const {
        if (modal)
            return overlay.window;
        for (const Surface* s :
             {divided ? &left : nullptr, divided ? &right : nullptr, &header, &body, &footer}) {
            if (!s || !s->window)
                continue;
            int y, x, h, w;
            getbegyx(s->window, y, x);
            getmaxyx(s->window, h, w);
            if (row >= y && row < y + h && column >= x && column < x + w)
                return s->window;
        }
        return root.window;
    }
    int attributes(int color, bool bold = false, bool dim = false) const {
        return (colors ? COLOR_PAIR(color + 1) : A_NORMAL) | (bold ? A_BOLD : 0) |
               (dim ? A_DIM : 0);
    }
    void resize(int h, int w) {
        left.reset();
        right.reset();
        header.reset();
        body.reset();
        footer.reset();
        pads.clear();
        overlay.reset();
        root.reset();
        height = h;
        width = w;
        root.own(newwin(h, w, 0, 0), true);
        overlay.own(newwin(h, w, 0, 0), true);
        keypad(root.window, TRUE);
        keypad(overlay.window, TRUE);
        if (h >= 8) {
            header.own(derwin(root.window, 4, w, 0, 0));
            body.own(derwin(root.window, h - 7, w, 4, 0));
            footer.own(derwin(root.window, 3, w, h - 3, 0));
        }
    }
};
ScreenRenderer::ScreenRenderer() : impl_(std::make_unique<Impl>()) {
}
ScreenRenderer::~ScreenRenderer() = default;
void ScreenRenderer::begin(bool modal, bool colors, bool repaint) {
    auto& s = *impl_;
    int h, w;
    getmaxyx(stdscr, h, w);
    if (h != s.height || w != s.width)
        s.resize(h, w);
    s.modal = modal;
    s.colors = colors;
    s.divided = false;
    if (modal) {
        show_panel(s.overlay.panel);
        top_panel(s.overlay.panel);
    } else
        hide_panel(s.overlay.panel);
    wbkgd(s.active(), s.attributes(0));
    werase(s.active());
    if (repaint)
        redrawwin(s.active());
    s.cursor_row = s.cursor_column = 0;
}
void ScreenRenderer::split(int top, int bottom, int column) {
    auto& s = *impl_;
    if (s.modal || top < 0 || bottom >= s.height || bottom < top || column <= 0 ||
        column >= s.width - 1)
        return;
    int y = 0, x = 0, h = 0, w = 0;
    if (s.left.window) {
        getbegyx(s.left.window, y, x);
        getmaxyx(s.left.window, h, w);
    }
    if (!s.left.window || y != top || h != bottom - top + 1 || w != column) {
        s.left.own(derwin(s.root.window, bottom - top + 1, column, top, 0));
        s.right.own(derwin(s.root.window, bottom - top + 1, s.width - column - 1, top, column + 1));
    }
    s.divided = true;
}
void ScreenRenderer::write(int row, int column, const std::string& text, int color, bool bold,
                           bool dim) {
    auto& s = *impl_;
    if (row < 0 || column < 0 || row >= s.height || column >= s.width - 1)
        return;
    WINDOW* target = s.at(row, column);
    int y, x, h, w;
    getbegyx(target, y, x);
    getmaxyx(target, h, w);
    const int count = std::min(w - (column - x), s.width - column - 1);
    wattrset(target, s.attributes(color, bold, dim));
    mvwaddnstr(target, row - y, column - x, sanitized(text).c_str(), count);
    wsyncup(target);
}
void ScreenRenderer::cell(int row, int column, char symbol, int color) {
    auto& s = *impl_;
    if (row < 0 || column < 0 || row >= s.height || column >= s.width)
        return;
    WINDOW* target = s.at(row, column);
    int y, x;
    getbegyx(target, y, x);
    wattrset(target, s.attributes(color));
    mvwaddch(target, row - y, column - x,
             symbol == '-'   ? ACS_HLINE
             : symbol == '|' ? ACS_VLINE
                             : static_cast<unsigned char>(symbol));
    wsyncup(target);
}
void ScreenRenderer::scroll_text(const std::string& id, const std::vector<TextRow>& rows,
                                 int offset, int top, int left, int height, int width) {
    auto& s = *impl_;
    height = std::min(height, s.height - top);
    width = std::min(width, s.width - left - 1);
    if (top < 0 || left < 0 || height <= 0 || width <= 0)
        return;
    offset = std::clamp(offset, 0, std::max(0, static_cast<int>(rows.size()) - 1));
    // A viewport-sized pad bounds ncurses allocation even for very large files.
    auto& pad = s.pads[id];
    if (!pad)
        pad = std::make_unique<Surface>();
    int h = 0, w = 0;
    if (pad->window)
        getmaxyx(pad->window, h, w);
    if (!pad->window || h != height || w != width)
        pad->own(newpad(height, width));
    wbkgd(pad->window, s.attributes(0));
    werase(pad->window);
    for (int i = 0; i < height && offset + i < static_cast<int>(rows.size()); ++i) {
        const auto& row = rows[offset + i];
        wattrset(pad->window, s.attributes(row.color, row.bold, row.dim));
        mvwaddnstr(pad->window, i, 0, sanitized(row.text).c_str(), width);
    }
    // Copy into the panel's shared buffer, so overlays and cursor styling compose correctly.
    copywin(pad->window, s.active(), 0, 0, top, left, top + height - 1, left + width - 1, FALSE);
}
void ScreenRenderer::cursor(int row, int column, bool highlight, int color) {
    auto& s = *impl_;
    s.cursor_row = std::clamp(row, 0, s.height - 1);
    s.cursor_column = std::clamp(column, 0, s.width - 1);
    if (highlight)
        mvwchgat(s.active(), s.cursor_row, s.cursor_column, 1, A_REVERSE,
                 s.colors ? static_cast<short>(color + 1) : 0, nullptr);
}
void ScreenRenderer::present() {
    update_panels();
    setsyx(impl_->cursor_row, impl_->cursor_column);
    doupdate();
}
int ScreenRenderer::input(int milliseconds) {
    WINDOW* target = impl_->active();
    wtimeout(target, milliseconds);
    untouchwin(target);
    return wgetch(target);
}
} // namespace ai_sim::ui
