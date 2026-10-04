#include "modding/ModConfig.h"

#include "client/DebugLog.h"

#include <fstream>

namespace kestrel::modding {

namespace {

// Keys and values stay on one line each; a backslash keeps line breaks, '='
// in keys and itself.
std::string escape(std::string_view text, bool key)
{
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (c == '\\') {
            out += "\\\\";
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else if (c == '=' && key) {
            out += "\\=";
        } else {
            out += c;
        }
    }
    return out;
}

std::string unescape(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            char next = text[++i];
            out += next == 'n' ? '\n' : next == 'r' ? '\r' : next;
        } else {
            out += text[i];
        }
    }
    return out;
}

size_t separatorOf(std::string_view line)
{
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\\') {
            ++i;
        } else if (line[i] == '=') {
            return i;
        }
    }
    return std::string_view::npos;
}

}

ModConfig::ModConfig(std::filesystem::path file)
    : file(std::move(file))
{
    load();
}

std::optional<std::string> ModConfig::find(std::string_view key) const
{
    auto found = values.find(key);
    if (found == values.end()) {
        return std::nullopt;
    }
    return found->second;
}

void ModConfig::put(std::string_view key, std::string value)
{
    auto found = values.find(key);
    if (found == values.end()) {
        values.emplace(std::string(key), std::move(value));
        changed = true;
    } else if (found->second != value) {
        found->second = std::move(value);
        changed = true;
    }
}

void ModConfig::remove(std::string_view key)
{
    auto found = values.find(key);
    if (found != values.end()) {
        values.erase(found);
        changed = true;
    }
}

std::vector<std::string> ModConfig::keys() const
{
    std::vector<std::string> names;
    names.reserve(values.size());
    for (const auto& [key, value] : values) {
        names.push_back(key);
    }
    return names;
}

void ModConfig::save()
{
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        debugLog("mods: could not write " + file.string());
        return;
    }
    for (const auto& [key, value] : values) {
        out << escape(key, true) << '=' << escape(value, false) << '\n';
    }
    changed = false;
}

void ModConfig::saveIfChanged()
{
    if (changed) {
        save();
    }
}

void ModConfig::load()
{
    values.clear();
    changed = false;
    std::ifstream in(file, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        size_t separator = separatorOf(line);
        if (line.empty() || line.front() == '#' || separator == std::string::npos) {
            continue;
        }
        values[unescape(std::string_view(line).substr(0, separator))] = unescape(std::string_view(line).substr(separator + 1));
    }
}

}
