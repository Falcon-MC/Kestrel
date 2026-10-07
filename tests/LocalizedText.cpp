#include "client/ChatText.h"
#include "ui/Localization.h"
#include "world/PackSource.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

void require(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

int main()
{
    namespace fs = std::filesystem;
    const auto directory = fs::temp_directory_path() / ("kestrel-localized-text-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(directory / "texts");
    { std::ofstream lang(directory / "texts/en_US.lang"); lang << "audit.mode=Mode: %s\naudit.creative=Creative\naudit.join=%s connected\n"; }
    auto& localization = kestrel::ui::Localization::shared();
    localization.load(std::make_shared<kestrel::world::PackSource>(directory), "en_US");
    fs::remove_all(directory);
    for (const auto* arguments : { R"(["%audit.creative"])", R"({"rawtext":[{"text":"%audit.creative"}]})" }) {
        const std::string input = std::string(R"({"rawtext":[{"translate":"audit.mode","with":)") + arguments + R"(},{"text":" | 100% literal %audit.creative"}]})";
        require(kestrel::rawText(input) == "Mode: Creative | 100% literal %audit.creative", "Translated rawtext arguments must resolve keys while literal text stays intact");
    }
    for (auto kind : { kestrel::ChatMessage::Kind::Raw, kestrel::ChatMessage::Kind::System, kestrel::ChatMessage::Kind::Popup }) {
        kestrel::ChatMessage message;
        message.kind = kind;
        message.translate = true;
        message.message = "\xC2\xA7" "e%audit.join";
        message.parameters = { "Alex" };
        require(kestrel::messageBody(message) == "\xC2\xA7" "eAlex connected", "Marked text packet keys must resolve across chat and popup surfaces");
    }
    require(localization.translateMessage("audit.mode", { "%audit.creative" }) == "Mode: Creative", "Command arguments must resolve their marked keys");
    for (const std::string& literal : { "audit.creative", "Alex %audit.creative", "%audit.unknown", "%audit.creative extra" }) {
        require(localization.translateMessage("audit.mode", { literal }) == "Mode: " + literal, "Unmarked, embedded and unknown argument keys must stay literal");
        const std::string input = std::string(R"({"translate":"audit.mode","with":[")") + literal + R"("]})";
        require(kestrel::rawText(input) == "Mode: " + literal, "Rawtext arguments must preserve literal player names");
    }
    require(kestrel::rawText(R"({"rawtext":[{"text":"%audit.creative"}]})") == "%audit.creative", "Literal rawtext must not be localized");
    kestrel::ChatMessage announcement;
    announcement.kind = kestrel::ChatMessage::Kind::Announcement;
    announcement.source = "Alex";
    require(kestrel::chatLine(announcement, "[Alex] hello") == "[Alex] hello", "Server-formatted announcements must not repeat the sender");
    require(kestrel::chatLine(announcement, "hello") == "[Alex] hello", "Unformatted announcements must keep their sender");
    announcement.source.clear();
    require(kestrel::chatLine(announcement, "[Server] hello") == "[Server] hello", "Source-less announcements must stay intact");

}
