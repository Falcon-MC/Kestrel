#pragma once

#include "world/ServerPack.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace json {
struct Value;
}

namespace kestrel::world {
class PackSource;
}

namespace kestrel::ui {

struct LanguageInfo {
    std::string code;
    std::string name;
};

/**
 * The game's translated texts: the .lang files of the vanilla packs and of
 * the server's packs for the chosen language, over United States English
 * for keys the language lacks.
 */
class Localization {
public:
    static Localization& shared();

    void load(std::shared_ptr<world::PackSource> vanilla, const std::string& code);
    void setServerPacks(std::vector<std::shared_ptr<const world::PackFiles>> packs);

    /**
     * The oreui pack, where the texts of the HTML menus live under hbui keys.
     */
    void setInterfacePack(std::shared_ptr<world::PackSource> interface);

    const std::string& code() const
    {
        return current;
    }

    const std::vector<LanguageInfo>& languages() const
    {
        return available;
    }

    bool has(std::string_view key) const;
    const std::vector<std::string>& splashes() const { return splashTexts; }

    /**
     * The text of a key, or the fallback when no loaded language has it.
     */
    std::string text(std::string_view key, std::string_view fallback) const;

    /**
     * The text of a key with its %s, %1$s, %1, %d and %% placeholders filled
     * from the arguments in order or by position.
     */
    std::string format(std::string_view key, std::string_view fallback, const std::vector<std::string>& arguments) const;

    /**
     * A message as servers send it: every %key it contains replaced by its
     * text, the parameters filling the placeholders, and parameters that are
     * keys themselves translated too.
     */
    std::string translateMessage(const std::string& message, const std::vector<std::string>& parameters = {}) const;

private:
    void rebuild();
    static void parseLang(const std::string& text, std::unordered_map<std::string, std::string>& out);
    static std::string fill(std::string_view pattern, const std::vector<std::string>& arguments);

    std::shared_ptr<world::PackSource> pack;
    std::shared_ptr<world::PackSource> interfacePack;
    std::vector<std::shared_ptr<const world::PackFiles>> serverPacks;
    std::string current = "en_US";
    std::vector<LanguageInfo> available;
    std::unordered_map<std::string, std::string> texts;
    std::vector<std::string> splashTexts;
};

/**
 * The translated text of a key, or fallback when the key is unknown.
 */
std::string tr(std::string_view key, std::string_view fallback);

std::string trf(std::string_view key, std::string_view fallback, const std::vector<std::string>& arguments);

/**
 * The text a rawtext component spells out, translate components translated.
 */
std::string rawText(const json::Value& component);

}
