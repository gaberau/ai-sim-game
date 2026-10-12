#include "ai_sim/ui/TerminalUI.h"
#include "ai_sim/ui/ScreenRenderer.h"

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
    file_category_ = "all";
    file_query_.clear();
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
    selected_file_path_ = absolute;
    file_query_.clear();
    file_details_focused_ = false;
    file_list_offset_ = 0;
    file_content_offset_ = 0;
    selected_tab_ = Tab::Files;
}

void TerminalUI::select_file_category(int direction) {
    const std::vector<std::string> categories{"all", "work", "personal", "hidden"};
    const auto found = std::find(categories.begin(), categories.end(), file_category_);
    const int index = (static_cast<int>(found - categories.begin()) + direction + 4) % 4;
    file_category_ = categories[index];
    selected_file_path_.clear();
    file_list_offset_ = file_content_offset_ = 0;
}
void TerminalUI::select_file(int direction) {
    const auto entries = file_entries();
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
    selected_file_path_ = entries[index]->path;
    file_content_offset_ = 0;
}
void TerminalUI::open_selected_file() {
    if (selected_file_path_.empty())
        select_file(0);
    if (selected_file_path_.empty())
        return;
    const auto& entry =
        file_system_.inspect(network_, file_identity(), file_host_id_, selected_file_path_);
    if (entry.directory) {
        change_file_directory(entry.path);
        return;
    }
    (void)file_system_.read(network_, file_identity(), file_host_id_, selected_file_path_);
    file_details_focused_ = true;
    file_content_offset_ = 0;
}
void TerminalUI::create_category_file(const std::string& name) {
    if (name.empty() || name == "." || name == ".." ||
        name.find_first_of("/\\") != std::string::npos)
        throw std::runtime_error("Enter a file name without a path");
    const auto actor = file_identity();
    const auto host = file_host_id_;
    auto candidate = file_system_;
    candidate.enter(network_, actor, host, file_directory_);
    const auto path = FileSystem::normalize(file_directory_ + "/" + name);
    try {
        (void)candidate.inspect(network_, actor, host, path);
        throw std::runtime_error("A file with that name already exists");
    } catch (const std::exception& e) {
        if (std::string{e.what()}.find("No such file") == std::string::npos)
            throw;
    }
    candidate.write(network_, actor, host, path, "", computer_.time_seconds());
    if (file_category_ != "all")
        candidate.set_category(network_, actor, host, path, file_category_);
    file_system_ = std::move(candidate);
    file_host_id_ = host;
    file_directory_ = FileSystem::parent(path);
    selected_file_path_ = path;
    file_query_.clear();
    edit_file(path);
}

std::pair<int, std::string> TerminalUI::file_access(const FileEntry& entry) const {
    try {
        if (entry.directory)
            file_system_.enter(network_, file_identity(), entry.host_id, entry.path);
        else
            (void)file_system_.read(network_, file_identity(), entry.host_id, entry.path);
        if (!entry.service_kind.empty())
            return {FileRead, "Managed service"};
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

std::vector<const FileEntry*> TerminalUI::file_entries() const {
    std::vector<const FileEntry*> entries;
    if (!file_query_.empty()) {
        entries = file_system_.search(network_, file_identity(), file_host_id_, file_directory_,
                                      file_query_);
    } else {
        entries.push_back(&file_system_.inspect(network_, file_identity(), file_host_id_, "/"));
        const auto walk = [&](const auto& self, const std::string& directory) -> void {
            try {
                for (const auto* entry :
                     file_system_.list(network_, file_identity(), file_host_id_, directory)) {
                    entries.push_back(entry);
                    if (entry->directory && (entry->path == file_directory_ ||
                                             file_directory_.starts_with(entry->path + "/")))
                        self(self, entry->path);
                }
            } catch (const std::exception&) {
            }
        };
        walk(walk, "/");
    }
    std::erase_if(entries, [&](const auto* entry) {
        return !entry->directory && file_category_ != "all" &&
               FileSystem::category_of(*entry) != file_category_;
    });
    return entries;
}
void TerminalUI::open_file_hosts() {
    std::vector<MenuAction> actions;
    for (const auto& host : network_.hosts()) {
        if (!host.known)
            continue;
        std::string reason;
        try {
            file_system_.enter(network_, file_identity(), host.id, "/");
        } catch (const std::exception& error) {
            reason = error.what();
        }
        actions.push_back({host.name + " | " + host.id,
                           [this, id = host.id] {
                               switch_file_host(id);
                           },
                           reason});
    }
    open_menu("Choose file host", std::move(actions));
}
void TerminalUI::draw_files(int top, int bottom, int width) {
    const auto host =
        std::find_if(network_.hosts().begin(), network_.hosts().end(), [&](const auto& h) {
            return h.id == file_host_id_;
        });
    write(
        top, 1,
        fit("FILES | " + (host == network_.hosts().end() ? file_host_id_ : host->name), width - 2),
        Heading, true);
    std::string breadcrumbs = "Path: /";
    std::istringstream path(file_directory_);
    std::string part;
    while (std::getline(path, part, '/'))
        if (!part.empty())
            breadcrumbs += " > " + part;
    write(top + 1, 1, fit(breadcrumbs, width - 2), Resources);
    std::string filters = "Filter: ";
    for (const auto* category : {"all", "work", "personal", "hidden"})
        filters += file_category_ == category ? "[" + std::string(category) + "] "
                                              : std::string(category) + " ";
    if (!file_query_.empty())
        filters += "| Search: " + file_query_;
    write(top + 2, 1, fit(filters, width - 2), Footer);
    const int body = top + 4, divider = ScreenRenderer::list_divider(width), right = divider + 2;
    const int content_width = std::max(1, width - right - 2);
    renderer_->split(body, bottom - 1, divider);
    for (int row = body; row < bottom; ++row)
        line_cell(row, divider, '|', Border);
    write(bottom, 1,
          fit(settings_.binding_label(KeyAction::Submit) + ": actions | " +
                  settings_.binding_label(KeyAction::ScrollLeft) + "/" +
                  settings_.binding_label(KeyAction::ScrollRight) + ": filter | " +
                  settings_.binding_label(KeyAction::ToggleExpanded) + ": open | F5: help",
              width - 2),
          Footer);
    std::vector<const FileEntry*> entries;
    try {
        entries = file_entries();
    } catch (const std::exception& error) {
        write(body, 1, fit(error.what(), width - 2), FileBlocked);
        return;
    }
    if (entries.empty()) {
        selected_file_path_.clear();
        write(body, 1, "No matching files.");
        return;
    }
    auto selected = std::find_if(entries.begin(), entries.end(), [&](const auto* entry) {
        return entry->path == selected_file_path_;
    });
    if (selected == entries.end()) {
        selected = entries.begin();
        selected_file_path_ = (*selected)->path;
    }
    const auto index = static_cast<int>(selected - entries.begin());
    const int visible = std::max(1, bottom - body);
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
        const auto [color, access] = file_access(entry);
        const auto depth =
            entry.path == "/"
                ? 0
                : static_cast<int>(std::count(entry.path.begin(), entry.path.end(), '/')) - 1;
        const auto name = entry.path == "/" ? "/" : entry.path.substr(entry.path.rfind('/') + 1);
        const auto label = !file_query_.empty() ? entry.path
                                                : std::string(std::min(depth * 2, 10), ' ') +
                                                      (entry.directory              ? "+ "
                                                       : entry.service_kind.empty() ? "  "
                                                                                    : "* ") +
                                                      name;
        write(body + line, 1, fit(std::string(item == index ? "> " : "  ") + label, divider - 2),
              color, item == index && !file_details_focused_);
    }
    const auto& entry = **selected;
    const auto [color, access] = file_access(entry);
    write(body, right, fit(entry.path, content_width), Heading, true);
    write(body + 1, right,
          fit(entry.owner + " | " + FileSystem::mode_text(entry.mode) + " | " +
                  std::to_string(entry.size_bytes) + " B",
              content_width),
          Resources);
    write(body + 2, right, fit(access, content_width), color);
    if (!entry.service_kind.empty()) {
        write(body + 4, right, fit("Managed " + entry.service_kind + " storage", content_width),
              Heading);
        write(body + 5, right,
              fit(entry.service_kind == "agent"
                      ? "Manage through the Agents tab."
                      : entry.service_kind == "work" ? "Manage through the Money tab."
                      : "Manage through Operations > Backup and Recovery.",
                  content_width));
        return;
    }
    try {
        std::vector<std::string> lines;
        if (entry.directory) {
            for (const auto* child :
                 file_system_.list(network_, file_identity(), file_host_id_, entry.path))
                lines.push_back(std::string(child->directory ? "+ " : "  ") +
                                child->path.substr(child->path.rfind('/') + 1));
            if (lines.empty())
                lines.push_back("(empty folder)");
        } else
            lines = content_lines(
                file_system_.read(network_, file_identity(), file_host_id_, entry.path),
                content_width);
        const int readable = std::max(0, bottom - body - 4);
        file_content_offset_ = std::clamp(file_content_offset_, 0,
                                          std::max(0, static_cast<int>(lines.size()) - readable));
        std::vector<ScreenRenderer::TextRow> preview;
        preview.reserve(lines.size());
        for (const auto& line : lines)
            preview.push_back({fit(line, content_width)});
        renderer_->scroll_text("files", preview, file_content_offset_, body + 4, right,
                          readable, content_width);
    } catch (const std::exception& error) {
        write(body + 4, right, fit(error.what(), content_width), FileBlocked);
    }
}
} // namespace ai_sim::ui
