#ifndef BT3_UWP_UNISTD_H
#define BT3_UWP_UNISTD_H

#include <windows.h>
#include <io.h>
#include <direct.h>
#undef near
#undef far
#undef small

typedef unsigned int useconds_t;
static inline int usleep(useconds_t microseconds) { Sleep((microseconds + 999) / 1000); return 0; }

#define close _close
#define read _read
#define write _write
#define lseek _lseek
#define chdir _chdir
#define getcwd _getcwd

#endif
