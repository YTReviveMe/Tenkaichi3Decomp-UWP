#include "storage.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <cstring>
#if defined(_WIN32) && defined(BT3_UWP)
#include <windows.h>
#include <fileapifromapp.h>
#include <winrt/Windows.Storage.h>
#include "iso_io.h"
#endif

namespace {
std::string data_path;
std::string internal_path;
std::string save_path;
std::string settings_path;
std::string content_path;
std::string error_text;
#if defined(_WIN32) && defined(BT3_UWP)
DWORD local_write_error = 0;

void append_log(const std::wstring &path, const char *message) {
    HANDLE file = CreateFileFromAppW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    const char *line = message ? message : "";
    DWORD written = 0;
    WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
    WriteFile(file, "\r\n", 2, &written, nullptr);
    CloseHandle(file);
}
#endif

bool installed(const std::filesystem::path &root) {
#if defined(_WIN32) && defined(BT3_UWP)
    const auto exists = [](const std::filesystem::path &path) {
        WIN32_FILE_ATTRIBUTE_DATA info{};
        return GetFileAttributesExFromAppW(path.c_str(), GetFileExInfoStandard, &info) &&
               !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
    };
    return exists(root / "disc" / "SLUS_216.78") &&
           exists(root / "disc" / "BIN" / "DBZP.BIN");
#else
    std::error_code ec;
    if (!std::filesystem::is_regular_file(root / "disc" / "SLUS_216.78", ec)) return false;
    ec.clear();
    return std::filesystem::is_regular_file(root / "disc" / "BIN" / "DBZP.BIN", ec);
#endif
}

bool directory_exists(const std::filesystem::path &path) {
#if defined(_WIN32) && defined(BT3_UWP)
    WIN32_FILE_ATTRIBUTE_DATA info{};
    return GetFileAttributesExFromAppW(path.c_str(), GetFileExInfoStandard, &info) &&
           (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
#else
    std::error_code ec;
    return std::filesystem::is_directory(path, ec);
#endif
}

bool create_local(const std::filesystem::path &path) {
#if defined(_WIN32) && defined(BT3_UWP)
    local_write_error = 0;
    if (!PortUwp_CreateDirectory(path.string())) {
        local_write_error = GetLastError();
        return false;
    }
    const auto probe = (path / "bt3-write-probe.txt").wstring();
    HANDLE file = CreateFileFromAppW(probe.c_str(), GENERIC_WRITE, 0, nullptr,
                                     CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        local_write_error = GetLastError();
        return false;
    }
    const char marker = '1';
    DWORD count = 0;
    const bool ok = WriteFile(file, &marker, 1, &count, nullptr) && count == 1;
    if (!ok) local_write_error = GetLastError();
    CloseHandle(file);
    DeleteFileFromAppW(probe.c_str());
    return ok;
#else
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) return false;
    const auto probe = path / ".bt3-write-probe";
    std::ofstream file(probe, std::ios::binary);
    if (!file) return false;
    file.put('1');
    file.close();
    const bool ok = !file.fail();
    std::filesystem::remove(probe, ec);
    return ok;
#endif
}
}

extern "C" int PortUwp_ConfigureStorage(const char *local_path, const char *external_path) {
    data_path.clear();
    internal_path.clear();
    save_path.clear();
    settings_path.clear();
    content_path.clear();
    error_text.clear();
    if (!local_path || !*local_path) {
        error_text = "The app LocalFolder path is empty";
        return 0;
    }
    const std::filesystem::path local(local_path);
    if (!local.is_absolute() || !create_local(local)) {
        error_text = "Internal BT3 is not writable: " + local.string() +
                     " (Windows error " + std::to_string(local_write_error) + ")";
        return 0;
    }
    internal_path = local.string();
    std::filesystem::path content = local;
    if (external_path && *external_path) {
        const std::filesystem::path external(external_path);
        if (!external.is_absolute()) {
            error_text = "External content path must be absolute";
            return 0;
        }
        if (installed(external)) content = external;
        else if (!installed(local)) {
            error_text = "No installed game data at the external path or in LocalFolder";
            return 0;
        }
    } else if (!installed(local)) {
        error_text = "No installed game data in LocalFolder";
        return 0;
    }
#if defined(_WIN32) && defined(BT3_UWP)
    const bool saves_ready = PortUwp_CreateDirectory((local / "saves").string());
#else
    std::error_code ec;
    const bool saves_ready = std::filesystem::create_directories(local / "saves", ec) || !ec;
#endif
    if (!saves_ready) {
        error_text = "Could not create the internal saves folder";
        return 0;
    }
    content_path = content.string();
    data_path = content.string();
    save_path = (local / "saves").string();
    settings_path = (local / "bt3_settings.txt").string();
    return 1;
}

extern "C" const char *PortUwp_DataPath(void) { return data_path.c_str(); }
extern "C" int PortUwp_EnsureDirectory(const char *path) {
#if defined(_WIN32) && defined(BT3_UWP)
    return path && *path && PortUwp_CreateDirectory(path);
#else
    if (!path || !*path) return 0;
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return !ec;
#endif
}
extern "C" const char *PortUwp_LocalPath(void) { return internal_path.c_str(); }
extern "C" const char *PortUwp_SavePath(void) { return save_path.c_str(); }
extern "C" const char *PortUwp_SettingsPath(void) { return settings_path.c_str(); }
extern "C" const char *PortUwp_ContentPath(void) { return content_path.c_str(); }
extern "C" const char *PortUwp_StorageError(void) { return error_text.c_str(); }
extern "C" void PortUwp_SetStorageError(const char *message) { error_text = message ? message : ""; }
extern "C" void PortUwp_Trace(const char *message) {
#if defined(_WIN32) && defined(BT3_UWP)
    OutputDebugStringA(message ? message : "");
    OutputDebugStringA("\n");
    try {
        const auto local = winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path();
        append_log((std::filesystem::path(local.c_str()) / "boot.log").wstring(), message);
    } catch (...) {}
    if (!internal_path.empty())
        append_log((std::filesystem::path(internal_path) / "port.log").wstring(), message);
#else
    if (internal_path.empty()) return;
    std::ofstream log(std::filesystem::path(internal_path) / "port.log", std::ios::app);
    if (log) log << (message ? message : "") << '\n';
#endif
}

extern "C" int PortUwp_ApplyStorageEnvironment(void) {
    if (data_path.empty()) return 0;
    const auto internal_textures = std::filesystem::path(internal_path) / "textures";
    const auto texture_path = directory_exists(internal_textures) ? internal_textures :
                              std::filesystem::path(content_path) / "textures";
#ifdef _WIN32
    const auto set = [](const char *key, const std::string &value) { return _putenv_s(key, value.c_str()) == 0; };
#else
    const auto set = [](const char *key, const std::string &value) { return setenv(key, value.c_str(), 1) == 0; };
#endif
    const bool paths = set("BT3_DATA", data_path) && set("BT3_SAVES", save_path) &&
           set("BT3_SETTINGS", settings_path) &&
           set("BT3_TEXTURES", texture_path.string()) &&
           set("BT3_STAGES", (std::filesystem::path(content_path) / "stages").string()) &&
           set("BT3_SONGS", (std::filesystem::path(content_path) / "songs").string()) &&
           set("BT3_MATCH_URL", "");
    if (!paths) return 0;
    PortUwp_Trace(("Texture folder: " + texture_path.string()).c_str());
    return 1;
}
