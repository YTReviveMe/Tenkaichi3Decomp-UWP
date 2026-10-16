#pragma once
#include <string>
#include <vector>

bool PortUwp_RunInstaller(const std::string &internal_root, std::string &installed_root);
std::vector<std::string> PortUwp_ExternalRoots();
