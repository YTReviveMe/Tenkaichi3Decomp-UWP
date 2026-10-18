#ifndef BT3_UWP_STORAGE_H
#define BT3_UWP_STORAGE_H

#ifdef __cplusplus
extern "C" {
#endif

int PortUwp_ConfigureStorage(const char *local_path, const char *external_path);
int PortUwp_ApplyStorageEnvironment(void);
int PortUwp_InitStorage(void);
int PortUwp_EnsureDirectory(const char *path);
const char *PortUwp_DataPath(void);
const char *PortUwp_LocalPath(void);
const char *PortUwp_SavePath(void);
const char *PortUwp_SettingsPath(void);
const char *PortUwp_ContentPath(void);
const char *PortUwp_StorageError(void);
void PortUwp_SetStorageError(const char *message);
void PortUwp_Trace(const char *message);
int PortUwp_RequestLanguageSetup(void);

#ifdef __cplusplus
}
#endif
#endif
