// Module: Shared prose wrapping and bounded clipping for terminal panes.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace ai_sim::ui {
// Clip display text to a byte budget. A marker is optional and also fits the
// budget; this matches the renderer's existing byte-based text handling.
std::string fit_text(std::string_view text, int width, std::string_view marker = {});

// Wrap prose without losing paragraph breaks. Long tokens are split so narrow
// panes cannot overflow or loop forever. Source-code previews retain hard wraps.
std::vector<std::string> wrap_text(const std::string& text, int width);
} // namespace ai_sim::ui
