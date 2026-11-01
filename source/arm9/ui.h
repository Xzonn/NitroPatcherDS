#pragma once
#include "../patch/core.h"
#include <nds.h>

namespace ui {
enum class Action { None, Launch, Original, Report, ReadCard, ChangeCard, RepairStorage };
struct PreparationProgress {
    uint32_t fileNumber = 0, fileCount = 0;
    int64_t remaining = -1;
    uint64_t completed = 0, total = 0;
    bool bytes = false;
};
void showStartup();
void initialize();
bool ready();
void chooseInitialLanguage();
bool setGame(np3d::SourcePtr card, const sNDSHeader& header);
void noCard(bool missing = false, bool reinsert = false);
Action update();
const std::vector<std::string>& selectedPatches();
bool apPatchEnabled();
bool skipRomMd5Enabled();
bool limitOpenFilesEnabled();
void preparing();
void progress(const char* stage, uint32_t completed, uint32_t total, const char* detail, uint64_t elapsed, const PreparationProgress& preparation);
void report(bool working);
void repairStorage();
bool confirm(const std::string& detail);
void error(const std::string& detail, bool mismatch = false);
[[noreturn]] void fatal(const std::string& detail);
}
