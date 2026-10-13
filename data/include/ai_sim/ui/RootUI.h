// Module: Own the terminal lifetime, workspace composition, navigation and focused input routing.
#pragma once

// Root workspace composition and terminal lifecycle.
//
// RootUI
//   Header: clock and unlocked resource summary
//   Tab bar: available workspace children only
//   Workspace: one active child page
//     Messages, Processes, Files, Network, Dossiers, News,
//     Agents, Modifications, Money, Operations, Web, AI, Phone
//   Footer: notifications and controls for the focused child
//   Overlays: Menu -> Settings / Save / Load / Help / Editors
//
// The root owns composition, navigation, input routing and terminal resizing.
// Each child owns its selection, scrolling, editing and internal pane layout.
// Gameplay rules and simulation state stay in the existing game core.

#include <memory>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ai_sim {
struct WorldState;
}

namespace ai_sim::ui {
class ScreenRenderer;
struct UISettings;

struct UIRegion {
    int top = 0;
    int left = 0;
    int height = 0;
    int width = 0;
};

// Shared references; child pages do not create their own world or terminal session.
struct UIContext {
    WorldState& world;
    UISettings& settings;
    ScreenRenderer& renderer;
};

enum class UIPageLayer { Workspace, Overlay };

class UIPage {
  public:
    // Centered dialogs capture focus and suspend automatic simulation time.
    virtual bool centered_overlay() const {
        return false;
    }
    // Stage an optional child-specific cursor after its content is drawn.
    virtual void place_cursor() {
    }
    // Report whether active child state should automatically join the focus stack.
    virtual bool auto_open() const {
        return false;
    }
    // Release active child state when the root closes this overlay.
    virtual void on_close() {
    }
    virtual ~UIPage() = default;
    // Return the stable registration ID used by root navigation.
    virtual std::string_view id() const = 0;
    // Return the visible workspace label used by the tab strip.
    virtual std::string_view title() const = 0;
    // Report whether this child is currently eligible for display or overlay focus.
    virtual bool available(const UIContext& context) const = 0;
    // Draw only the supplied region; the root owns physical frame presentation.
    virtual void draw(UIContext& context, const UIRegion& region) = 0;
    // Return true when consumed; otherwise the root can handle navigation.
    virtual bool handle_key(UIContext& context, int key) = 0;
};

class RootUI final {
  public:
    // Initialize ownership and configuration; terminal startup remains explicit.
    RootUI(WorldState& world, UISettings& settings);
    // Release owned resources; terminal lifetime remains controlled by RootUI.
    ~RootUI();
    RootUI(const RootUI&) = delete;
    RootUI& operator=(const RootUI&) = delete;

    // Gameplay callbacks are borrowed from TerminalUI; these never own model state.
    struct Hooks {
        std::function<std::string()> clock;
        std::function<void()> menu, help, pause, reset_controls;
        std::function<std::string()> controls;
        std::function<void(int, int)> notice, header;
    } hooks;
    // Initialize ncurses once; the root closes it after owned surfaces are destroyed.
    void start_session();
    // Present actual startup work before constructing/loading a world.
    void draw_loading(const std::string& stage);
    // Apply the current palette, cursor preference and terminal input timeout.
    void apply_settings();
    // Report whether ncurses initialized enough configured color pairs.
    bool colors_enabled() const {
        return colors_enabled_;
    }
    // Borrow the single renderer owned by this root.
    ScreenRenderer& renderer() {
        return *renderer_;
    }
    // Borrow the stable ID of the selected root workspace.
    std::string_view active_workspace() const {
        return active_workspace_;
    }
    // Report whether a global quit action ended the application loop.
    bool exit_requested() const {
        return exit_requested_;
    }
    // Synchronize lifecycle state and report whether a child currently holds overlay focus.
    bool has_overlay();
    // Report whether a centered dialog should suspend wall-clock simulation advancement.
    bool modal_active();
    // Navigate available children only, wrapping at either end.
    void cycle_workspace(int direction);
    // Every screen is attached here; there are no separately running UI pages.
    void add_child(std::unique_ptr<UIPage> page, UIPageLayer layer);
    // Select an available workspace; leave selection unchanged when unavailable.
    bool activate_workspace(std::string_view id);
    // Focus a registered available overlay without creating another terminal session.
    bool open_overlay(std::string_view id);
    // Close the focused overlay through its lifecycle hook.
    void close_overlay();
    // Update composition regions for terminal rows/columns while retaining child state.
    void resize(int rows, int columns);
    // Route input according to focused overlay, global controls and active workspace.
    void handle_key(int key);
    // Compose the current workspace/overlays and present one complete frame.
    void draw();

  private:
    struct Child {
        std::unique_ptr<UIPage> page;
        UIPageLayer layer;
    };

    bool workspace_available(UIPage& page) const;
    std::vector<UIPage*> workspace_pages();
    UIPage* find_child(std::string_view id);
    UIPage* focused_child();
    void draw_header();
    void draw_tabs();
    void draw_footer();

    struct TerminalSession;
    std::unique_ptr<TerminalSession> session_;
    bool colors_enabled_ = false;
    bool exit_requested_ = false;
    // One renderer composes the complete frame and presents it once.
    std::unique_ptr<ScreenRenderer> renderer_;
    UIContext context_;
    std::vector<Child> children_;
    std::string active_workspace_ = "messages";
    std::vector<std::string> overlay_stack_;
    UIRegion header_, tabs_, workspace_, footer_;
};
} // namespace ai_sim::ui
