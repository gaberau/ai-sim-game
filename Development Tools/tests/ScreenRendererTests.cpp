#include "ai_sim/ui/ScreenRenderer.h"
#include <ncurses.h>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
char cell(int row, int column) {
    return static_cast<char>(mvwinch(curscr, row, column) & A_CHARTEXT);
}
} // namespace
int main() {
    FILE* output = tmpfile();
    FILE* input = tmpfile();
    SCREEN* screen = newterm("xterm", output, input);
    if (!screen)
        return 1;
    int result = 0;
    try {
        resizeterm(30, 100);
        ai_sim::ui::ScreenRenderer renderer;
        renderer.begin(false, false, false);
        renderer.split(4, 26, 30);
        check(renderer.pane_divider() == 30, "Active pane divider does not match layout");
        renderer.write(5, 1, std::string(90, 'L'), 0, false, false);
        renderer.write(5, 32, "RIGHT", 0, false, false);
        renderer.cell(5, 30, '|', 0);
        renderer.write(0, 0, "HEADER", 0, false, false);
        renderer.write(29, 1, "FOOTER", 0, false, false);
        renderer.present();
        check(cell(0, 0) == 'H' && cell(29, 1) == 'F', "Header/footer surfaces missing");
        check(cell(5, 29) == 'L' && cell(5, 31) == ' ' && cell(5, 32) == 'R',
              "Pane text leaked across divider");
        std::vector<ai_sim::ui::ScreenRenderer::TextRow> rows;
        for (int i = 0; i < 10000; ++i)
            rows.push_back({"Row " + std::to_string(i)});
        renderer.begin(false, false, false);
        renderer.scroll_text("large-file", rows, 5000, 6, 2, 10, 40);
        renderer.cursor(6, 2, true);
        renderer.present();
        check(cell(6, 6) == '5' && cell(15, 9) == '9', "Pad viewport offset incorrect");
        check((mvwinch(curscr, 6, 2) & A_REVERSE) != 0, "Software cursor missing");
        renderer.begin(true, false, false);
        renderer.write(6, 2, "MENU", 0, false, false);
        renderer.present();
        const auto dialog = renderer.dialog_layout();
        check(dialog.top > 0 && dialog.left > 0, "Dialog was not centered inside workspace");
        check(cell(dialog.top + 6, dialog.left + 2) == 'M', "Modal panel missing");
        check(cell(6, 2) == 'R', "Modal erased uncovered workspace");
        check(cell(dialog.top, dialog.left) != ' ', "Dialog border missing");
        renderer.begin(false, false, false);
        renderer.scroll_text("large-file", rows, 9999, 6, 2, 10, 40);
        renderer.present();
        check(cell(6, 6) == '9' && cell(7, 2) == ' ', "Pad dismissal or last-row offset incorrect");
        for (const auto [height, width] :
             {std::pair{12, 40}, std::pair{40, 120}, std::pair{30, 100}}) {
            resizeterm(height, width);
            renderer.begin(false, false, true);
            renderer.scroll_text("large-file", rows, 5000, 4, 1, height - 7, width - 2);
            renderer.write(height - 1, 1, "RESIZED", 0, false, false);
            renderer.present();
            check(cell(4, 5) == '5' && cell(height - 1, 1) == 'R',
                  "Resize lost viewport or footer");
        }
        resizeterm(24, 80);
        renderer.begin(false, false, false);
        renderer.write(2, 3, "TABS", 0, false, false);
        renderer.begin(true, false, false);
        check(renderer.dialog_layout().top >= 4, "Dialog covers the tab bar at minimum size");
        renderer.write(5, 3, "TEXT", 0, false, false);
        renderer.cursor(5, 7, true);
        renderer.present();
        const auto text_dialog = renderer.dialog_layout();
        check((mvwinch(curscr, text_dialog.top + 5, text_dialog.left + 7) & A_REVERSE) != 0,
              "Modal text cursor missing");
        check(cell(2, 3) == 'T', "Minimum-size dialog erased persistent tabs");
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        result = 1;
    }
    endwin();
    delscreen(screen);
    fclose(input);
    fclose(output);
    return result;
}
