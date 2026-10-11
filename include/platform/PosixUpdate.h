#pragma once

#include <filesystem>
#include <cerrno>
#include <fstream>
#include <fcntl.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace kestrel::platform {

inline std::string shellLiteral(const std::filesystem::path& path)
{
    std::string result = "'";
    for (char c : path.string()) result += c == '\'' ? "'\\''" : std::string(1, c);
    return result + "'";
}

inline bool launchPosixUpdate(const std::filesystem::path& target, const std::filesystem::path& replacement, std::string& error)
{
    if (target.empty() || !std::filesystem::is_regular_file(target)) {
        error = "Could not locate the installed executable.";
        return false;
    }
    auto pending = target;
    pending += ".update-" + replacement.parent_path().filename().string();
    auto backup = pending;
    backup += ".previous";
    {
        std::ofstream probe(pending);
        if (!probe) { error = "The installation directory is not writable."; return false; }
    }
    std::error_code ec;
    std::filesystem::remove(pending, ec);
    auto script = replacement.parent_path() / "install.sh";
    std::ofstream file(script);
    file << "#!/bin/sh\nset -eu\n"
         << "target=" << shellLiteral(target) << "\nsource=" << shellLiteral(replacement)
         << "\npending=" << shellLiteral(pending) << "\nbackup=" << shellLiteral(backup) << "\n"
         << "n=0\nwhile kill -0 " << getpid() << " 2>/dev/null; do\n"
         << "  n=$((n + 1)); [ \"$n\" -le 60 ] || exit 1; sleep 1\ndone\n"
         << "cp \"$source\" \"$pending\"\nchmod 755 \"$pending\"\n"
         << "mv \"$target\" \"$backup\"\n"
         << "if ! mv \"$pending\" \"$target\"; then mv \"$backup\" \"$target\"; exit 1; fi\n"
         << "cd " << shellLiteral(target.parent_path()) << "\n"
         << "if ! \"$target\"; then mv \"$backup\" \"$target\"; exec \"$target\"; fi\n";
    file.close();
    if (!file) { error = "Could not create the update installer."; return false; }
    int handshake[2];
    if (pipe(handshake) != 0) { error = "Could not start the update installer."; return false; }
    if (fcntl(handshake[1], F_SETFD, FD_CLOEXEC) < 0) {
        close(handshake[0]);
        close(handshake[1]);
        error = "Could not start the update installer.";
        return false;
    }
    pid_t child = fork();
    if (child < 0) {
        close(handshake[0]);
        close(handshake[1]);
        error = "Could not start the update installer.";
        return false;
    }
    if (child == 0) {
        close(handshake[0]);
        if (setsid() >= 0) execl("/bin/sh", "sh", script.c_str(), static_cast<char*>(nullptr));
        char failed = 1;
        write(handshake[1], &failed, 1);
        _exit(1);
    }
    close(handshake[1]);
    char failed;
    ssize_t received;
    do { received = read(handshake[0], &failed, 1); } while (received < 0 && errno == EINTR);
    close(handshake[0]);
    if (received != 0) { error = "Could not start the update installer."; return false; }
    return true;
}

}
