#include "storage.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

int main() {
    const auto base = std::filesystem::temp_directory_path() / "bt3-uwp-storage-test";
    const auto local = base / "internal";
    const auto external = base / "external";
    std::filesystem::remove_all(base);
    std::filesystem::create_directories(local / "disc" / "BIN");
    std::filesystem::create_directories(external / "disc" / "BIN");

    assert(!PortUwp_ConfigureStorage(local.string().c_str(), nullptr));
    std::ofstream(local / "disc" / "SLUS_216.78").put('1');
    std::ofstream(local / "disc" / "BIN" / "DBZP.BIN").put('1');
    assert(PortUwp_ConfigureStorage(local.string().c_str(), nullptr));
    assert(std::filesystem::path(PortUwp_LocalPath()) == local);
    assert(std::filesystem::path(PortUwp_DataPath()) == local);
    assert(std::filesystem::path(PortUwp_SavePath()) == local / "saves");

    assert(PortUwp_ConfigureStorage(local.string().c_str(), external.string().c_str()));
    assert(std::filesystem::path(PortUwp_DataPath()) == local);
    std::ofstream(external / "disc" / "SLUS_216.78").put('1');
    std::ofstream(external / "disc" / "BIN" / "DBZP.BIN").put('1');
    assert(PortUwp_ConfigureStorage(local.string().c_str(), external.string().c_str()));
    assert(std::filesystem::path(PortUwp_DataPath()) == external);
    assert(std::filesystem::path(PortUwp_SavePath()) == local / "saves");
    assert(PortUwp_ApplyStorageEnvironment());
    assert(std::string(std::getenv("BT3_DATA")) == PortUwp_DataPath());

    std::filesystem::remove_all(base);
}
