#pragma once
#include <string>

bool PortUwp_RunInstaller(const std::string &internal_root, std::string &installed_root,
                          const std::string &existing_root = {});
