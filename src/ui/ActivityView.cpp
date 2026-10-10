#include "ai_sim/ui/TerminalUI.h"
#include "ai_sim/ui/ScreenRenderer.h"

#include <algorithm>

namespace ai_sim::ui {

void TerminalUI::draw_activity(int top, int bottom, int width) {
    write(top, 1, "ACTIVITY HISTORY", Heading, true);

    const int visible_rows = std::max(0, bottom - top - 1);
    const int count = static_cast<int>(log_.size());

    activity_offset_ = std::clamp(activity_offset_, 0, std::max(0, count - visible_rows));

    const int first = std::max(0, count - visible_rows - activity_offset_);

    std::vector<ScreenRenderer::TextRow> rows;
    rows.reserve(log_.size());
    for (const auto& entry : log_)
        rows.push_back({entry, Log});
    renderer_->scroll_text("activity", rows, first, top + 1, 1, visible_rows, width - 2);

    write(bottom, 1,
          settings_.binding_label(KeyAction::ScrollUp) + ": older | " +
              settings_.binding_label(KeyAction::ScrollDown) +
              ": newer | History: " + std::to_string(settings_.history_limit),
          Footer);
}

} // namespace ai_sim::ui