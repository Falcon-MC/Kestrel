#include "menu/ServerStore.h"

#include "util/Text.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>

namespace kestrel::menu {

namespace {

std::string sanitize(std::string value)
{
    std::replace(value.begin(), value.end(), '\t', ' ');
    std::replace(value.begin(), value.end(), '\n', ' ');
    std::replace(value.begin(), value.end(), '\r', ' ');
    return value;
}

using util::trim;

int64_t now()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

}

std::string normalizeAddress(std::string address)
{
    address = trim(sanitize(std::move(address)));
    if (address.empty()) {
        return address;
    }
    bool bracketed = address.front() == '[';
    size_t colon = address.rfind(':');
    bool hasPort = bracketed ? address.find("]:") != std::string::npos : colon != std::string::npos && address.find(':') == colon;
    if (!hasPort) {
        address += ":19132";
    }
    return address;
}

ServerStore::ServerStore(std::filesystem::path file)
    : file(std::move(file))
{
}

void ServerStore::load()
{
    entries.clear();
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream row(line);
        std::string favorite;
        std::string joined;
        SavedServer server;
        if (!std::getline(row, favorite, '\t') || !std::getline(row, joined, '\t') || !std::getline(row, server.name, '\t') || !std::getline(row, server.address)) {
            continue;
        }
        server.favorite = favorite == "1";
        try {
            server.lastJoined = std::stoll(joined);
        } catch (...) {
            server.lastJoined = 0;
        }
        if (!server.address.empty()) {
            entries.push_back(std::move(server));
        }
    }
}

void ServerStore::save() const
{
    std::ofstream out(file, std::ios::trunc);
    for (const SavedServer& server : entries) {
        out << (server.favorite ? '1' : '0') << '\t' << server.lastJoined << '\t' << server.name << '\t' << server.address << '\n';
    }
}

size_t ServerStore::add(std::string name, std::string address)
{
    SavedServer server;
    server.address = normalizeAddress(std::move(address));
    server.name = trim(sanitize(std::move(name)));
    if (server.name.empty()) {
        server.name = server.address;
    }
    entries.push_back(std::move(server));
    save();
    return entries.size() - 1;
}

void ServerStore::update(size_t index, std::string name, std::string address)
{
    if (index >= entries.size()) {
        return;
    }
    SavedServer& server = entries[index];
    server.address = normalizeAddress(std::move(address));
    server.name = trim(sanitize(std::move(name)));
    if (server.name.empty()) {
        server.name = server.address;
    }
    save();
}

void ServerStore::remove(size_t index)
{
    if (index >= entries.size()) {
        return;
    }
    entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(index));
    save();
}

void ServerStore::toggleFavorite(size_t index)
{
    if (index >= entries.size()) {
        return;
    }
    entries[index].favorite = !entries[index].favorite;
    save();
}

void ServerStore::markJoined(size_t index)
{
    if (index >= entries.size()) {
        return;
    }
    entries[index].lastJoined = now();
    save();
}

}
