#ifndef BT3_UWP_FILEOPS_H
#define BT3_UWP_FILEOPS_H

#include <io.h>

int PortUwp_Access(const char *path, int mode);
int PortUwp_Rename(const char *source, const char *destination);
int PortUwp_Remove(const char *path);
int PortUwp_Mkdir(const char *path);
intptr_t PortUwp_FindFirst(const char *pattern, struct _finddata_t *entry);
int PortUwp_FindNext(intptr_t search, struct _finddata_t *entry);
int PortUwp_FindClose(intptr_t search);

#define access PortUwp_Access
#define rename PortUwp_Rename
#define remove PortUwp_Remove
#define unlink PortUwp_Remove
#define _mkdir PortUwp_Mkdir
#define _findfirst PortUwp_FindFirst
#define _findnext PortUwp_FindNext
#define _findclose PortUwp_FindClose

#endif
