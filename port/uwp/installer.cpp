#include "installer.h"
#include "audio.h"
#include "iso_io.h"
#include "storage.h"
#include "../setup/native.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_filesystem.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <windows.h>
#include <fileapifromapp.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.UI.Core.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <future>
#include <string>
#include <vector>

namespace {
struct Entry { std::string name, path; bool folder; };
struct Listing { std::vector<Entry> entries; std::string error; };

std::string utf8(const std::wstring &s) {
    int length = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string text(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, text.data(), length, nullptr, nullptr);
    text.pop_back();
    return text;
}

Listing list(const std::string &folder) {
    Listing listing;
    auto pattern = (std::filesystem::path(folder) / "*").wstring();
    WIN32_FIND_DATAW data{};
    HANDLE search = FindFirstFileExFromAppW(pattern.c_str(), FindExInfoBasic, &data,
                                            FindExSearchNameMatch, nullptr, 0);
    if (search == INVALID_HANDLE_VALUE) {
        listing.error = "Cannot browse this folder (Windows error " + std::to_string(GetLastError()) + ").";
        return listing;
    }
    do {
        const std::wstring name(data.cFileName);
        if (name == L"." || name == L"..") continue;
        const bool folder_entry = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        auto extension = std::filesystem::path(name).extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
        if (!folder_entry && extension != L".iso") continue;
        listing.entries.push_back({utf8(name), utf8((std::filesystem::path(folder) / name).wstring()), folder_entry});
    } while (FindNextFileW(search, &data));
    FindClose(search);
    std::sort(listing.entries.begin(), listing.entries.end(), [](const Entry &a, const Entry &b) {
        if (a.folder != b.folder) return a.folder > b.folder;
        return a.name < b.name;
    });
    return listing;
}

std::vector<std::string> external_roots() {
    std::vector<std::string> roots;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        const auto folders = winrt::Windows::Storage::KnownFolders::RemovableDevices().GetFoldersAsync().get();
        for (const auto &folder : folders) roots.push_back(winrt::to_string(folder.Path()));
    } catch (...) {}
    return roots;
}

std::string with_slash(std::string path) {
    if (!path.empty() && path.back() != '\\' && path.back() != '/') path += '\\';
    return path;
}
}

std::vector<std::string> PortUwp_ExternalRoots() {
    return std::async(std::launch::async, external_roots).get();
}

bool PortUwp_RunInstaller(const std::string &internal_root, std::string &installed_root,
                          const std::string &existing_root) {
    PortUwp_Trace("Starting BT3 ISO installer");
    SDL_SetLogOutputFunction([](void *, int, SDL_LogPriority, const char *message) {
        PortUwp_Trace(message);
    }, nullptr);
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
    PortUwp_Trace("Initializing SDL video and controller input");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
        const std::string error = std::string("SDL startup failed: ") + SDL_GetError();
        PortUwp_SetStorageError(error.c_str());
        PortUwp_Trace(error.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Tenkaichi 3 setup", error.c_str(), nullptr);
        return false;
    }
    PortUwp_Trace("Creating setup window and D3D12 device");
    SDL_Window *window = SDL_CreateWindow("Tenkaichi 3 Setup", 1280, 720, 0);
    SDL_GPUDevice *gpu = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL, false, nullptr);
    if (!window || !gpu || !SDL_ClaimWindowForGPUDevice(gpu, window)) {
        const std::string error = std::string("Setup graphics failed: ") + SDL_GetError();
        PortUwp_SetStorageError(error.c_str());
        PortUwp_Trace(error.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Tenkaichi 3 setup", error.c_str(), window);
        if (gpu) SDL_DestroyGPUDevice(gpu);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigNavCursorVisibleAlways = true;
    const std::string asset_root = SDL_GetBasePath() ? SDL_GetBasePath() : "";
    ImFont *body_font = io.Fonts->AddFontFromFileTTF(
        (asset_root + "fonts/dejavu/DejaVuSansCondensed.ttf").c_str(), 22.0f);
    ImFont *title_font = io.Fonts->AddFontFromFileTTF(
        (asset_root + "fonts/fira/FiraSansCondensed-Bold.ttf").c_str(), 42.0f);
    if (!body_font) {
        PortUwp_Trace("Setup font unavailable; using vector fallback");
        body_font = io.Fonts->AddFontDefaultVector();
    }
    if (!title_font) title_font = body_font;
    io.FontDefault = body_font;
    ImGui::StyleColorsDark();
    auto &style = ImGui::GetStyle();
    style.FontSizeBase = 22.0f;
    style.WindowPadding = ImVec2(30, 22);
    style.FramePadding = ImVec2(14, 10);
    style.ItemSpacing = ImVec2(12, 10);
    style.WindowRounding = 16.0f;
    style.ChildRounding = 10.0f;
    style.FrameRounding = 8.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.ScrollbarSize = 18.0f;
    auto &colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.035f, 0.075f, 0.14f, 0.97f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.055f, 0.11f, 0.19f, 0.94f);
    colors[ImGuiCol_Border] = ImVec4(0.22f, 0.45f, 0.68f, 0.85f);
    colors[ImGuiCol_Text] = ImVec4(0.94f, 0.97f, 1.0f, 1.0f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.59f, 0.69f, 0.78f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.10f, 0.25f, 0.41f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.16f, 0.43f, 0.68f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.21f, 0.55f, 0.82f, 1.0f);
    colors[ImGuiCol_Header] = ImVec4(0.12f, 0.37f, 0.61f, 0.80f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.18f, 0.51f, 0.80f, 1.0f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.23f, 0.61f, 0.91f, 1.0f);
    colors[ImGuiCol_NavCursor] = ImVec4(0.43f, 0.79f, 1.0f, 1.0f);
    colors[ImGuiCol_Separator] = ImVec4(0.17f, 0.40f, 0.65f, 0.9f);
    bool ready = ImGui_ImplSDL3_InitForSDLGPU(window);
    ImGui_ImplSDLGPU3_InitInfo info{};
    info.Device = gpu;
    info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(gpu, window);
    info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    ready = ready && ImGui_ImplSDLGPU3_Init(&info);
    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_AutoAll, nullptr, 0);
    if (!ready) {
        const std::string error = "Setup UI initialization failed";
        PortUwp_SetStorageError(error.c_str());
        PortUwp_Trace(error.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Tenkaichi 3 setup", error.c_str(), window);
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        SDL_ReleaseWindowFromGPUDevice(gpu, window);
        SDL_DestroyGPUDevice(gpu);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }

    const std::string internal_parent = with_slash(std::filesystem::path(internal_root).parent_path().string());
    const bool add_languages = !existing_root.empty();
    std::string browse_path = internal_root, source_root = internal_parent;
    std::string selected_iso, selected_eu, destination = existing_root;
    std::string status = add_languages ? "Choose a European ISO (SLES-54945)." :
                                         "Choose an ISO of the unmodified USA release.";
    std::vector<Entry> entries;
    std::vector<std::string> drives;
    std::future<Listing> pending;
    std::future<std::vector<std::string>> pending_drives;
    bool first_focus = true, running = false, done = false, quit = false, choosing_eu = add_languages;
    bool controller_present = false, controller_reported = false;
    auto browse = [&](std::string path) {
        if (pending.valid()) return;
        browse_path = std::move(path);
        entries.clear();
        status = "Reading " + browse_path;
        pending = std::async(std::launch::async, list, browse_path);
    };
    auto clear_selected = [&] {
        if (choosing_eu) selected_eu.clear();
        else { selected_iso.clear(); destination.clear(); }
    };
    PortUwp_CreateDirectory(internal_root);
    browse(internal_root);
    PortUwp_Trace("ISO browser ready");
    PortUwp_AudioStart();

    while (!quit && !done) {
        SDL_Event event;
        bool back = false;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) quit = true;
            if (event.type == SDL_EVENT_GAMEPAD_ADDED) PortUwp_Trace("Setup: gamepad connected");
            if (event.type == SDL_EVENT_GAMEPAD_REMOVED) PortUwp_Trace("Setup: gamepad disconnected");
            if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST)
                back = true;
        }
        if (back && !running) {
            const auto parent = std::filesystem::path(browse_path).parent_path().string();
            if (std::filesystem::path(parent).lexically_normal().wstring().size() >=
                std::filesystem::path(source_root).lexically_normal().wstring().size()) browse(parent);
        }
        if (auto core = winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread())
            core.Dispatcher().ProcessEvents(winrt::Windows::UI::Core::CoreProcessEventsOption::ProcessAllIfPresent);
        if (pending.valid() && pending.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            auto listing = pending.get();
            entries = std::move(listing.entries);
            status = listing.error.empty() ? (entries.empty() ? "No ISO found here. Browse another folder." :
                            choosing_eu ? "Choose the European ISO." : "Choose the USA ISO.") : listing.error;
        }
        if (pending_drives.valid() && pending_drives.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            drives = pending_drives.get();
            if (drives.empty()) status = "No removable drive was found.";
            else if (drives.size() == 1) {
                source_root = with_slash(drives[0]);
                browse(source_root);
            } else status = "Select a drive below.";
        }
        std::string line;
        while (Native_Poll(line)) {
            PortUwp_Trace(line.c_str());
            if (line.rfind("@stopped ", 0) == 0) {
                status = line.substr(9);
                for (size_t at = 0; (at = status.find("\\n", at)) != std::string::npos;)
                    status.replace(at, 2, "\n");
            } else if (line.rfind("@note ", 0) == 0 || line.rfind("@ok ", 0) == 0 ||
                       line.rfind("@step ", 0) == 0) status = line.substr(line.find(' ') + 1);
            else if (line.rfind("@done ", 0) == 0) { done = true; installed_root = destination; }
        }
        if (running && !Native_Running()) running = false;

        ImGui_ImplSDLGPU3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        const bool gamepad_now = (io.BackendFlags & ImGuiBackendFlags_HasGamepad) != 0;
        if (!controller_reported || gamepad_now != controller_present) {
            controller_present = gamepad_now;
            controller_reported = true;
            PortUwp_Trace(controller_present ? "Setup: controller ready" : "Setup: waiting for controller");
        }
        ImGui::NewFrame();
        const ImVec2 display = io.DisplaySize;
        auto *background = ImGui::GetBackgroundDrawList();
        background->AddRectFilledMultiColor(ImVec2(0, 0), display,
            IM_COL32(4, 20, 43, 255), IM_COL32(9, 39, 76, 255),
            IM_COL32(2, 13, 30, 255), IM_COL32(5, 26, 53, 255));
        background->AddRectFilled(ImVec2(0, display.y * 0.13f),
            ImVec2(display.x, display.y * 0.135f), IM_COL32(20, 87, 151, 80));
        ImGui::SetNextWindowPos(ImVec2(display.x * 0.05f, display.y * 0.03f));
        ImGui::SetNextWindowSize(ImVec2(display.x * 0.90f, display.y * 0.94f));
        ImGui::Begin("BT3 Setup", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                           ImGuiWindowFlags_NoSavedSettings);
        ImGui::PushFont(title_font, 42.0f);
        ImGui::TextUnformatted("TENKAICHI 3 DECOMP");
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 320.0f);
        ImGui::TextColored(ImVec4(0.49f, 0.79f, 1.0f, 1.0f), "UWP port by: ReviveMe");
        ImGui::TextColored(ImVec4(0.59f, 0.76f, 0.91f, 1.0f),
                           add_languages ? "ADD EUROPEAN LANGUAGES  /  XBOX DEV MODE" :
                                           "SELECT GAME DISC  /  XBOX DEV MODE");
        ImGui::Separator();
        ImGui::TextWrapped(add_languages ?
            "Select your European disc image. The installed USA game stays in place." :
            "Select the USA game disc. A European disc can optionally add Spanish, German, French and Italian.");
        ImGui::Spacing();
        ImGui::BeginDisabled(running);
        const float source_width = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;
        const bool internal_selected = source_root == internal_parent;
        if (internal_selected)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.39f, 0.65f, 1.0f));
        if (ImGui::Button("INTERNAL  |  D:\\DevelopmentFiles\\BT3", ImVec2(source_width, 54))) {
            source_root = internal_parent;
            browse(internal_root);
            clear_selected();
        }
        if (internal_selected) ImGui::PopStyleColor();
        if (first_focus) { ImGui::SetKeyboardFocusHere(-1); first_focus = false; }
        ImGui::SameLine();
        if (!internal_selected)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.39f, 0.65f, 1.0f));
        if (ImGui::Button("EXTERNAL DRIVE", ImVec2(source_width, 54))) {
            if (!pending_drives.valid()) {
                drives.clear();
                pending_drives = std::async(std::launch::async, external_roots);
                status = "Searching for removable drives...";
            }
            clear_selected();
        }
        if (!internal_selected) ImGui::PopStyleColor();
        if (!drives.empty()) {
            for (const auto &drive : drives) {
                if (ImGui::Button(drive.c_str())) {
                    source_root = with_slash(drive);
                    browse(source_root);
                    clear_selected();
                }
                ImGui::SameLine();
            }
            ImGui::NewLine();
        }
        if (ImGui::Button("UP ONE FOLDER")) {
            const auto parent = std::filesystem::path(browse_path).parent_path().string();
            const auto limit = std::filesystem::path(source_root).lexically_normal().wstring();
            if (std::filesystem::path(parent).lexically_normal().wstring().size() >= limit.size()) browse(parent);
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.60f, 0.80f, 0.96f, 1.0f), "CURRENT FOLDER");
        ImGui::TextWrapped("%s", browse_path.c_str());
        if (!add_languages) {
            if (ImGui::Button(choosing_eu ? "USA GAME ISO" : "USA GAME ISO  [SELECTED]", ImVec2(250, 42)))
                choosing_eu = false;
            ImGui::SameLine();
            if (ImGui::Button(choosing_eu ? "EUROPEAN ISO  [SELECTED]" : "EUROPEAN ISO  [OPTIONAL]", ImVec2(300, 42)))
                choosing_eu = true;
        }
        ImGui::BeginChild("files", ImVec2(0, display.y * (drives.empty() ? 0.19f : 0.12f)),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
        for (const auto &entry : entries) {
            if (ImGui::Selectable((std::string(entry.folder ? "[Folder] " : "[ISO] ") + entry.name).c_str())) {
                if (entry.folder) browse(entry.path);
                else {
                    if (choosing_eu) {
                        selected_eu = entry.path;
                        status = "European language disc selected.";
                    } else {
                        selected_iso = entry.path;
                        destination = source_root == internal_parent ? internal_root :
                            (std::filesystem::path(source_root) / "BT3").string();
                        status = "Ready to extract to " + destination;
                    }
                }
                break;
            }
        }
        ImGui::EndChild();
        ImGui::EndDisabled();
        ImGui::TextColored(ImVec4(0.49f, 0.79f, 1.0f, 1.0f), "STATUS");
        ImGui::SameLine();
        ImGui::TextWrapped("%s", status.c_str());
        if (!selected_iso.empty()) ImGui::TextWrapped("USA: %s", selected_iso.c_str());
        if (!selected_eu.empty()) ImGui::TextWrapped("European: %s", selected_eu.c_str());
        ImGui::BeginDisabled((add_languages ? selected_eu.empty() : selected_iso.empty()) || running);
        if (ImGui::Button(add_languages ? "ADD LANGUAGES AND PLAY" : "INSTALL AND PLAY", ImVec2(340, 56))) {
            PortUwp_CreateDirectory(destination);
            Native_Start(with_slash(std::filesystem::path(destination).parent_path().string()),
                         selected_iso, selected_eu);
            running = true;
            status = add_languages ? "Reading European disc..." : "Verifying disc image...";
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.59f, 0.76f, 0.91f, 1.0f),
            controller_present ? "D-PAD / STICK  Navigate      A  Choose      B  Back" :
                                 "Waiting for controller. Reconnect it if needed.");
        ImGui::End();
        ImGui::Render();
        SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(gpu);
        SDL_GPUTexture *swap = nullptr;
        if (cmd && SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window, &swap, nullptr, nullptr) && swap) {
            ImGui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), cmd);
            SDL_GPUColorTargetInfo target{};
            target.texture = swap;
            target.clear_color = {0.03f, 0.04f, 0.08f, 1.0f};
            target.load_op = SDL_GPU_LOADOP_CLEAR;
            target.store_op = SDL_GPU_STOREOP_STORE;
            SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
            ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), cmd, pass);
            SDL_EndGPURenderPass(pass);
            SDL_SubmitGPUCommandBuffer(cmd);
        } else if (cmd) SDL_CancelGPUCommandBuffer(cmd);
        SDL_Delay(16);
    }
    if (running || Native_Running()) Native_Cancel();
    Native_Join();
    if (pending.valid()) pending.wait();
    if (pending_drives.valid()) pending_drives.wait();
    SDL_WaitForGPUIdle(gpu);
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_ReleaseWindowFromGPUDevice(gpu, window);
    SDL_DestroyGPUDevice(gpu);
    SDL_DestroyWindow(window);
    // Keep the WGI hint for gameplay.
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS);
    PortUwp_Trace(done ? "ISO installer completed" : "ISO installer closed");
    if (!done) PortUwp_SetStorageError("The game setup was closed before installation finished");
    return done;
}
