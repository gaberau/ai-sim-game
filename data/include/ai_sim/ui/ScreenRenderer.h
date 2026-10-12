// Module: Own ncurses windows, pads and panels; stage one composed frame before presentation.
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace ai_sim::ui {

// Owns terminal surfaces only. Child pages own interaction state; the game core owns gameplay.
class ScreenRenderer {
  public:
    struct TextRow {
        std::string text;
        int color = 0;
        bool bold = false;
        bool dim = false;
    };
    // Initialize ownership and configuration; terminal startup remains explicit.
    ScreenRenderer();
    // Release owned resources; terminal lifetime remains controlled by RootUI.
    ~ScreenRenderer();
    ScreenRenderer(const ScreenRenderer&) = delete;
    ScreenRenderer& operator=(const ScreenRenderer&) = delete;
    struct DialogLayout {
        int top, left, height, width;
    };
    // Return the centered dialog bounds in screen coordinates.
    DialogLayout dialog_layout() const;
    // Choose a responsive list/detail divider for the supplied terminal width.
    static int list_divider(int width);
    // Return the current divider so inline actions respect the active page layout.
    int pane_divider() const;
    // Choose the frame target and reset its buffered content without physical refresh.
    // Update dialog bounds; surfaces are recreated on the next frame when needed.
    void configure_dialog(int max_width, int max_height);
    void begin(bool modal, bool colors, bool repaint);
    // Create/update shared-buffer left/right panes for the supplied row bounds.
    void split(int top, int bottom, int column);
    // Write styled text into the owning pane, clipping at its boundary.
    void write(int row, int column, const std::string& text, int color, bool bold, bool dim);
    // Write one decorative cell through the owning ncurses surface.
    void cell(int row, int column, char symbol, int color);
    // Copy a bounded pad viewport; offset indexes text rows, not terminal screen rows.
    void scroll_text(const std::string& id, const std::vector<TextRow>& rows, int offset, int top,
                     int left, int height, int width);
    // Stage cursor position or a software highlight in the active frame.
    void cursor(int row, int column, bool highlight = false, int color = 0);
    // Stage panels and perform the single physical terminal update for this frame.
    void present();
    // Read from the active window with the supplied millisecond timeout.
    int input(int milliseconds);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace ai_sim::ui
