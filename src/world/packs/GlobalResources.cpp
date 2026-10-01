#include "world/GlobalResources.h"

#include "Core/Json/Json.h"
#include "util/Text.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <set>
#include <stdexcept>

namespace kestrel::world {
namespace {
namespace fs = std::filesystem;

bool uuid(const std::string& id)
{
    if (id.size() != 36) return false;
    for (size_t i = 0; i < id.size(); ++i) {
        bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        if (dash ? id[i] != '-' : !std::isxdigit(static_cast<unsigned char>(id[i]))) return false;
    }
    return true;
}

std::array<int, 3> version(const json::Value* value)
{
    if (!value || !value->isArray() || value->mArray.size() != 3) throw std::runtime_error("Invalid pack version");
    std::array<int, 3> result {};
    for (size_t i = 0; i < 3; ++i) {
        double n = value->mArray[i]->number(-1);
        if (!std::isfinite(n) || n < 0 || n > 65535 || n != int(n)) throw std::runtime_error("Invalid pack version");
        result[i] = int(n);
    }
    return result;
}

void replaceFile(const fs::path& temporary, const fs::path& target)
{
    fs::path backup = target;
    backup += ".previous";
    std::error_code ec;
    fs::remove(backup, ec);
    bool existed = fs::exists(target);
    if (existed) fs::rename(target, backup);
    try { fs::rename(temporary, target); }
    catch (...) { if (existed) fs::rename(backup, target, ec); throw; }
    if (existed) fs::remove(backup, ec);
}

std::string localText(const PackFiles& pack, std::string value)
{
    auto lang = pack.find("texts/en_US.lang");
    if (!lang) return value;
    std::istringstream lines(*lang);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.starts_with(value + "=")) {
            std::string text = line.substr(value.size() + 1);
            if (!text.empty() && text.back() == '\r') text.pop_back();
            return text;
        }
    }
    return value;
}
}

GlobalResources::GlobalResources(fs::path directory) : root(std::move(directory))
{
    request({ GlobalPackAction::Kind::Refresh });
}

GlobalResources::~GlobalResources() = default;

GlobalResources::Record GlobalResources::read(const fs::path& file)
{
    Record record;
    auto filename = file.filename().u8string();
    record.entry.file.assign(filename.begin(), filename.end());
    record.entry.id = "invalid:" + record.entry.file;
    record.entry.name = record.entry.file;
    try {
        std::string error;
        record.pack = loadServerPack(file, {}, error);
        if (!record.pack) throw std::runtime_error(error);
        auto manifest = record.pack->find("manifest.json");
        if (!manifest) manifest = record.pack->find("pack_manifest.json");
        auto document = manifest ? json::parse(*manifest) : nullptr;
        auto* header = document ? document->get("header") : nullptr;
        auto* id = header ? header->get("uuid") : nullptr;
        auto* name = header ? header->get("name") : nullptr;
        auto* description = header ? header->get("description") : nullptr;
        if (!id || !uuid(id->string()) || !name || !name->isString()) throw std::runtime_error("Invalid resource pack header");
        bool resources = false;
        auto* modules = document->get("modules");
        if (modules && modules->isArray()) for (const auto& module : modules->mArray) {
            auto* type = module->get("type");
            if (type && type->string() == "resources") resources = true;
            else throw std::runtime_error("Global Resources only accepts resource packs");
        }
        if (!resources) throw std::runtime_error("This archive is not a resource pack");
        record.version = version(header->get("version"));
        record.entry.id = util::lowercase(id->string());
        record.entry.name = localText(*record.pack, name->string());
        record.entry.description = description ? localText(*record.pack, description->string()) : std::string();
        record.entry.version = std::to_string(record.version[0]) + "." + std::to_string(record.version[1]) + "." + std::to_string(record.version[2]);
        record.entry.bytes = record.pack->archiveBytes();
        record.entry.icon = record.pack->find("pack_icon.png");
        if (auto* dependencies = document->get("dependencies"); dependencies && dependencies->isArray()) {
            for (const auto& dependency : dependencies->mArray) {
                auto* dependencyId = dependency->get("uuid");
                if (!dependencyId || !uuid(dependencyId->string())) throw std::runtime_error("Invalid resource pack dependency");
                record.dependencies.emplace_back(util::lowercase(dependencyId->string()), version(dependency->get("version")));
            }
        }
        if (auto* variants = document->get("subpacks"); variants && variants->isArray()) {
            for (const auto& variant : variants->mArray) {
                auto* folder = variant->get("folder_name");
                auto* label = variant->get("name");
                std::string path = folder ? folder->string() : std::string();
                if (path.empty() || path.find_first_of("/\\:") != std::string::npos || path == "." || path == "..") throw std::runtime_error("Invalid subpack folder");
                record.entry.subPacks.emplace_back(path, label ? localText(*record.pack, label->string()) : path);
            }
        }
    } catch (const std::exception& e) {
        record.entry.error = e.what();
        record.pack.reset();
    }
    return record;
}

GlobalResources::State GlobalResources::process(fs::path root, State state, GlobalPackAction action)
{
    fs::create_directories(root);
    auto original = state;
    try {
        auto find = [&](const std::string& id) -> Record& {
            auto it = std::find_if(state.records.begin(), state.records.end(), [&](const auto& record) { return record.entry.id == id; });
            if (it == state.records.end()) throw std::runtime_error("Resource pack is no longer available");
            return *it;
        };
        if (action.kind == GlobalPackAction::Kind::Import) {
            fs::path source = fs::u8path(action.value);
            auto extension = util::lowercase(source.extension().string());
            if (extension != ".mcpack" && extension != ".zip") throw std::runtime_error("Choose a .mcpack or .zip resource pack");
            Record incoming = read(source);
            if (!incoming.pack) throw std::runtime_error(incoming.entry.error);
            for (const auto& record : state.records) {
                if (record.entry.id == incoming.entry.id && incoming.version < record.version) throw std::runtime_error("A newer version of this pack is already installed");
            }
            fs::path target = root / (incoming.entry.id + ".mcpack"), temporary = target;
            temporary += ".importing";
            fs::copy_file(source, temporary, fs::copy_options::overwrite_existing);
            Record copied = read(temporary);
            if (!copied.pack || copied.entry.id != incoming.entry.id || copied.version != incoming.version) {
                std::error_code ignored;
                fs::remove(temporary, ignored);
                throw std::runtime_error("Resource pack changed during import; retry the import");
            }
            replaceFile(temporary, target);
            state.message = "Imported " + incoming.entry.name;
            action.kind = GlobalPackAction::Kind::Refresh;
        }
        if (action.kind == GlobalPackAction::Kind::Refresh || action.kind == GlobalPackAction::Kind::Reload) {
            if (state.records.empty()) {
                std::ifstream config(root / "active.txt");
                std::string id, variant;
                while (config >> std::quoted(id) >> std::quoted(variant)) {
                    if (!uuid(id) || std::find(state.active.begin(), state.active.end(), id) != state.active.end()) continue;
                    state.active.push_back(id);
                    state.subPacks[id] = variant;
                }
            }
            std::vector<Record> records;
            for (const auto& file : fs::directory_iterator(root)) {
                if (!file.is_regular_file() || file.is_symlink()) continue;
                std::string ext = util::lowercase(file.path().extension().string());
                if (ext != ".mcpack" && ext != ".zip") continue;
                Record record = read(file.path());
                records.push_back(std::move(record));
            }
            std::sort(records.begin(), records.end(), [](const Record& a, const Record& b) {
                if (a.entry.id != b.entry.id) return a.entry.id < b.entry.id;
                if (a.version != b.version) return a.version > b.version;
                bool canonicalA = a.entry.file == a.entry.id + ".mcpack";
                bool canonicalB = b.entry.file == b.entry.id + ".mcpack";
                if (canonicalA != canonicalB) return canonicalA;
                return a.entry.file < b.entry.file;
            });
            std::set<std::string> seen;
            for (auto& record : records) {
                if (!seen.insert(record.entry.id).second) {
                    record.entry.error = "Duplicate resource pack UUID";
                    record.entry.id = "invalid:" + record.entry.file;
                    record.pack.reset();
                }
            }
            state.records = std::move(records);
            std::erase_if(state.active, [&](const std::string& id) {
                return std::none_of(state.records.begin(), state.records.end(), [&](const auto& record) { return record.entry.id == id && record.pack; });
            });
            bool removed;
            do {
                removed = false;
                const auto activeBefore = state.active;
                std::erase_if(state.active, [&](const std::string& id) {
                    const auto& record = find(id);
                    for (const auto& [dependency, wanted] : record.dependencies) {
                        auto source = std::find_if(state.records.begin(), state.records.end(), [&](const auto& candidate) {
                            return candidate.entry.id == dependency && candidate.pack && candidate.version == wanted;
                        });
                        if (source == state.records.end() || std::find(activeBefore.begin(), activeBefore.end(), dependency) == activeBefore.end()) {
                            state.message = "Deactivated packs with missing dependencies";
                            removed = true;
                            return true;
                        }
                    }
                    return false;
                });
            } while (removed);
        } else if (action.kind == GlobalPackAction::Kind::Activate) {
            std::set<std::string> visiting, done;
            std::function<void(const std::string&)> activate = [&](const std::string& id) {
                if (done.contains(id)) return;
                if (!visiting.insert(id).second) throw std::runtime_error("Circular resource pack dependency");
                Record& record = find(id);
                if (!record.pack) throw std::runtime_error(record.entry.error);
                for (const auto& [dependency, wanted] : record.dependencies) {
                    if (find(dependency).version != wanted) throw std::runtime_error("Dependency version mismatch: " + dependency);
                    activate(dependency);
                }
                std::erase(state.active, id);
                state.active.insert(state.active.begin(), id);
                visiting.erase(id);
                done.insert(id);
            };
            activate(action.id);
        } else if (action.kind == GlobalPackAction::Kind::Deactivate || action.kind == GlobalPackAction::Kind::Remove) {
            std::set<std::string> disabled { action.id };
            bool expanded = true;
            while (expanded) {
                expanded = false;
                for (const auto& id : state.active) for (const auto& dependency : find(id).dependencies)
                    if (disabled.contains(dependency.first) && disabled.insert(id).second) expanded = true;
            }
            std::erase_if(state.active, [&](const auto& id) { return disabled.contains(id); });
            if (action.kind == GlobalPackAction::Kind::Remove) {
                const std::string file = find(action.id).entry.file;
                fs::path relative = fs::u8path(file);
                if (relative.has_parent_path()) throw std::runtime_error("Invalid pack filename");
                fs::remove(root / relative);
                std::erase_if(state.records, [&](const auto& record) { return record.entry.id == action.id; });
                state.subPacks.erase(action.id);
            }
        } else if (action.kind == GlobalPackAction::Kind::Up || action.kind == GlobalPackAction::Kind::Down) {
            auto it = std::find(state.active.begin(), state.active.end(), action.id);
            if (it != state.active.end()) {
                if (action.kind == GlobalPackAction::Kind::Up && it != state.active.begin()) std::iter_swap(it, it - 1);
                if (action.kind == GlobalPackAction::Kind::Down && it + 1 != state.active.end()) std::iter_swap(it, it + 1);
            }
        } else if (action.kind == GlobalPackAction::Kind::SubPack) {
            auto& record = find(action.id);
            if (!action.value.empty() && std::none_of(record.entry.subPacks.begin(), record.entry.subPacks.end(), [&](const auto& variant) { return variant.first == action.value; })) throw std::runtime_error("Unknown subpack");
            state.subPacks[action.id] = action.value;
        }
        uint64_t archived = 0, expanded = 0;
        for (const auto& id : state.active) {
            const auto& record = find(id);
            if (!record.pack) throw std::runtime_error("Invalid active resource pack");
            archived += record.pack->archiveBytes();
            expanded += record.pack->expandedBytes();
            if (archived > 512ull * 1024 * 1024 || expanded > 1024ull * 1024 * 1024) throw std::runtime_error("Global resource pack stack exceeds memory budget");
            for (const auto& [dependency, wanted] : record.dependencies)
                if (std::find(state.active.begin(), state.active.end(), dependency) == state.active.end() || find(dependency).version != wanted)
                    throw std::runtime_error("Missing resource pack dependency: " + dependency);
        }
        fs::path temporary = root / "active.txt.tmp";
        std::ofstream config(temporary, std::ios::trunc);
        for (const auto& id : state.active) config << std::quoted(id) << ' ' << std::quoted(state.subPacks[id]) << '\n';
        config.close();
        if (!config) throw std::runtime_error("Cannot save global resource settings");
        replaceFile(temporary, root / "active.txt");
        if (state.message.empty()) state.message = "Global resources updated";
        return state;
    } catch (const std::exception& e) {
        original.message = e.what();
        return original;
    }
}

void GlobalResources::request(GlobalPackAction action)
{
    requests.push_back(std::move(action));
}

void GlobalResources::publish()
{
    shown.clear();
    std::vector<std::shared_ptr<const PackFiles>> next;
    std::vector<std::pair<std::shared_ptr<const PackFiles>, std::string>> sources;
    auto append = [&](const Record& record, bool active) {
        GlobalPackEntry entry = record.entry;
        entry.active = active;
        auto selected = state.subPacks.find(entry.id);
        if (selected != state.subPacks.end()) entry.selectedSubPack = selected->second;
        if (!entry.selectedSubPack.empty() && std::none_of(entry.subPacks.begin(), entry.subPacks.end(), [&](const auto& variant) { return variant.first == entry.selectedSubPack; })) entry.selectedSubPack.clear();
        if (active) sources.emplace_back(record.pack, entry.selectedSubPack);
        shown.push_back(std::move(entry));
    };
    for (const auto& id : state.active) for (const auto& record : state.records) if (record.entry.id == id && record.pack) append(record, true);
    for (const auto& record : state.records) if (std::find(state.active.begin(), state.active.end(), record.entry.id) == state.active.end()) append(record, false);
    if (sources != activeSources) {
        for (const auto& [pack, variant] : sources) next.push_back(variant.empty() ? pack : pack->withSubPack(variant));
        activeSources = std::move(sources);
        activePacks = std::move(next);
    }
    status = state.message;
    ++serial;
}

bool GlobalResources::poll()
{
    bool changed = false;
    if (job.valid() && job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try { state = job.get(); publish(); changed = true; }
        catch (const std::exception& e) { status = e.what(); }
    }
    if (!job.valid() && !requests.empty()) {
        auto action = std::move(requests.front());
        requests.pop_front();
        state.message.clear();
        job = std::async(std::launch::async, &GlobalResources::process, root, state, std::move(action));
    }
    return changed;
}
}
