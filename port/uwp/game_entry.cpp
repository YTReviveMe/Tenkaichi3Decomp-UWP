#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdint.h>
#include <stdio.h>
#include <windows.h>

extern "C" int __wrap_main(int argc, char **argv);
extern "C" int PortUwp_ReserveFixedMemory(void);
extern "C" const char *PortUwp_FixedMemoryError(void);
extern "C" void PortUwp_Trace(const char *message);

int main(int argc, char **argv) {
    OutputDebugStringA("BT3 startup: entered app main\n");
    SDL_SetLogOutputFunction([](void *, int, SDL_LogPriority, const char *message) {
        PortUwp_Trace(message);
    }, nullptr);
    const uintptr_t image_base = (uintptr_t)GetModuleHandleW(nullptr);
    if (image_base < 0x1F000000ULL || image_base >= 0xE0000000ULL) {
        char error[256];
        _snprintf_s(error, sizeof(error), _TRUNCATE,
                    "The game executable loaded outside its required low-address range (0x%llx).",
                    (unsigned long long)image_base);
        PortUwp_Trace(error);
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
            SDL_Window *window = SDL_CreateWindow("Tenkaichi 3 startup error", 1280, 720, 0);
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Tenkaichi 3 startup error", error, window);
            if (window) SDL_DestroyWindow(window);
            SDL_Quit();
        }
        return 2;
    }
    if (!PortUwp_ReserveFixedMemory()) {
        const char *error = PortUwp_FixedMemoryError();
        PortUwp_Trace(error);
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
            SDL_Window *window = SDL_CreateWindow("Tenkaichi 3 startup error", 1280, 720, 0);
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Tenkaichi 3 startup error", error, window);
            if (window) SDL_DestroyWindow(window);
            SDL_Quit();
        }
        return 2;
    }
    PortUwp_Trace("BT3 startup: fixed memory reserved");
    char image_message[96];
    _snprintf_s(image_message, sizeof(image_message), _TRUNCATE,
                "BT3 startup: image base 0x%llx", (unsigned long long)image_base);
    PortUwp_Trace(image_message);
    // Set WGI before SDL_Init.
    if (SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_WGI, "1", SDL_HINT_OVERRIDE))
        PortUwp_Trace("BT3 startup: Windows Gaming Input enabled");
    else
        PortUwp_Trace("BT3 startup: failed to enable Windows Gaming Input");
    return __wrap_main(argc, argv);
}
