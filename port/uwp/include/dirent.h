#ifndef BT3_UWP_DIRENT_H
#define BT3_UWP_DIRENT_H

#include <windows.h>
#include <fileapifromapp.h>
#include <stdlib.h>
#include <string.h>
#undef near
#undef far
#undef small

struct dirent { char d_name[1024]; };
typedef struct bt3_dir { HANDLE handle; WIN32_FIND_DATAW data; struct dirent entry; int first; } DIR;

static inline DIR *opendir(const char *path) {
    int len = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (len <= 0) return NULL;
    wchar_t *pattern = (wchar_t *)malloc(((size_t)len + 3) * sizeof(wchar_t));
    DIR *dir = (DIR *)calloc(1, sizeof(*dir));
    if (!pattern || !dir) { free(pattern); free(dir); return NULL; }
    MultiByteToWideChar(CP_UTF8, 0, path, -1, pattern, len);
    size_t at = (size_t)len - 1;
    if (at > 0 && pattern[at - 1] != L'\\' && pattern[at - 1] != L'/') pattern[at++] = L'\\';
    pattern[at++] = L'*';
    pattern[at] = 0;
    dir->handle = FindFirstFileExFromAppW(pattern, FindExInfoBasic, &dir->data,
                                          FindExSearchNameMatch, NULL, 0);
    free(pattern);
    if (dir->handle == INVALID_HANDLE_VALUE) { free(dir); return NULL; }
    dir->first = 1;
    return dir;
}
static inline struct dirent *readdir(DIR *dir) {
    if (!dir) return NULL;
    if (dir->first) dir->first = 0;
    else if (!FindNextFileW(dir->handle, &dir->data)) return NULL;
    if (!WideCharToMultiByte(CP_UTF8, 0, dir->data.cFileName, -1,
                             dir->entry.d_name, sizeof(dir->entry.d_name), NULL, NULL)) return NULL;
    return &dir->entry;
}
static inline int closedir(DIR *dir) {
    if (!dir) return -1;
    BOOL ok = FindClose(dir->handle);
    free(dir);
    return ok ? 0 : -1;
}

#endif
