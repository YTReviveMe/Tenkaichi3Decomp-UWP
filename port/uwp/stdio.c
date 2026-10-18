#include <windows.h>
#include <fileapifromapp.h>
#include <fcntl.h>
#include <io.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "storage.h"
#define BT3_UWP_STDIO_IMPLEMENTATION
#include "include/uwp_stdio.h"

static wchar_t *wide_path(const char *path) {
    int length;
    wchar_t *wide;
    if (!path) return NULL;
    length = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (length <= 0) return NULL;
    wide = (wchar_t *)malloc((size_t)length * sizeof(*wide));
    if (wide) MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, length);
    return wide;
}

int PortUwp_Access(const char *path, int mode) {
    struct stat info;
    if (mode != 0) return _access(path, mode);
    return PortUwp_Stat(path, &info);
}

int PortUwp_Rename(const char *source, const char *destination) {
    wchar_t *from = wide_path(source), *to = wide_path(destination);
    int ok = from && to && MoveFileFromAppW(from, to);
    free(from);
    free(to);
    return ok ? 0 : -1;
}

int PortUwp_Remove(const char *path) {
    wchar_t *wide = wide_path(path);
    int ok = wide && DeleteFileFromAppW(wide);
    free(wide);
    return ok ? 0 : -1;
}

int PortUwp_Mkdir(const char *path) {
    return PortUwp_EnsureDirectory(path) ? 0 : -1;
}

static void find_entry(struct _finddata_t *entry, const WIN32_FIND_DATAW *data) {
    memset(entry, 0, sizeof(*entry));
    WideCharToMultiByte(CP_UTF8, 0, data->cFileName, -1, entry->name,
                        sizeof(entry->name), NULL, NULL);
}

intptr_t PortUwp_FindFirst(const char *pattern, struct _finddata_t *entry) {
    wchar_t *wide = wide_path(pattern);
    WIN32_FIND_DATAW data;
    HANDLE search;
    if (!wide || !entry) { free(wide); return -1; }
    search = FindFirstFileExFromAppW(wide, FindExInfoBasic, &data,
                                    FindExSearchNameMatch, NULL, 0);
    free(wide);
    if (search == INVALID_HANDLE_VALUE) return -1;
    find_entry(entry, &data);
    return (intptr_t)search;
}

int PortUwp_FindNext(intptr_t search, struct _finddata_t *entry) {
    WIN32_FIND_DATAW data;
    if (!entry || !FindNextFileW((HANDLE)search, &data)) return -1;
    find_entry(entry, &data);
    return 0;
}

int PortUwp_FindClose(intptr_t search) {
    return FindClose((HANDLE)search) ? 0 : -1;
}

static int same_prefix(const char *path, const char *root) {
    size_t i = 0;
    if (!root || !*root) return 0;
    for (; root[i]; i++) {
        char a = path[i] == '/' ? '\\' : path[i];
        char b = root[i] == '/' ? '\\' : root[i];
        if (!a || tolower((unsigned char)a) != tolower((unsigned char)b)) return 0;
    }
    return path[i] == 0 || path[i] == '/' || path[i] == '\\';
}

FILE *PortUwp_Fopen(const char *path, const char *mode) {
    const char *external = PortUwp_ContentPath();
    const char *local = PortUwp_LocalPath();
    wchar_t *wide;
    DWORD access, creation;
    HANDLE handle;
    int length, flags, fd;
    FILE *stream;

    if (!path || !mode) return NULL;
    if (!external || !*external || same_prefix(external, local) || !same_prefix(path, external)) {
        stream = fopen(path, mode);
        if (stream != NULL) return stream;
    }
    length = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (length <= 0) return NULL;
    wide = (wchar_t *)malloc((size_t)length * sizeof(*wide));
    if (!wide) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, length);
    access = strchr(mode, '+') ? (GENERIC_READ | GENERIC_WRITE) :
             mode[0] == 'r' ? GENERIC_READ : GENERIC_WRITE;
    creation = mode[0] == 'w' ? CREATE_ALWAYS : mode[0] == 'a' ? OPEN_ALWAYS : OPEN_EXISTING;
    handle = CreateFileFromAppW(wide, access, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL, creation, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wide);
    if (handle == INVALID_HANDLE_VALUE) return NULL;
    flags = _O_BINARY | (strchr(mode, '+') ? _O_RDWR : mode[0] == 'r' ? _O_RDONLY : _O_WRONLY);
    if (mode[0] == 'a') flags |= _O_APPEND;
    fd = _open_osfhandle((intptr_t)handle, flags);
    if (fd < 0) { CloseHandle(handle); return NULL; }
    stream = _fdopen(fd, mode);
    if (!stream) _close(fd);
    return stream;
}

int PortUwp_Stat(const char *path, struct stat *info) {
    const char *external = PortUwp_ContentPath();
    const char *local = PortUwp_LocalPath();
    WIN32_FILE_ATTRIBUTE_DATA attributes;
    wchar_t *wide;
    int length;
    if (!path || !info) { errno = EINVAL; return -1; }
    if ((!external || !*external || same_prefix(external, local) || !same_prefix(path, external)) &&
        _stat(path, info) == 0)
        return 0;
    length = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (length <= 0) return -1;
    wide = (wchar_t *)malloc((size_t)length * sizeof(*wide));
    if (!wide) { errno = ENOMEM; return -1; }
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, length);
    if (!GetFileAttributesExFromAppW(wide, GetFileExInfoStandard, &attributes)) {
        free(wide);
        errno = ENOENT;
        return -1;
    }
    free(wide);
    memset(info, 0, sizeof(*info));
    info->st_mode = (attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? _S_IFDIR : _S_IFREG;
    info->st_size = (long)((unsigned long long)attributes.nFileSizeHigh << 32 | attributes.nFileSizeLow);
    return 0;
}
