#include "ai_sim/ui/TerminalUI.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace ai_sim::ui {
namespace {

std::string fit(std::string value, int width) {
    if (width <= 0)
        return "";
    if (value.size() > static_cast<std::size_t>(width)) {
        value = value.substr(0, width - 1) + "~";
    }
    return value;
}

std::vector<std::string> content_lines(const std::string& content, int width) {
    std::vector<std::string> result;
    std::istringstream input(content);
    std::string line;
    const auto length = static_cast<std::size_t>(std::max(1, width));
    while (std::getline(input, line)) {
        if (line.empty())
            result.emplace_back();
        else {
            for (std::size_t start = 0; start < line.size(); start += length) {
                result.push_back(line.substr(start, length));
            }
        }
    }
    if (result.empty())
        result.emplace_back("(empty file)");
    return result;
}

} // namespace

void TerminalUI::switch_file_host(const std::string& host) {
    // Validate before changing the UI so failed access preserves the location.
    file_system_.enter(network_, file_identity(), host, "/");
    file_host_id_ = host;
    file_directory_ = "/";
    file_category_ = "work";
    selected_file_path_.clear();
    file_list_offset_ = 0;
    file_content_offset_ = 0;
    selected_tab_ = Tab::Files;
}

void TerminalUI::change_file_directory(const std::string& path) {
    const auto absolute = FileSystem::normalize(
        !path.empty() && path.front() == '/' ? path : file_directory_ + "/" + path);
    file_system_.enter(network_, file_identity(), file_host_id_, absolute);
    file_directory_ = absolute;
    selected_file_path_.clear();
    file_list_offset_ = 0;
    file_content_offset_ = 0;
    selected_tab_ = Tab::Files;
}

void TerminalUI::select_file_category(int direction) {
    const std::vector<std::string> categories{"work", "personal", "hidden"};
    const auto found = std::find(categories.begin(), categories.end(), file_category_);
    const int index = (static_cast<int>(found - categories.begin()) + direction + 3) % 3;
    file_category_ = categories[index];
    selected_file_path_.clear();
    file_list_offset_ = file_content_offset_ = 0;
}
void TerminalUI::select_file(int direction) {
    const auto entries = file_system_.category_files(network_, file_identity(), file_category_);
    if (entries.empty()) {
        selected_file_path_.clear();
        return;
    }
    const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto* e) {
        return e->host_id == file_host_id_ && e->path == selected_file_path_;
    });
    const int index = found == entries.end()
                          ? 0
                          : std::clamp(static_cast<int>(found - entries.begin()) + direction, 0,
                                       static_cast<int>(entries.size()) - 1);
    file_host_id_ = entries[index]->host_id;
    selected_file_path_ = entries[index]->path;
    file_directory_ = FileSystem::parent(selected_file_path_);
    file_content_offset_ = 0;
}
void TerminalUI::open_selected_file() {
    if (selected_file_path_.empty())
        select_file(0);
    if (selected_file_path_.empty())
        return;
    (void)file_system_.read(network_, file_identity(), file_host_id_, selected_file_path_);
    file_details_focused_ = true;
    file_content_offset_ = 0;
}
void TerminalUI::create_category_file(const std::string& name) {
    if (name.empty() || name == "." || name == ".." ||
        name.find_first_of("/\\") != std::string::npos)
        throw std::runtime_error("Enter a file name without a path");
    // All player-created files have a writable home. Category never grants permissions.
    const auto actor = file_identity();
    const auto host = network_.hosts()[network_.local_host_index()].id;
    const std::string base = "/home/" + actor.user + "/assistant";
    auto candidate = file_system_;
    for (const auto& directory : std::vector<std::string>{"/home", "/home/" + actor.user, base,
                                                          base + "/" + file_category_}) {
        try {
            candidate.enter(network_, actor, host, directory);
        } catch (const std::exception&) {
            candidate.mkdir(network_, actor, host, directory, computer_.time_seconds());
        }
    }
    const auto path = base + "/" + file_category_ + "/" + name;
    try {
        (void)candidate.inspect(network_, actor, host, path);
        throw std::runtime_error("A file with that name already exists");
    } catch (const std::exception& e) {
        if (std::string{e.what()}.find("No such file") == std::string::npos)
            throw;
    }
    candidate.write(network_, actor, host, path, "", computer_.time_seconds());
    candidate.set_category(network_, actor, host, path, file_category_);
    file_system_ = std::move(candidate);
    file_host_id_ = host;
    file_directory_ = FileSystem::parent(path);
    selected_file_path_ = path;
    edit_file(path);
}

std::pair<int, std::string> TerminalUI::file_access(const FileEntry& entry) const {
    try {
        if (entry.directory)
            file_system_.enter(network_, file_identity(), entry.host_id, entry.path);
        else
            (void)file_system_.read(network_, file_identity(), entry.host_id, entry.path);
        try {
            file_system_.validate_write(network_, file_identity(), entry.host_id,
                                        entry.directory ? entry.path + "/.ai-sim-access-probe"
                                                        : entry.path);
            return {FileEdit, "Read + edit"};
        } catch (const std::exception&) {
            return {FileRead, "Read only"};
        }
    } catch (const std::exception& e) {
        if (std::string{e.what()}.find("Permission denied") != std::string::npos) {
            if (auto* requests = people_["work_requests"].as_array())
                for (const auto& n : *requests) {
                    const auto& r = *n.as_table();
                    if (r["host_id"].value_or(std::string{}) == entry.host_id &&
                        r["path"].value_or(std::string{}) == entry.path &&
                        r["access_status"].value_or(std::string{}) != "denied")
                        return {FileApproval, "Ask owner"};
                }
            return {FileBlocked, "Blocked"};
        }
        return {FileUnknown, "Unavailable"};
    }
}

void TerminalUI::draw_files(int top, int bottom, int width) {
    std::string categories = "FILES | ";
    for (const auto& category : std::vector<std::string>{"work", "personal", "hidden"}) {
        std::string label = category;
        label[0] = static_cast<char>(std::toupper(label[0]));
        categories += category == file_category_ ? "[" + label + "]  " : label + "  ";
    }
    write(top, 1, categories, Heading, true);
    const std::string controls = settings_.binding_label(KeyAction::ScrollLeft) + "/" +
                                 settings_.binding_label(KeyAction::ScrollRight) + ": category | " +
                                 settings_.binding_label(KeyAction::FocusLeft) + "/" +
                                 settings_.binding_label(KeyAction::FocusRight) + ": panes | " +
                                 settings_.binding_label(KeyAction::SelectPrevious) + "/" +
                                 settings_.binding_label(KeyAction::SelectNext) + ": select/scroll";
    write(bottom, 1, fit(controls, width - 2), Footer);
    const int divider = std::clamp(width / 3, 26, 42), right = divider + 2,
              content_width = std::max(1, width - right - 2);
    for (int row = top + 1; row < bottom; ++row)
        write(row, divider, "|", Border);
    const auto entries = file_system_.category_files(network_, file_identity(), file_category_);
    if (entries.empty()) {
        selected_file_path_.clear();
        write(top + 2, 2, "No known files in this category.");
        return;
    }
    auto selected = std::find_if(entries.begin(), entries.end(), [&](const auto* e) {
        return e->host_id == file_host_id_ && e->path == selected_file_path_;
    });
    if (selected == entries.end()) {
        select_file(0);
        selected = entries.begin();
    }
    const int index = static_cast<int>(selected - entries.begin()),
              visible = std::max(1, bottom - top - 1);
    if (index < file_list_offset_)
        file_list_offset_ = index;
    if (index >= file_list_offset_ + visible)
        file_list_offset_ = index - visible + 1;
    file_list_offset_ =
        std::clamp(file_list_offset_, 0, std::max(0, static_cast<int>(entries.size()) - visible));
    for (int line = 0;
         line < visible && file_list_offset_ + line < static_cast<int>(entries.size()); ++line) {
        const int item = file_list_offset_ + line;
        const auto& entry = *entries[item];
        const auto [color, label] = file_access(entry);
        write(top + 1 + line, 1,
              fit(std::string(item == index ? "> " : "  ") +
                      entry.path.substr(entry.path.rfind('/') + 1),
                  divider - 2),
              color, item == index && !file_details_focused_);
    }
    const auto& entry = **selected;
    const auto [color, label] = file_access(entry);
    write(top + 1, right, fit(entry.path.substr(entry.path.rfind('/') + 1), content_width), Heading,
          true);
    write(top + 2, right,
          fit(entry.host_id + " | " + entry.owner + " | " + std::to_string(entry.size_bytes) + " B",
              content_width),
          Resources);
    write(top + 3, right, fit(label, content_width), color, true);
    try {
        const auto lines = content_lines(
            file_system_.read(network_, file_identity(), entry.host_id, entry.path), content_width);
        const int readable = std::max(0, bottom - top - 5);
        file_content_offset_ = std::clamp(file_content_offset_, 0,
                                          std::max(0, static_cast<int>(lines.size()) - readable));
        for (int i = 0; i < readable && file_content_offset_ + i < static_cast<int>(lines.size());
             ++i)
            write(top + 5 + i, right, lines[file_content_offset_ + i]);
    } catch (const std::exception&) {
        write(top + 5, right, "Access required to read this file.", FileBlocked);
    }
}

} // namespace ai_sim::ui
