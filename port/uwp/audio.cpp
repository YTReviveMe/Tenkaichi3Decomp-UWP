#include "audio.h"
#include "storage.h"

namespace {
SDL_AtomicInt status;
SDL_AudioDeviceID playback;

int SDLCALL open_playback(void *) {
    PortUwp_Trace("UWP audio: initializing WASAPI playback");
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        PortUwp_Trace(SDL_GetError());
        SDL_SetAtomicInt(&status, 3);
        return 1;
    }
    SDL_AudioDeviceID device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!device || !SDL_ResumeAudioDevice(device)) {
        PortUwp_Trace(SDL_GetError());
        if (device) SDL_CloseAudioDevice(device);
        SDL_SetAtomicInt(&status, 3);
        return 1;
    }
    playback = device;
    PortUwp_Trace("UWP audio: playback device ready");
    SDL_SetAtomicInt(&status, 2);
    return 0;
}
}

extern "C" void PortUwp_AudioStart(void) {
    if (!SDL_CompareAndSwapAtomicInt(&status, 0, 1)) return;
    SDL_Thread *thread = SDL_CreateThread(open_playback, "BT3 UWP audio", nullptr);
    if (thread) SDL_DetachThread(thread);
    else {
        PortUwp_Trace(SDL_GetError());
        SDL_SetAtomicInt(&status, 3);
    }
}

extern "C" void PortUwp_AudioRetry(void) {
    if (SDL_CompareAndSwapAtomicInt(&status, 3, 0)) PortUwp_AudioStart();
}

extern "C" int PortUwp_AudioStatus(void) { return SDL_GetAtomicInt(&status); }

extern "C" SDL_AudioDeviceID PortUwp_AudioDevice(void) {
    return SDL_GetAtomicInt(&status) == 2 ? playback : 0;
}
