// Module: Shared prose wrapping and bounded clipping for terminal panes.
#include "ai_sim/ui/TextLayout.h"
#include <algorithm>
#include <sstream>

namespace ai_sim::ui {
std::string fit_text(std::string_view text, int width, std::string_view marker) {
    const auto limit = static_cast<std::size_t>(std::max(0, width));
    if (text.size() <= limit)
        return std::string(text);
    if (marker.size() >= limit)
        return std::string(text.substr(0, limit));
    return std::string(text.substr(0, limit - marker.size())) + std::string(marker);
}
std::vector<std::string> wrap_text(const std::string& text, int width) {
    const auto limit = static_cast<std::size_t>(std::max(1, width));
    std::vector<std::string> lines;
    std::istringstream paragraphs(text);
    std::string paragraph;
    while (std::getline(paragraphs, paragraph)) {
        std::istringstream words(paragraph);
        std::string line, word;
        while (words >> word) {
            if (!line.empty() && line.size() + word.size() + 1 > limit) {
                lines.push_back(line);
                line.clear();
            }
            // Flush preceding words before splitting a long token to preserve order.
            while (word.size() > limit) {
                if (!line.empty()) {
                    lines.push_back(line);
                    line.clear();
                }
                lines.push_back(word.substr(0, limit));
                word.erase(0, limit);
            }
            if (!line.empty())
                line += ' ';
            line += word;
        }
        lines.push_back(line);
    }
    return lines;
}
} // namespace ai_sim::ui
