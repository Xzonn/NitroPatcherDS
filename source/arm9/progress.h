#pragma once
#include "../patch/core.h"

void beginPatchProgress();
void planPatchProgress(np3d::SourcePtr card, const std::vector<std::string>& paths, bool dsOnly = false);
void finishPatchProgress(bool reused);
