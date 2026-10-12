// Verify TOML presentation edits change geometry/labels without granting game capabilities.
#include "ai_sim/ui/UISettings.h"
#include "ai_sim/TomlIO.h"
#include <filesystem>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace ai_sim;
using namespace ai_sim::ui;
namespace {
void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
} // namespace
int main(int argc, char** argv) {
    auto temporary = std::filesystem::temp_directory_path() /
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    try {
        check(argc == 2, "Repository root required");
        const auto config = std::filesystem::path(argv[1]) / "data/config";
        std::filesystem::create_directories(temporary);
        std::filesystem::copy_file(config / "keybindings.toml", temporary / "keybindings.toml");
        auto document = toml::parse_file((config / "settings.toml").string());
        const auto path = temporary / "settings.toml";
        document["layout"].as_table()->insert_or_assign("response_gap", 2);
        document["pages"].as_table()->insert_or_assign("order", toml::array{"files", "messages"});
        document["pages"].as_table()->insert_or_assign("hidden", toml::array{"news"});
        document["pages"]["labels"].as_table()->insert_or_assign("messages", "Messages");
        document["labels"].as_table()->insert_or_assign("menu_save_and_quit", "Save & exit");
        write_toml(path, document);
        auto settings = UISettings::load(path);
        check(settings.layout.response_gap == 2 && settings.page_order.front() == "files" &&
                  !settings.page_enabled("news"),
              "Presentation fields did not load");
        check(settings.page_title("messages", "Chat") == "Messages" &&
                  settings.menu_label("Save and quit") == "Save & exit",
              "Configurable labels did not apply");
        settings = UISettings::edit(path, false, "layout", "response_gap", "0");
        check(settings.layout.response_gap == 0, "Live layout edit not applied");
        const auto rejected = [&](const std::string& section, const std::string& key,
                                  const std::string& value) {
            bool failed = false;
            try {
                UISettings::edit(path, false, section, key, value);
            } catch (const std::exception&) {
                failed = true;
            }
            if (!failed)
                throw std::runtime_error("Invalid presentation edit was accepted: " + section +
                                         "." + key + " = " + value);
            check(UISettings::load(path).layout.response_gap == 0,
                  "Rejected edit replaced the saved settings");
        };
        rejected("layout", "response_gap", "-1");
        rejected("layout", "response_gap", "1.0");
        rejected("layout", "response_heading_gap", "\"wide\"");
        rejected("layout", "minimum_columns", "40");
        rejected("layout", "list_min_width", "50");
        rejected("layout", "typo_padding", "2");
        rejected("pages", "order", "[\"files\",\"files\"]");
        rejected("pages", "hidden", "[\"messages\"]");
        rejected("pages", "order", "[\"invented\"]");
        rejected("pages.labels", "messages", "\"\"");
        rejected("chrome", "clock", "1");
        rejected("labels", "chat_response", "\"line\\nline\"");
        // Older preference files keep the same defaults without gaining new capabilities.
        document.erase("layout");
        document.erase("pages");
        document.erase("chrome");
        document.erase("labels");
        write_toml(path, document);
        settings = UISettings::load(path);
        check(settings.layout.response_gap == 1 && settings.show_tabs &&
                  settings.page_enabled("news") &&
                  settings.page_title("messages", "Chat") == "Chat",
              "Older settings defaults changed");
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        std::filesystem::remove_all(temporary);
        return 1;
    }
    std::filesystem::remove_all(temporary);
}
