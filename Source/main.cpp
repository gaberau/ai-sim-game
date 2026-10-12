#include "ai_sim/Computer.h"
#include "ai_sim/DefinitionLoader.h"
#include "ai_sim/ui/TerminalUI.h"
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        std::filesystem::path computer_path = "data/scenarios/hobbyist.toml";
        bool fresh = false, custom = false, migrate_only = false;
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--new")
                fresh = true;
            else if (argument == "--migrate-data")
                migrate_only = true;
            else if (!custom && !argument.empty() && argument.front() != '-') {
                computer_path = argument;
                custom = true;
            } else
                throw std::runtime_error(
                    "Usage: ai_sim [scenario.toml or legacy computer.toml] [--new]");
        }
        if ((!custom || migrate_only) && ai_sim::DefinitionLoader::migrate_legacy("data"))
            std::cout << "Data reorganized. Previous definitions are preserved in backups/.\n";
        if (migrate_only)
            return 0;
        ai_sim::Computer computer;
        ai_sim::ui::TerminalUI interface(computer, "data/config/settings.toml", computer_path,
                                         !fresh && !custom);
        interface.run();
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
