#ifndef BT3_UWP_TIME_H
#define BT3_UWP_TIME_H

#include <time.h>
#include <sys/stat.h>
#ifndef S_ISREG
#define S_ISREG(mode) (((mode) & S_IFMT) == S_IFREG)
#endif

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
int clock_gettime(int clock_id, struct timespec *time);
#endif

int nanosleep(const struct timespec *duration, struct timespec *remaining);

#endif
