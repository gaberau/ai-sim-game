// Module: Stable color-role indices shared by configured palettes and terminal views.
#pragma once
namespace ai_sim::ui {
// Indices match UISettings::colors and the configured ncurses pairs.
enum Color {
    Plain = 0,
    Protected,
    Stoppable,
    Player,
    Heading,
    Clock,
    Resources,
    ActiveTab,
    InactiveTab,
    Border,
    Log,
    Prompt,
    Footer,
    FileEdit,
    FileRead,
    FileApproval,
    FileBlocked,
    FileUnknown,
    MapLocked
};
} // namespace ai_sim::ui
