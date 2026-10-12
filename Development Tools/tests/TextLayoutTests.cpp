// Verify text layout keeps token order, paragraph boundaries and narrow-pane bounds.
#include "ai_sim/ui/TextLayout.h"
#include <iostream>
#include <stdexcept>
using namespace ai_sim::ui;
int main() {
    try {
        if (fit_text("abcdef", 4, "~") != "abc~" || !fit_text("abcdef", 0).empty() ||
            fit_text("abcdef", 2, "...") != "ab")
            throw std::runtime_error("Clipping exceeded the pane budget");
        const auto lines = wrap_text("before enormousword\n\nafter", 6);
        if (lines != std::vector<std::string>{"before", "enormo", "usword", "", "after"})
            throw std::runtime_error("Paragraph or long-token order changed");
        for (int width : {-1, 0, 1, 2, 8})
            for (const auto& line : wrap_text("a abcdefghijklmnop b", width))
                if (line.size() > static_cast<std::size_t>(width > 0 ? width : 1))
                    throw std::runtime_error("Wrapped line exceeded the pane width");
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
