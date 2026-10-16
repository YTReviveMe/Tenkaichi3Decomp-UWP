#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "storage.h"
#include <fstream>
#include <string>

static void write_storage_report(bool ready) {
    const char *local = PortUwp_LocalPath();
    if (!local || !*local) return;
    std::ofstream report(std::string(local) + "\\platform-check.txt", std::ios::trunc);
    if (!report) return;
    report << "LocalFolder: " << local << '\n';
    report << "Storage ready: " << (ready ? "yes" : "no") << '\n';
    report << "Content root: " << PortUwp_ContentPath() << '\n';
    report << "Game data: " << PortUwp_DataPath() << '\n';
    report << "Storage result: " << PortUwp_StorageError() << '\n';
}

int main(int, char **) {
    const bool content_ready = PortUwp_InitStorage() != 0;
    write_storage_report(content_ready);
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) return 1;
    SDL_Window *window = SDL_CreateWindow("Tenkaichi 3 Xbox platform check", 1280, 720, SDL_WINDOW_FULLSCREEN);
    if (!window) return 2;
    SDL_GPUDevice *gpu = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL, false, "direct3d12");
    if (!gpu || !SDL_ClaimWindowForGPUDevice(gpu, window)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "GPU unavailable", SDL_GetError(), window);
        return 3;
    }
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST)) {
                running = false;
            }
        }
        SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(gpu);
        SDL_GPUTexture *surface = nullptr;
        if (!commands || !SDL_WaitAndAcquireGPUSwapchainTexture(commands, window, &surface, nullptr, nullptr)) break;
        if (surface) {
            SDL_GPUColorTargetInfo target{};
            target.texture = surface;
            target.load_op = SDL_GPU_LOADOP_CLEAR;
            target.store_op = SDL_GPU_STOREOP_STORE;
            target.clear_color = content_ready ? SDL_FColor{0.02f, 0.25f, 0.12f, 1.0f}
                                               : SDL_FColor{0.32f, 0.14f, 0.02f, 1.0f};
            SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
            if (pass) SDL_EndGPURenderPass(pass);
        }
        SDL_SubmitGPUCommandBuffer(commands);
    }
    SDL_ReleaseWindowFromGPUDevice(gpu, window);
    SDL_DestroyGPUDevice(gpu);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
