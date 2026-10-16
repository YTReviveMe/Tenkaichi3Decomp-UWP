#ifndef BT3_UWP_AUDIO_H
#define BT3_UWP_AUDIO_H

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

void PortUwp_AudioStart(void);
void PortUwp_AudioRetry(void);
int PortUwp_AudioStatus(void); /* 0 idle, 1 opening, 2 ready, 3 failed */
SDL_AudioDeviceID PortUwp_AudioDevice(void);

#ifdef __cplusplus
}
#endif
#endif
