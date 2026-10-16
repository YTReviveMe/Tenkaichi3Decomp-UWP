#include <windows.h>
#include "include/uwp_time.h"

int clock_gettime(int clock_id, struct timespec *time) {
    LARGE_INTEGER counter, frequency;
    if (clock_id != CLOCK_MONOTONIC || !QueryPerformanceCounter(&counter) ||
        !QueryPerformanceFrequency(&frequency)) return -1;
    time->tv_sec = (time_t)(counter.QuadPart / frequency.QuadPart);
    time->tv_nsec = (long)((counter.QuadPart % frequency.QuadPart) * 1000000000LL / frequency.QuadPart);
    return 0;
}

int nanosleep(const struct timespec *duration, struct timespec *remaining) {
    unsigned long long ms = (unsigned long long)duration->tv_sec * 1000ULL +
                            ((unsigned long long)duration->tv_nsec + 999999ULL) / 1000000ULL;
    (void)remaining;
    Sleep(ms > 0xFFFFFFFEULL ? 0xFFFFFFFEUL : (DWORD)ms);
    return 0;
}
