#include "iso_io.h"
#include <windows.h>
#include <fileapifromapp.h>
#include <filesystem>

namespace {
struct File { HANDLE handle; };

std::wstring wide(const std::string &s) {
    const int count = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (count <= 0) return {};
    std::wstring text(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, text.data(), count);
    text.pop_back();
    return text;
}

Sint64 SDLCALL size(void *p) {
    LARGE_INTEGER value{};
    return GetFileSizeEx(static_cast<File *>(p)->handle, &value) ? value.QuadPart : -1;
}
Sint64 SDLCALL seek(void *p, Sint64 offset, SDL_IOWhence whence) {
    LARGE_INTEGER move{}, position{};
    move.QuadPart = offset;
    const DWORD method = whence == SDL_IO_SEEK_SET ? FILE_BEGIN : whence == SDL_IO_SEEK_CUR ? FILE_CURRENT : FILE_END;
    return SetFilePointerEx(static_cast<File *>(p)->handle, move, &position, method) ? position.QuadPart : -1;
}
size_t SDLCALL read(void *p, void *buffer, size_t count, SDL_IOStatus *status) {
    DWORD done = 0;
    if (!ReadFile(static_cast<File *>(p)->handle, buffer, static_cast<DWORD>(count), &done, nullptr)) {
        *status = SDL_IO_STATUS_ERROR;
        return 0;
    }
    if (done == 0) *status = SDL_IO_STATUS_EOF;
    return done;
}
size_t SDLCALL write(void *p, const void *buffer, size_t count, SDL_IOStatus *status) {
    DWORD done = 0;
    if (!WriteFile(static_cast<File *>(p)->handle, buffer, static_cast<DWORD>(count), &done, nullptr)) {
        *status = SDL_IO_STATUS_ERROR;
        return 0;
    }
    return done;
}
bool SDLCALL flush(void *p, SDL_IOStatus *status) {
    if (FlushFileBuffers(static_cast<File *>(p)->handle)) return true;
    *status = SDL_IO_STATUS_ERROR;
    return false;
}
bool SDLCALL close(void *p) {
    auto *file = static_cast<File *>(p);
    const bool ok = CloseHandle(file->handle) != 0;
    delete file;
    return ok;
}
}

SDL_IOStream *PortUwp_OpenIO(const std::string &path, bool writing) {
    const auto name = wide(path);
    if (name.empty()) return nullptr;
    const HANDLE handle = CreateFileFromAppW(name.c_str(), writing ? GENERIC_WRITE : GENERIC_READ,
        FILE_SHARE_READ, nullptr, writing ? CREATE_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return nullptr;
    auto *file = new File{handle};
    SDL_IOStreamInterface iface{};
    SDL_INIT_INTERFACE(&iface);
    iface.size = size;
    iface.seek = seek;
    iface.read = read;
    iface.write = write;
    iface.flush = flush;
    iface.close = close;
    SDL_IOStream *stream = SDL_OpenIO(&iface, file);
    if (!stream) close(file);
    return stream;
}

bool PortUwp_CreateDirectory(const std::string &path) {
    const std::filesystem::path target = std::filesystem::path(path).lexically_normal();
    std::filesystem::path current = target.root_path();
    for (const auto &part : target.relative_path()) {
        current /= part;
        const auto name = current.wstring();
        if (!CreateDirectoryFromAppW(name.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
            return false;
    }
    return true;
}

bool PortUwp_FileExists(const std::string &path) {
    WIN32_FILE_ATTRIBUTE_DATA info{};
    const auto name = wide(path);
    return !name.empty() && GetFileAttributesExFromAppW(name.c_str(), GetFileExInfoStandard, &info) &&
        !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool PortUwp_MoveFile(const std::string &source, const std::string &destination) {
    const auto from = wide(source);
    const auto to = wide(destination);
    if (from.empty() || to.empty()) return false;
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (GetFileAttributesExFromAppW(to.c_str(), GetFileExInfoStandard, &info))
        return ReplaceFileFromAppW(to.c_str(), from.c_str(), nullptr,
                                   REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr);
    return MoveFileFromAppW(from.c_str(), to.c_str());
}
