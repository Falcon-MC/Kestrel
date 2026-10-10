#include "platform/Update.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <fstream>
#include <vector>

namespace kestrel::platform {
namespace {

std::string literal(const std::filesystem::path& path)
{
    auto utf8 = path.u8string();
    std::string value(utf8.begin(), utf8.end()), result = "'";
    for (char c : value) result += c == '\'' ? "''" : std::string(1, c);
    return result + "'";
}

}

std::filesystem::path executablePath()
{
    std::vector<wchar_t> buffer(32768);
    DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    return size && size < buffer.size() ? std::filesystem::path(std::wstring(buffer.data(), size)) : std::filesystem::path();
}

std::string updatePlatform()
{
#if defined(_M_X64) || defined(__x86_64__)
    return "windows-x64.zip";
#else
    return {};
#endif
}

bool launchUpdate(const std::filesystem::path& replacement, std::string& error)
{
    auto target = executablePath();
    auto pending = target;
    pending += L".update-" + replacement.parent_path().filename().wstring();
    auto backup = pending;
    backup += L".previous";
    {
        std::ofstream probe(pending, std::ios::binary);
        if (!probe) { error = "The installation directory is not writable."; return false; }
    }
    std::error_code ec;
    std::filesystem::remove(pending, ec);
    auto script = replacement.parent_path() / "install.ps1";
    std::ofstream file(script, std::ios::binary);
    file << "\xef\xbb\xbf$ErrorActionPreference = 'Stop'\n"
         << "$target = " << literal(target) << "\n$source = " << literal(replacement)
         << "\n$pending = " << literal(pending) << "\n$backup = " << literal(backup) << "\n"
         << "try {\n"
         << "  $parent = Get-Process -Id " << GetCurrentProcessId() << " -ErrorAction SilentlyContinue\n"
         << "  if ($parent -and -not $parent.WaitForExit(60000)) { throw 'Kestrel did not close.' }\n"
         << "  Copy-Item -LiteralPath $source -Destination $pending\n"
         << "  Move-Item -LiteralPath $target -Destination $backup\n"
         << "  try {\n"
         << "    Move-Item -LiteralPath $pending -Destination $target\n"
         << "    $client = Start-Process -FilePath $target -WorkingDirectory (Split-Path -LiteralPath $target) -PassThru -ErrorAction Stop\n"
         << "    if ($client.WaitForExit(2000) -and $client.ExitCode -ne 0) { throw 'The updated client could not start.' }\n"
         << "  } catch {\n"
         << "    if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target }\n"
         << "    Move-Item -LiteralPath $backup -Destination $target\n"
         << "    throw\n  }\n"
         << "} catch {\n"
         << "  $_ | Out-File -LiteralPath " << literal(replacement.parent_path() / "error.txt") << "\n"
         << "  if (Test-Path -LiteralPath $target) { Start-Process -FilePath $target -WorkingDirectory (Split-Path -LiteralPath $target) }\n"
         << "}\n";
    file.close();
    if (!file) { error = "Could not create the update installer."; return false; }
    wchar_t system[MAX_PATH];
    if (!GetSystemDirectoryW(system, MAX_PATH)) { error = "Could not locate PowerShell."; return false; }
    auto shell = std::filesystem::path(system) / L"WindowsPowerShell/v1.0/powershell.exe";
    std::wstring command = L"\"" + shell.wstring() + L"\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + script.wstring() + L"\"";
    STARTUPINFOW startup {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process {};
    if (!CreateProcessW(shell.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        error = "Could not start the update installer.";
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

}
