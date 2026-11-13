#pragma once
#include "../patch/core.h"
#include "../patch/cheats.h"
#include <nds.h>

namespace ui {
enum class Action { None, Launch, Original, Report, ReadCard, ChangeCard, RepairStorage, Cheats, ReadRom, ClearCache };
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
bool setGame(np3d::SourcePtr card, const sNDSHeader& header, bool romFile = false);
void noCard(bool missing = false, bool reinsert = false);
Action update();
const std::vector<std::string>& selectedPatches();
const std::string& selectedRom();
int apPatchMode();
bool skipRomMd5Enabled();
int limitOpenFiles();
int updateDisplayWhileReading();
const np3d::CheatSelection& selectedCheats();
void showCheats(np3d::SourcePtr rom, np3d::SourcePtr card);
std::string cheatError(np3d::CheatStatus status);
void preparing();
void progress(const char* stage, uint32_t completed, uint32_t total, const char* detail, uint64_t elapsed,
              const PreparationProgress& preparation);
void report(bool working);
void clearCache();
void repairStorage();
bool confirm(const std::string& detail);
void error(const std::string& detail, bool mismatch = false);
[[noreturn]] void fatal(const std::string& detail);
}
