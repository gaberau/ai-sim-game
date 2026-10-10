#pragma once

#include <toml++/toml.hpp>
#include <filesystem>

namespace ai_sim {
void write_toml(const std::filesystem::path& path, const toml::table& data);
void sync_directory(const std::filesystem::path& path);
}
