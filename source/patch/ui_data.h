#pragma once
#include "core.h"
#include "cheats.h"
#include <map>

namespace np3d {

struct PatchInfo {
    std::string name, author, homepage, version, readme;
    bool beta = false;
    uint32_t formatVersion = 1;
    std::string id, error;
};
PatchInfo readPatchMetadata(const std::string& data);
PatchInfo inspectPatch(const std::string& path);
std::vector<std::string> listRoms(const std::string& root);

struct Settings {
    std::string language;
    std::map<std::string, std::vector<std::string>> games;
    std::map<std::string, int> apPatch;
    std::map<std::string, bool> skipRomMd5;
    std::map<std::string, bool> limitOpenFiles;
    std::map<std::string, bool> updateDisplayWhileReading;
    std::map<std::string, CheatSelection> cheats;
};
Settings loadSettings(const std::string& root);
void saveSettings(const std::string& root, const Settings& settings);

}
