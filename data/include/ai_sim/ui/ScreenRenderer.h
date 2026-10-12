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
    ScreenRenderer();
    ~ScreenRenderer();
    ScreenRenderer(const ScreenRenderer&) = delete;
    ScreenRenderer& operator=(const ScreenRenderer&) = delete;
    struct DialogLayout {
        int top, left, height, width;
    };
    DialogLayout dialog_layout() const;
    static int list_divider(int width);
    int pane_divider() const;
    void begin(bool modal, bool colors, bool repaint);
    void split(int top, int bottom, int column);
    void write(int row, int column, const std::string& text, int color, bool bold, bool dim);
    void cell(int row, int column, char symbol, int color);
    void scroll_text(const std::string& id, const std::vector<TextRow>& rows, int offset, int top,
                     int left, int height, int width);
    void cursor(int row, int column, bool highlight = false, int color = 0);
    void present();
    int input(int milliseconds);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace ai_sim::ui
