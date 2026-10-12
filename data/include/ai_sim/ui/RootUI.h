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
    virtual bool centered_overlay() const {
        return false;
    }
    virtual void place_cursor() {
    }
    virtual bool auto_open() const {
        return false;
    }
    virtual void on_close() {
    }
    virtual ~UIPage() = default;
    virtual std::string_view id() const = 0;
    virtual std::string_view title() const = 0;
    virtual bool available(const UIContext& context) const = 0;
    virtual void draw(UIContext& context, const UIRegion& region) = 0;
    // Return true when consumed; otherwise the root can handle navigation.
    virtual bool handle_key(UIContext& context, int key) = 0;
    virtual std::string controls(const UIContext& context) const = 0;
};

class RootUI final {
  public:
    RootUI(WorldState& world, UISettings& settings);
    ~RootUI();
    RootUI(const RootUI&) = delete;
    RootUI& operator=(const RootUI&) = delete;

    struct Hooks {
        std::function<std::string()> clock;
        std::function<void()> menu, help, pause;
        std::function<void(int, int)> notice, header;
    } hooks;
    void start_session();
    void apply_settings();
    bool colors_enabled() const {
        return colors_enabled_;
    }
    ScreenRenderer& renderer() {
        return *renderer_;
    }
    std::string_view active_workspace() const {
        return active_workspace_;
    }
    bool exit_requested() const {
        return exit_requested_;
    }
    bool has_overlay();
    bool modal_active();
    void cycle_workspace(int direction);
    // Every screen is attached here; there are no separately running UI pages.
    void add_child(std::unique_ptr<UIPage> page, UIPageLayer layer);
    bool activate_workspace(std::string_view id);
    bool open_overlay(std::string_view id);
    void close_overlay();
    void resize(int rows, int columns);
    void handle_key(int key);
    void draw();

  private:
    struct Child {
        std::unique_ptr<UIPage> page;
        UIPageLayer layer;
    };

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
    UIRegion header_, tabs_, workspace_, footer_, overlay_;
};
} // namespace ai_sim::ui
