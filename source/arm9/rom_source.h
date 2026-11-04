#pragma once
#include "../patch/core.h"
#include <nds.h>

np3d::SourcePtr openRomFile(const std::string& path, sNDSHeader& header, std::string& error);
std::string prepareRomSave(const sNDSHeader& header);
