#pragma once
#include "ai_sim/gui/GameSession.h"
#include "ai_sim/EconomySystem.h"
#include <stdexcept>
#include <cmath>

namespace ai_sim::gui::presentation {
inline QString q(const std::string& s) {
    return QString::fromStdString(s);
}
inline std::string str(const toml::table& t, const char* key) {
    return t[key].value_or(std::string{});
}
inline QString money(std::int64_t cents) {
    return (cents < 0 ? "-" : "") + q(EconomySystem::money(cents < 0 ? -cents : cents));
}
// Render saved public records as readable labels, never expose private person state.
inline QString recordText(const toml::node& node, int depth = 0) {
    QString result;
    if (const auto* table = node.as_table()) {
        for (const auto& [key, value] : *table) {
            QString label = q(std::string(key.str())).replace('_', ' ');
            if (!label.isEmpty())
                label[0] = label[0].toUpper();
            const auto indentation = QString(depth * 2, ' ');
            if (value.is_table() || value.is_array())
                result += indentation + label + ":\n" + recordText(value, depth + 1);
            else {
                QString content;
                if (auto text = value.value<std::string>())
                    content = q(*text);
                else if (auto integer = value.value<std::int64_t>())
                    content = label.endsWith("cents") ? money(*integer) : QString::number(*integer);
                else if (auto number = value.value<double>())
                    content = QString::number(*number);
                else if (auto flag = value.value<bool>())
                    content = *flag ? "Yes" : "No";
                result += indentation + label + ": " + content + "\n";
            }
        }
    } else if (const auto* array = node.as_array()) {
        for (const auto& entry : *array) {
            if (entry.is_table() || entry.is_array())
                result += recordText(entry, depth) + "\n";
            else if (auto text = entry.value<std::string>())
                result += QString(depth * 2, ' ') + q(*text) + "\n";
        }
    }
    return result;
}
inline QVariantMap row(QString id, QString title, QString subtitle, QString body = {}) {
    return {{"id", id}, {"title", title}, {"subtitle", subtitle}, {"body", body}};
}
inline QVariantMap field(QString id, QString label, QString value = {}, bool multiline = false) {
    return {{"id", id}, {"label", label}, {"value", value}, {"multiline", multiline}};
}
inline QVariantMap selectField(QString id, QString label, QString value, QVariantList options) {
    auto result = field(id, label, value);
    result["options"] = options;
    return result;
}
inline QVariantMap action(QString id, QString label, QString reason = {}, QVariantList fields = {},
                          bool destructive = false) {
    return {{"id", id},         {"label", label},
            {"reason", reason}, {"enabled", reason.isEmpty()},
            {"fields", fields}, {"confirm", destructive}};
}
inline QString capability(const toml::table& people, const char* id) {
    return ChatSystem::available(people, id) ? QString{} : "Research this capability first.";
}
inline std::int64_t integer(const QVariantMap& args, const char* key, std::int64_t low,
                            std::int64_t high) {
    bool ok = false;
    const auto result = args.value(key).toString().toLongLong(&ok);
    if (!ok || result < low || result > high)
        throw std::runtime_error(std::string(key) + ": enter an integer in range " +
                                 std::to_string(low) + "–" + std::to_string(high));
    return result;
}
inline double decimal(const QVariantMap& args, const char* key, double low, double high) {
    bool ok = false;
    const auto result = args.value(key).toString().toDouble(&ok);
    if (!ok || !std::isfinite(result) || result < low || result > high)
        throw std::runtime_error(std::string(key) + ": invalid amount.");
    return result;
}
} // namespace ai_sim::gui::presentation
