#include "ai_sim/TomlIO.h"
#include <fstream>
#include <stdexcept>
#ifdef __unix__
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ai_sim {
void write_toml(const std::filesystem::path& path, const toml::table& data) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Cannot write " + path.string());
    output << data << '\n';
    output.flush();
    if (!output) throw std::runtime_error("Write failed: " + path.string());
    output.close();
    if (!output) throw std::runtime_error("Close failed: " + path.string());
#ifdef __unix__
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) throw std::runtime_error("Cannot sync " + path.string());
    const int result = ::fsync(fd);
    ::close(fd);
    if (result != 0) throw std::runtime_error("Sync failed: " + path.string());
#endif
}

void sync_directory(const std::filesystem::path& path) {
#ifdef __unix__
    const int fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd < 0) throw std::runtime_error("Cannot sync directory " + path.string());
    const int result = ::fsync(fd);
    ::close(fd);
    if (result != 0) throw std::runtime_error("Directory sync failed: " + path.string());
#else
    (void)path;
#endif
}
}
