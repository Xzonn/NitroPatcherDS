#pragma once
#include "core.h"
#include <map>

namespace np3d {
struct PreparationWork {
    uint64_t size = 0, completed = 0;
    void update(uint32_t current, uint32_t total);
    int64_t remaining(uint64_t elapsed) const;
};
struct PatchWork {
    PreparationWork hash, files, layout, verify, dsi;
    std::map<std::string, PreparationWork> extraction, application;
    bool finished = false;
};
struct PreparationPlan {
    std::vector<PatchWork> patches;
    PreparationWork card, cache, check, launch, finish;
    PreparationWork aggregate;
    size_t patch = 0, hash = 0, nextHash = 0;
    uint32_t windowCompleted = 0, windowTotal = 0;
    uint64_t windowBase = 0, dsiBase = 0;
    std::string windowName;
    PreparationWork* windowWork = nullptr;
    bool ready = false, cardRead = false, dsi = false;
    void initialize(SourcePtr source, const std::vector<std::string>& paths, uint32_t launchSize, bool reused = false,
                    bool dsOnly = false, bool skipRomMd5 = false);
    void set(PreparationWork& task, uint64_t size, uint64_t completed);
    void update(const char* stage, uint32_t completed, uint32_t total, const char* detail);
    void finishPatch();
    PreparationWork work() const;
};
}
