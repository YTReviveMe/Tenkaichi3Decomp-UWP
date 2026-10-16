#pragma once
#include <SDL3/SDL.h>
#include <string>

SDL_IOStream *PortUwp_OpenIO(const std::string &path, bool write);
bool PortUwp_CreateDirectory(const std::string &path);
bool PortUwp_FileExists(const std::string &path);
