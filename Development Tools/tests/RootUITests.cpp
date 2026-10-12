#include "ai_sim/ui/RootUI.h"
#include "ai_sim/ui/UISettings.h"
#include "ai_sim/ui/ScreenRenderer.h"
#include "ai_sim/SaveSystem.h"
#include <ncurses.h>
#include <cstdio>
#include <iostream>
#include <stdexcept>
using namespace ai_sim::ui;
namespace {
void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
struct Page final : UIPage {
    std::string name;
    bool visible = true, modal = false;
    int keys = 0, draws = 0, selection = 7;
    UIRegion region;
    explicit Page(std::string id, bool centered = false) : name(std::move(id)), modal(centered) {
    }
    std::string_view id() const override {
        return name;
    }
    std::string_view title() const override {
        return name;
    }
    bool available(const UIContext&) const override {
        return visible;
    }
    bool centered_overlay() const override {
        return modal;
    }
    void draw(UIContext& context, const UIRegion& r) override {
        ++draws;
        region = r;
        context.renderer.write(r.top, 1, name, 0, false, false);
    }
    bool handle_key(UIContext&, int) override {
        ++keys;
        return true;
    }
    std::string controls(const UIContext&) const override {
        return {};
    }
};
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
        ai_sim::WorldState world;
        world.people.insert("chat", toml::table{{"interface_visible", true}});
        UISettings settings;
        settings.keybindings[static_cast<std::size_t>(KeyAction::Quit)].codes = {'q'};
        settings.keybindings[static_cast<std::size_t>(KeyAction::NextTab)].codes = {'t'};
        settings.keybindings[static_cast<std::size_t>(KeyAction::Pause)].codes = {'p'};
        RootUI root(world, settings);
        root.hooks.pause = [&world] {
            world.paused = !world.paused;
        };
        root.hooks.clock = [] {
            return "TEST CLOCK";
        };
        auto messages = std::make_unique<Page>("messages");
        auto* chat = messages.get();
        auto locked = std::make_unique<Page>("locked");
        auto* hidden = locked.get();
        hidden->visible = false;
        auto files = std::make_unique<Page>("files");
        auto* file = files.get();
        auto dialog = std::make_unique<Page>("dialog", true);
        auto* modal = dialog.get();
        root.add_child(std::move(messages), UIPageLayer::Workspace);
        root.add_child(std::move(locked), UIPageLayer::Workspace);
        root.add_child(std::move(files), UIPageLayer::Workspace);
        root.add_child(std::move(dialog), UIPageLayer::Overlay);
        check(!root.activate_workspace("locked"), "Locked page activated");
        check(!root.open_overlay("files"), "Workspace opened as overlay");
        root.cycle_workspace(1);
        check(root.active_workspace() == "files", "Navigation did not skip locked page");
        root.handle_key('x');
        check(file->keys == 1 && chat->keys == 0, "Input routed to inactive workspace");
        root.draw();
        check(file->draws == 1 && hidden->draws == 0 && modal->draws == 0,
              "Root drew hidden or unopened page");
        check(file->region.top == 4 && file->region.height == 23, "Workspace geometry incorrect");
        check(root.open_overlay("dialog") && root.modal_active(), "Modal failed to acquire focus");
        root.handle_key('t');
        check(root.active_workspace() == "files", "Modal leaked global navigation");
        root.handle_key('x');
        check(modal->keys == 2 && file->keys == 1, "Modal input leaked to workspace");
        root.draw();
        check(modal->draws == 1 && file->draws == 2, "Modal lost background workspace");
        root.close_overlay();
        check(!root.has_overlay(), "Overlay did not close");
        resizeterm(40, 120);
        root.draw();
        check(file->region.height == 33 && file->region.width == 120 && file->selection == 7,
              "Resize reset child state or geometry");
        auto actions = std::make_unique<Page>("menu");
        auto* inline_menu = actions.get();
        root.add_child(std::move(actions), UIPageLayer::Overlay);
        root.open_overlay("menu");
        const bool was_paused = world.paused;
        root.handle_key('p');
        check(world.paused != was_paused, "Inline controls swallowed pause");
        check(root.has_overlay() && !root.modal_active(), "Inline controls incorrectly pause time");
        root.handle_key('x');
        check(inline_menu->keys == 1, "Inline controls did not receive input");
        root.draw();
        check(inline_menu->region.top == 4, "Inline controls were centered as a dialog");
        root.handle_key('t');
        check(!root.has_overlay() && root.active_workspace() == "messages",
              "Tab did not dismiss inline controls");
        root.activate_workspace("files");
        file->visible = false;
        root.handle_key('x');
        check(root.active_workspace() == "messages" && chat->keys == 1,
              "Unavailable active page did not fall back");
        hidden->visible = true;
        root.cycle_workspace(1);
        check(root.active_workspace() == "locked", "Newly unlocked page not navigable");
        bool duplicate = false;
        try {
            root.add_child(std::make_unique<Page>("files"), UIPageLayer::Workspace);
        } catch (const std::invalid_argument&) {
            duplicate = true;
        }
        check(duplicate, "Duplicate child accepted");
        root.handle_key('q');
        check(root.exit_requested(), "Root failed to route quit");
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
