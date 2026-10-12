// Module: Write TOML snapshots durably; filesystem synchronization supports atomic save
// publication.
#pragma once

#include <toml++/toml.hpp>
#include <filesystem>

namespace ai_sim {
// Write and synchronize a TOML file so its caller can safely publish the snapshot.
void write_toml(const std::filesystem::path& path, const toml::table& data);
// Synchronize directory metadata after renames where the platform supports it.
void sync_directory(const std::filesystem::path& path);
} // namespace ai_sim
