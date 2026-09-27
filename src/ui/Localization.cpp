#include "ui/Localization.h"

#include "util/JsonText.h"
#include "world/PackSource.h"

#include <cctype>
#include <cstdlib>

namespace kestrel::ui {

namespace {

constexpr const char* FallbackLanguage = "en_US";

}

Localization& Localization::shared()
{
    static Localization instance;
    return instance;
}

void Localization::load(std::shared_ptr<world::PackSource> vanilla, const std::string& code)
{
    pack = std::move(vanilla);
    current = code.empty() ? FallbackLanguage : code;
    available.clear();
    if (pack) {
        std::string names;
        if (pack->readArchived("texts", "language_names.json", names) || pack->readText("texts/language_names.json", names)) {
            std::unique_ptr<json::Value> root = json::parse(util::stripJsonComments(names));
            if (root && root->isArray()) {
                for (const std::unique_ptr<json::Value>& entry : root->mArray) {
                    if (entry->isArray() && entry->mArray.size() >= 2 && entry->mArray[0]->isString() && entry->mArray[1]->isString()) {
                        available.push_back({ entry->mArray[0]->mString, entry->mArray[1]->mString });
                    }
                }
            }
        }
    }
    if (available.empty()) {
        available.push_back({ FallbackLanguage, "English (United States)" });
    }
    rebuild();
}

void Localization::setServerPacks(std::vector<std::shared_ptr<const world::PackFiles>> packs)
{
    if (packs == serverPacks) {
        return;
    }
    serverPacks = std::move(packs);
    rebuild();
}

void Localization::setInterfacePack(std::shared_ptr<world::PackSource> interface)
{
    interfacePack = std::move(interface);
    rebuild();
}

void Localization::rebuild()
{
    texts.clear();
    splashTexts.clear();
    std::string splashFile;
    if (pack) {
        pack->readText("splashes.json", splashFile);
    }
    for (auto server = serverPacks.rbegin(); server != serverPacks.rend(); ++server) {
        if (const std::string* file = (*server)->find("splashes.json")) {
            splashFile = *file;
        }
    }
    if (auto root = json::parse(util::stripJsonComments(splashFile)); root && root->isObject()) {
        if (const auto* entries = root->get("splashes"); entries && entries->isArray()) {
            for (const auto& entry : entries->mArray) {
                if (entry->isString() && !entry->mString.empty()) {
                    splashTexts.push_back(entry->mString);
                }
            }
        }
    }
    auto loadLanguage = [&](const std::string& code) {
        std::string path = "texts/" + code + ".lang";
        for (const std::shared_ptr<world::PackSource>& source : { pack, interfacePack }) {
            if (!source) {
                continue;
            }
            std::vector<std::string> layers = source->readTextLayers(path);
            for (auto layer = layers.rbegin(); layer != layers.rend(); ++layer) {
                parseLang(*layer, texts);
            }
        }
        for (auto server = serverPacks.rbegin(); server != serverPacks.rend(); ++server) {
            if (const std::string* text = (*server)->find(path)) {
                parseLang(*text, texts);
            }
        }
    };
    loadLanguage(FallbackLanguage);
    if (current != FallbackLanguage) {
        loadLanguage(current);
    }
}

void Localization::parseLang(const std::string& text, std::unordered_map<std::string, std::string>& out)
{
    size_t position = 0;
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        position = 3;
    }
    while (position < text.size()) {
        size_t end = text.find('\n', position);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string_view line(text.data() + position, end - position);
        position = end + 1;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        size_t equals = line.find('=');
        if (equals == std::string_view::npos || equals == 0) {
            continue;
        }
        std::string_view value = line.substr(equals + 1);
        if (size_t comment = value.find("\t#"); comment != std::string_view::npos) {
            value = value.substr(0, comment);
        }
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
            value.remove_suffix(1);
        }
        out[std::string(line.substr(0, equals))] = std::string(value);
    }
}

bool Localization::has(std::string_view key) const
{
    return texts.find(std::string(key)) != texts.end();
}

std::string Localization::text(std::string_view key, std::string_view fallback) const
{
    auto found = texts.find(std::string(key));
    return found == texts.end() ? std::string(fallback) : found->second;
}

std::string Localization::fill(std::string_view pattern, const std::vector<std::string>& arguments)
{
    std::string out;
    out.reserve(pattern.size());
    size_t next = 0;
    for (size_t i = 0; i < pattern.size(); ++i) {
        char c = pattern[i];
        if (c != '%' || i + 1 >= pattern.size()) {
            out += c;
            continue;
        }
        char kind = pattern[i + 1];
        if (kind == '%') {
            out += '%';
            ++i;
            continue;
        }
        size_t index = next;
        size_t cursor = i + 1;
        if (std::isdigit(static_cast<unsigned char>(kind))) {
            size_t digits = cursor;
            while (digits < pattern.size() && std::isdigit(static_cast<unsigned char>(pattern[digits]))) {
                ++digits;
            }
            if (digits < pattern.size() && pattern[digits] == '$' && digits + 1 < pattern.size()) {
                index = static_cast<size_t>(std::atoi(std::string(pattern.substr(cursor, digits - cursor)).c_str())) - 1;
                cursor = digits + 1;
            } else {
                // Newer screens write a bare %1, which carries no type after it.
                size_t position = static_cast<size_t>(std::atoi(std::string(pattern.substr(cursor, digits - cursor)).c_str()));
                if (position >= 1 && position <= arguments.size()) {
                    out += arguments[position - 1];
                    i = digits - 1;
                } else {
                    out += c;
                }
                continue;
            }
        } else {
            ++next;
        }
        char type = pattern[cursor];
        if (type == 's' || type == 'd' || type == 'i' || type == 'f') {
            out += index < arguments.size() ? arguments[index] : std::string();
            i = cursor;
        } else {
            out += c;
        }
    }
    return out;
}

std::string Localization::format(std::string_view key, std::string_view fallback, const std::vector<std::string>& arguments) const
{
    return fill(text(key, fallback), arguments);
}

std::string Localization::translateMessage(const std::string& message, const std::vector<std::string>& parameters) const
{
    std::vector<std::string> translated;
    translated.reserve(parameters.size());
    for (const std::string& parameter : parameters) {
        std::string key = !parameter.empty() && parameter.front() == '%' ? parameter.substr(1) : parameter;
        translated.push_back(has(key) ? text(key, parameter) : parameter);
    }
    if (has(message)) {
        return fill(text(message, message), translated);
    }
    std::string out;
    size_t i = 0;
    while (i < message.size()) {
        if (message[i] != '%') {
            out += message[i++];
            continue;
        }
        size_t end = i + 1;
        while (end < message.size() && (std::isalnum(static_cast<unsigned char>(message[end])) || message[end] == '.' || message[end] == '_')) {
            ++end;
        }
        std::string key = message.substr(i + 1, end - i - 1);
        if (!key.empty() && has(key)) {
            out += fill(text(key, key), translated);
            i = end;
        } else {
            out += message[i++];
        }
    }
    return parameters.empty() ? out : fill(out, translated);
}

std::string tr(std::string_view key, std::string_view fallback)
{
    return Localization::shared().text(key, fallback);
}

std::string trf(std::string_view key, std::string_view fallback, const std::vector<std::string>& arguments)
{
    return Localization::shared().format(key, fallback, arguments);
}

}
