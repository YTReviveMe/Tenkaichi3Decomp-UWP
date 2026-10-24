#include "storage.h"
#include "installer.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.Profile.h>

extern "C" int PortUwp_InitStorage(void) {
    PortUwp_Trace("BT3 startup: resolving storage");
    try {
        const auto app_local = winrt::to_string(winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path());
        const bool xbox = winrt::Windows::System::Profile::AnalyticsInfo::VersionInfo().DeviceFamily() == L"Windows.Xbox";
        const auto local = xbox ? std::string("D:\\DevelopmentFiles\\BT3") :
            (std::filesystem::path(app_local) / "BT3").string();
        PortUwp_Trace((std::string("Device: ") + (xbox ? "Xbox" : "Windows") + "; LocalFolder: " + app_local).c_str());
        PortUwp_Trace((std::string("Internal BT3: ") + local).c_str());
        std::string external;
        std::ifstream choice(std::filesystem::path(app_local) / "external-content.txt");
        if (choice) std::getline(choice, external);
        if (!external.empty() && external.back() == '\r') external.pop_back();
        bool configured = PortUwp_ConfigureStorage(local.c_str(), external.c_str()) != 0;
        PortUwp_Trace(configured ? "Prepared content found" : PortUwp_StorageError());
        const auto language_request = std::filesystem::path(app_local) / "add-eu-languages.txt";
        const bool add_languages = configured && std::filesystem::exists(language_request);
        if (add_languages) std::filesystem::remove(language_request);
        if (!configured || add_languages) {
            PortUwp_Trace(add_languages ? "Opening European language setup" : "Opening ISO setup");
            std::string installed;
            if (!PortUwp_RunInstaller(local, installed, add_languages ? PortUwp_ContentPath() : "")) {
                PortUwp_Trace(PortUwp_StorageError());
                return 0;
            }
            external = installed == local ? std::string() : installed;
            configured = PortUwp_ConfigureStorage(local.c_str(), external.c_str()) != 0;
            if (configured) {
                std::ofstream saved(std::filesystem::path(app_local) / "external-content.txt", std::ios::trunc);
                if (saved) saved << external << '\n';
            }
        }
        const bool applied = configured && PortUwp_ApplyStorageEnvironment() != 0;
        if (applied) PortUwp_Trace((std::string("Content root: ") + PortUwp_ContentPath()).c_str());
        PortUwp_Trace(configured ? (applied ? "Storage ready" : "Could not set storage paths") : PortUwp_StorageError());
        return applied;
    } catch (const winrt::hresult_error &e) {
        PortUwp_SetStorageError(winrt::to_string(e.message()).c_str());
        PortUwp_Trace(PortUwp_StorageError());
        return 0;
    } catch (...) {
        PortUwp_SetStorageError("Could not resolve UWP local storage");
        PortUwp_Trace(PortUwp_StorageError());
        return 0;
    }
}

extern "C" int PortUwp_RequestLanguageSetup(void) {
    try {
        const auto local = winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path();
        std::ofstream request(std::filesystem::path(local.c_str()) / "add-eu-languages.txt", std::ios::trunc);
        return request.good();
    } catch (...) {
        return 0;
    }
}
