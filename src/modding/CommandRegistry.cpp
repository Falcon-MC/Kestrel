#include "modding/CommandRegistry.h"

#include <algorithm>
#include <cctype>

namespace kestrel::modding {

namespace {

bool sameName(std::string_view left, std::string_view right)
{
    return std::equal(left.begin(), left.end(), right.begin(), right.end(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
}

bool startsWithName(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && sameName(text.substr(0, prefix.size()), prefix);
}

}

CommandRegistry::~CommandRegistry()
{
    for (const std::shared_ptr<Entry>& entry : entries) {
        entry->handle->expire();
    }
}

mod::Subscription CommandRegistry::add(size_t owner, mod::CommandSpec spec, mod::Commands::Handler handler)
{
    auto entry = std::make_shared<Entry>();
    entry->owner = owner;
    entry->spec = std::move(spec);
    entry->handler = std::move(handler);
    entry->handle = std::make_shared<Handle>([this, raw = entry.get()] { remove(raw); });
    entries.push_back(entry);
    return mod::Subscription(entry->handle);
}

bool CommandRegistry::execute(std::string_view line, mod::Chat& chat, const ErrorSink& errors)
{
    if (line.size() < 2 || line.front() != mod::Commands::Prefix) {
        return false;
    }
    std::string_view body = line.substr(1);
    size_t space = body.find(' ');
    std::string_view label = body.substr(0, space);
    std::shared_ptr<Entry> entry = find(label);
    if (!entry) {
        return false;
    }
    std::string_view rest = space == std::string_view::npos ? std::string_view() : body.substr(space + 1);
    mod::CommandContext context { std::string(label), splitArguments(rest), std::string(rest), chat };
    try {
        entry->handler(context);
    } catch (const mod::CommandError& failure) {
        chat.print(std::string("§c") + failure.what());
    } catch (const std::exception& failure) {
        chat.print("§cThe command failed, see debug.txt");
        errors(entry->owner, failure.what());
    } catch (...) {
        chat.print("§cThe command failed, see debug.txt");
        errors(entry->owner, "unknown exception");
    }
    return true;
}

CommandRegistry::Completions CommandRegistry::complete(std::string_view line, mod::Chat& chat, const ErrorSink& errors) const
{
    Completions found;
    if (line.empty() || line.front() != mod::Commands::Prefix) {
        return found;
    }
    std::string_view body = line.substr(1);
    size_t space = body.find(' ');
    if (space == std::string_view::npos) {
        found.replaceFrom = 1;
        for (const std::shared_ptr<const Entry>& entry : list()) {
            if (found.suggestions.size() >= MaxSuggestions) {
                break;
            }
            if (startsWithName(entry->spec.name, body)) {
                found.suggestions.emplace_back(entry->spec.name, entry->spec.description);
            }
            for (const std::string& alias : entry->spec.aliases) {
                if (startsWithName(alias, body)) {
                    found.suggestions.emplace_back(alias, entry->spec.description);
                }
            }
        }
        return found;
    }
    std::string_view label = body.substr(0, space);
    std::shared_ptr<Entry> entry = find(label);
    if (!entry) {
        return found;
    }
    if (!entry->spec.usage.empty()) {
        found.usage.push_back(std::string(1, mod::Commands::Prefix) + entry->spec.name + " " + entry->spec.usage);
    }
    if (!entry->spec.complete) {
        return found;
    }
    std::string_view rest = body.substr(space + 1);
    size_t lastSpace = rest.find_last_of(' ');
    size_t tokenStart = lastSpace == std::string_view::npos ? 0 : lastSpace + 1;
    std::string_view typed = rest.substr(tokenStart);
    mod::CommandContext context { std::string(label), splitArguments(rest.substr(0, tokenStart)), std::string(rest), chat };
    context.args.emplace_back(typed);
    std::vector<std::string> candidates;
    try {
        candidates = entry->spec.complete(context);
    } catch (const std::exception& failure) {
        errors(entry->owner, failure.what());
        return found;
    } catch (...) {
        errors(entry->owner, "unknown exception");
        return found;
    }
    found.replaceFrom = 1 + space + 1 + tokenStart;
    for (std::string& candidate : candidates) {
        if (found.suggestions.size() >= MaxSuggestions) {
            break;
        }
        if (startsWithName(candidate, typed)) {
            found.suggestions.emplace_back(std::move(candidate), std::string());
        }
    }
    return found;
}

std::vector<std::shared_ptr<const CommandRegistry::Entry>> CommandRegistry::list() const
{
    std::vector<std::shared_ptr<const Entry>> sorted(entries.begin(), entries.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& left, const auto& right) { return left->spec.name < right->spec.name; });
    return sorted;
}

void CommandRegistry::release(size_t owner)
{
    std::erase_if(entries, [owner](const std::shared_ptr<Entry>& entry) {
        if (entry->owner != owner) {
            return false;
        }
        entry->handle->expire();
        return true;
    });
}

std::vector<std::string> CommandRegistry::splitArguments(std::string_view text)
{
    std::vector<std::string> arguments;
    std::string current;
    bool quoted = false;
    bool pending = false;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '\\' && i + 1 < text.size() && (text[i + 1] == '"' || text[i + 1] == '\\')) {
            current += text[++i];
            pending = true;
        } else if (c == '"') {
            quoted = !quoted;
            pending = true;
        } else if (c == ' ' && !quoted) {
            if (pending) {
                arguments.push_back(std::move(current));
                current.clear();
                pending = false;
            }
        } else {
            current += c;
            pending = true;
        }
    }
    if (pending) {
        arguments.push_back(std::move(current));
    }
    return arguments;
}

std::shared_ptr<CommandRegistry::Entry> CommandRegistry::find(std::string_view name) const
{
    // The first mod to claim a name keeps it.
    for (const std::shared_ptr<Entry>& entry : entries) {
        if (sameName(entry->spec.name, name)) {
            return entry;
        }
        for (const std::string& alias : entry->spec.aliases) {
            if (sameName(alias, name)) {
                return entry;
            }
        }
    }
    return nullptr;
}

void CommandRegistry::remove(const Entry* entry)
{
    std::erase_if(entries, [entry](const std::shared_ptr<Entry>& other) { return other.get() == entry; });
}

}
