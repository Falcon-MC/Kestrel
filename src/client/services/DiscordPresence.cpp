#include "client/DiscordPresence.h"
#include "client/DebugLog.h"
#include "Core/Json/Json.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace kestrel {
namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

// Discord's Minecraft application. The application ID determines the displayed
// game name; a "name" field in SET_ACTIVITY cannot rename an application.
constexpr const char* MinecraftApplicationId = "356875570916753438";
constexpr size_t MaxPayload = 64 * 1024;

uint32_t readUint32(const char* bytes)
{
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value |= uint32_t(static_cast<unsigned char>(bytes[i])) << (i * 8);
    }
    return value;
}

void appendUint32(std::string& bytes, uint32_t value)
{
    for (int i = 0; i < 4; ++i) {
        bytes.push_back(static_cast<char>(value >> (i * 8)));
    }
}

std::string field(const json::Value& object, const char* name)
{
    const json::Value* value = object.get(name);
    return value ? value->string() : std::string();
}

// Discord's IPC framing: https://docs.discord.com/developers/topics/rpc
class IpcConnection {
public:
    ~IpcConnection() { close(); }

    bool connected() const
    {
#ifdef _WIN32
        return pipe != INVALID_HANDLE_VALUE;
#else
        return socket >= 0;
#endif
    }

    bool open()
    {
#ifdef _WIN32
        for (int index = 0; index < 10; ++index) {
            std::string path = "\\\\?\\pipe\\discord-ipc-" + std::to_string(index);
            pipe = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                OPEN_EXISTING, 0, nullptr);
            if (connected()) {
                DWORD mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
                if (SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr)) {
                    return true;
                }
                close();
            }
        }
#else
        std::vector<std::string> roots;
        for (const char* variable : { "XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP" }) {
            if (const char* value = std::getenv(variable); value && *value) {
                roots.emplace_back(value);
            }
        }
        roots.emplace_back("/tmp");
        for (const std::string& root : roots) {
            for (const char* subdirectory : { "", "/app/com.discordapp.Discord",
                     "/app/com.discordapp.DiscordCanary", "/app/com.discordapp.DiscordPTB" }) {
                for (int index = 0; index < 10; ++index) {
                    std::string path = root + subdirectory + "/discord-ipc-" + std::to_string(index);
                    sockaddr_un address {};
                    if (path.size() >= sizeof(address.sun_path)) {
                        continue;
                    }
                    address.sun_family = AF_UNIX;
                    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
                    socket = ::socket(AF_UNIX, SOCK_STREAM, 0);
                    if (socket < 0) {
                        continue;
                    }
                    if (fcntl(socket, F_SETFL, O_NONBLOCK) < 0 || fcntl(socket, F_SETFD, FD_CLOEXEC) < 0) {
                        close();
                        continue;
                    }
#ifdef __APPLE__
                    int noSigpipe = 1;
                    if (setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &noSigpipe, sizeof(noSigpipe)) < 0) {
                        close();
                        continue;
                    }
#endif
                    if (::connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
                        return true;
                    }
                    close();
                }
            }
        }
#endif
        return false;
    }

    void close()
    {
        if (!connected()) {
            return;
        }
#ifdef _WIN32
        CloseHandle(pipe);
        pipe = INVALID_HANDLE_VALUE;
#else
        ::close(socket);
        socket = -1;
#endif
    }

    // Zero means try again later, negative means the connection has closed.
    int read(char* bytes, size_t size)
    {
#ifdef _WIN32
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) {
            return -1;
        }
        if (!available) {
            return 0;
        }
        DWORD count = 0;
        if (ReadFile(pipe, bytes, static_cast<DWORD>(size), &count, nullptr)) {
            return static_cast<int>(count);
        }
        return GetLastError() == ERROR_NO_DATA ? 0 : -1;
#else
        int count = static_cast<int>(recv(socket, bytes, size, 0));
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            return 0;
        }
        return count == 0 ? -1 : count;
#endif
    }

    int write(const std::string& bytes)
    {
#ifdef _WIN32
        DWORD count = 0;
        if (WriteFile(pipe, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr)) {
            return static_cast<int>(count);
        }
        return -1;
#else
#ifdef MSG_NOSIGNAL
        constexpr int flags = MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif
        int count = static_cast<int>(send(socket, bytes.data(), bytes.size(), flags));
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            return 0;
        }
        return count;
#endif
    }

private:
#ifdef _WIN32
    HANDLE pipe = INVALID_HANDLE_VALUE;
#else
    int socket = -1;
#endif
};

}

struct DiscordPresence::Impl {
    IpcConnection connection;
    std::string input;
    std::string output;
    Clock::time_point nextPoll {};
    Clock::time_point nextConnect {};
    Clock::time_point responseDeadline {};
    bool ready = false;
    bool acknowledged = false;
    const int64_t started = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
#ifdef _WIN32
    const unsigned long pid = GetCurrentProcessId();
#else
    const int pid = getpid();
#endif

    ~Impl()
    {
        if (connection.connected() && ready) {
            queue(1, activity(true));
            flush();
        }
        // Closing the IPC connection also removes the activity, including when
        // a nonblocking clear could not finish writing during shutdown.
    }

    std::string activity(bool clear = false) const
    {
        std::string value = clear ? "null" : "{\"type\":0,\"timestamps\":{\"start\":"
            + std::to_string(started) + "}}";
        return "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(pid)
            + ",\"activity\":" + value + "},\"nonce\":\"" + (clear ? "2" : "1") + "\"}";
    }

    void disconnect()
    {
        connection.close();
        input.clear();
        output.clear();
        ready = acknowledged = false;
        nextConnect = Clock::now() + 5s;
    }

    bool queue(uint32_t opcode, const std::string& payload)
    {
        if (output.size() + payload.size() + 8 > MaxPayload + 8) {
            disconnect();
            return false;
        }
        appendUint32(output, opcode);
        appendUint32(output, static_cast<uint32_t>(payload.size()));
        output += payload;
        return true;
    }

    bool flush()
    {
        if (!output.empty()) {
            int count = connection.write(output);
            if (count < 0) {
                disconnect();
                return false;
            }
            output.erase(0, static_cast<size_t>(count));
        }
        return true;
    }

    bool receive(uint32_t opcode, const std::string& payload)
    {
        if (opcode == 3) {
            return queue(4, payload);
        }
        if (opcode == 4) {
            return true;
        }
        if (opcode != 1) {
            return false;
        }
        std::unique_ptr<json::Value> message = json::parse(payload);
        if (!message || !message->isObject()) {
            return false;
        }
        if (field(*message, "evt") == "ERROR") {
            debugLog("Discord rejected the Minecraft activity request");
            return false;
        }
        if (!ready && field(*message, "cmd") == "DISPATCH" && field(*message, "evt") == "READY") {
            ready = true;
            responseDeadline = Clock::now() + 10s;
            return queue(1, activity());
        }
        if (ready && !acknowledged && field(*message, "cmd") == "SET_ACTIVITY" && field(*message, "nonce") == "1") {
            acknowledged = true;
            debugLog("Discord Minecraft activity published");
        }
        return true;
    }

    void update()
    {
        auto now = Clock::now();
        if (now < nextPoll) {
            return;
        }
        nextPoll = now + 250ms;
        if (!connection.connected()) {
            if (now < nextConnect) {
                return;
            }
            nextConnect = now + 5s;
            if (!connection.open()) {
                return;
            }
            responseDeadline = now + 10s;
            queue(0, std::string("{\"v\":1,\"client_id\":\"") + MinecraftApplicationId + "\"}");
        }
        if ((!acknowledged && now >= responseDeadline) || !flush()) {
            disconnect();
            return;
        }

        std::array<char, 4096> bytes {};
        int count = connection.read(bytes.data(), bytes.size());
        if (count < 0) {
            disconnect();
            return;
        }
        input.append(bytes.data(), static_cast<size_t>(count));
        while (input.size() >= 8) {
            uint32_t opcode = readUint32(input.data());
            uint32_t length = readUint32(input.data() + 4);
            if (length > MaxPayload) {
                disconnect();
                return;
            }
            if (input.size() < 8 + length) {
                break;
            }
            std::string payload = input.substr(8, length);
            input.erase(0, 8 + length);
            if (!receive(opcode, payload)) {
                disconnect();
                return;
            }
        }
        flush();
    }
};

DiscordPresence::DiscordPresence() : impl(std::make_unique<Impl>()) {}
DiscordPresence::~DiscordPresence() = default;

void DiscordPresence::update()
{
    impl->update();
}

}
