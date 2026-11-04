#pragma once
#include "../patch/core.h"

struct GameLaunchSettings {
    bool dsiMode, boostCpu;
    std::string guiLanguage;
};
GameLaunchSettings gameLaunchSettings(bool dsiHardware, uint8_t unitCode, const char* gameCode, const std::string& language);
void writePatchedLaunch(const np3d::Prepared& prepared, np3d::SourcePtr card, uint32_t chipId, int consoleModel, bool apPatch, const std::string& appPath, const std::string& sourcePath);
void startPatchedGame();
