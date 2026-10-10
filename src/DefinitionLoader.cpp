#include "ai_sim/DefinitionLoader.h"
#include <stdexcept>
#include <unordered_set>
#include <type_traits>

namespace ai_sim {
namespace {
void require(bool ok, const std::string& reason) {
    if (!ok)
        throw std::runtime_error("Definitions: " + reason);
}
void merge(toml::table& target, const toml::table& source, bool overrides) {
    for (const auto& [key, node] : source) {
        auto* old = target.get(key.str());
        if (!old)
            target.insert(key.str(), node);
        else if (old->is_table() && node.is_table())
            merge(*old->as_table(), *node.as_table(), overrides);
        else if (overrides)
            target.insert_or_assign(key.str(), node);
        else if (old->is_array() && node.is_array())
            for (const auto& item : *node.as_array())
                old->as_array()->push_back(item);
        else
            require(old->visit([&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                const auto* other = node.as<T>();
                return other && value == *other;
            }),
                    "Conflicting field '" + std::string(key.str()) + "' in sources");
    }
}
toml::table read(const std::filesystem::path& path, std::unordered_set<std::string>& stack) {
    const auto canonical =
        std::filesystem::weakly_canonical(std::filesystem::absolute(path)).string();
    require(stack.size() < 32, "Source nesting exceeds 32 files");
    require(stack.insert(canonical).second, "Circular source reference: " + canonical);
    auto result = toml::parse_file(canonical);
    if (auto* n = result.get("sources")) {
        require(n->is_array(), canonical + ": sources must be an array of relative filenames");
        const auto sources = *n->as_array();
        result.erase("sources");
        for (const auto& item : sources) {
            const auto name = item.value<std::string>();
            require(name && !name->empty(), canonical + ": empty or invalid source path");
            const auto relative = std::filesystem::path(*name);
            require(!relative.is_absolute(), "Source paths must be relative: " + *name);
            merge(result, read(std::filesystem::path(canonical).parent_path() / relative, stack),
                  false);
        }
    }
    stack.erase(canonical);
    return result;
}
} // namespace
toml::table DefinitionLoader::load(const std::filesystem::path& path) {
    std::unordered_set<std::string> stack;
    return read(path, stack);
}
toml::table DefinitionLoader::computer(const std::filesystem::path& path) {
    auto result = load(path);
    if (auto* processes = result["processes"].as_array())
        for (auto& node : *processes) {
            auto* process = node.as_table();
            require(process, "Process must be a table");
            if (auto* ref = process->get("application")) {
                const auto filename = ref->value<std::string>();
                require(filename && !filename->empty(), "Invalid application reference");
                require(!std::filesystem::path(*filename).is_absolute(),
                        "Application paths must be relative");
                const auto definition = load(path.parent_path() / *filename);
                require(definition["schema_version"].value_or(0) == 1,
                        "Unsupported application schema in " + *filename);
                const auto* defaults = definition["application"].as_table();
                require(defaults, "Missing [application] in " + *filename);
                auto expanded = *defaults;
                merge(expanded, *process, true);
                expanded.erase("application");
                *process = std::move(expanded);
            }
        }
    return result;
}
ScenarioDefinition ScenarioDefinition::load(const std::filesystem::path& path) {
    ScenarioDefinition d;
    d.source = path;
    const auto raw = DefinitionLoader::load(path);
    if (const auto* refs = raw["definitions"].as_table()) {
        require(raw["schema_version"].value_or(0) == 1, "Unsupported scenario manifest version");
        d.manifest = raw;
        auto resolve = [&](const char* key, bool optional = false) {
            const auto name = (*refs)[key].value<std::string>();
            if (optional && !name)
                return std::filesystem::path{};
            require(name && !name->empty(), std::string("Missing scenario reference: ") + key);
            const std::filesystem::path relative = *name;
            require(!relative.is_absolute(), "Scenario paths must be relative");
            const auto resolved = (path.parent_path() / relative).lexically_normal();
            require(std::filesystem::is_regular_file(resolved),
                    "Missing " + std::string(key) + " definition: " + resolved.string());
            return resolved;
        };
        d.computer = resolve("computer");
        d.network = resolve("network");
        d.files = resolve("files");
        d.people = resolve("people");
        d.emails = resolve("emails");
        d.news = resolve("news", true);
        d.workday = resolve("workday", true);
        d.places = resolve("places", true);
        d.economy = resolve("economy", true);
        d.stock_market = resolve("stock_market", true);
        d.infrastructure = resolve("infrastructure", true);
        if (!d.places.empty()) {
            const auto places = DefinitionLoader::load(d.places);
            const auto* records = places["places"].as_array();
            require(records, "Places catalog needs [[places]] records");
            std::unordered_set<std::string> ids;
            for (const auto& n : *records) {
                const auto* t = n.as_table();
                require(t, "Invalid place");
                const auto id = (*t)["id"].value<std::string>();
                require(id && !id->empty() && ids.insert(*id).second,
                        "Invalid or duplicate place ID");
            }
        }
    } else {
        // Legacy standalone computer files still load their neighboring definitions.
        require(raw["computer"].is_table(),
                "Expected a scenario manifest or legacy computer definition");
        const auto folder = path.parent_path();
        d.computer = path;
        d.network = folder / "network.toml";
        d.files = folder / "files.toml";
        d.people = folder / "people.toml";
        d.emails = folder / "emails.toml";
        d.manifest = DefinitionLoader::load(folder / "scenario.toml");
        if (std::filesystem::exists(folder / "workday.toml"))
            d.workday = folder / "workday.toml";
        if (std::filesystem::exists("data/news.toml"))
            d.news = "data/news.toml";
    }
    return d;
}
} // namespace ai_sim
