#include "progress.h"
#include "ui.h"
#include "../patch/core.h"
#include "../patch/preparation_progress.h"
#include "resources.h"
#include <cstring>

namespace {
uint64_t started, rendered;
std::string fileDetail;
ui::PreparationProgress preparation;
np3d::PreparationPlan plan;
np3d::SourcePtr plannedCard;
std::vector<std::string> plannedPaths;
bool plannedDsOnly;

void showProgress(const char* stage, uint32_t completed, uint32_t total, bool bytes, const char* detail) {
    if (std::strcmp(stage,"Planning patch preparation")==0 || std::strcmp(stage,"Using prepared cache")==0) {
        bool reused=std::strcmp(stage,"Using prepared cache")==0;
        plan.initialize(plannedCard,plannedPaths,np3d::resourceSize("bootstrap.nds"),reused,plannedDsOnly);
        for (auto& item : plan.patches) plan.set(item.hash,item.hash.size,item.hash.size);
        plan.nextHash=plan.patches.size();
        if (!reused) return;
    }
    uint64_t now = np3d::preparationMilliseconds();
    bool files = std::strcmp(stage,"Preparing game files")==0;
    bool nested = std::strcmp(stage,"Extracting patch file")==0 || std::strcmp(stage,"Applying patch section")==0
        || std::strcmp(stage,"Applying replacement")==0;
    if (files) {
        bool reset = !preparation.fileCount || total!=preparation.fileCount || completed==0;
        if (reset || fileDetail!=detail) preparation.fileNumber=std::min(completed+1,total);
        preparation.fileCount=total;fileDetail=detail;
    } else {
        if (!nested) {preparation.fileNumber=preparation.fileCount=0;fileDetail.clear();}
    }
    plan.update(stage,completed,total,detail);
    bool finished=std::strcmp(stage,"Patch cache prepared")==0 || std::strcmp(stage,"Patch cache reused")==0;
    if (now-rendered<100 && !finished) return;
    auto work=plan.work();
    preparation.completed=work.completed;preparation.total=work.size;
    preparation.remaining=work.remaining(now-started);
    preparation.bytes=bytes;
    rendered=now;
    ui::progress(stage,completed,total,detail,now-started,preparation);
}
}
void beginPatchProgress() {
    fileDetail.clear();preparation={};plan={};
    plannedCard.reset();plannedPaths.clear();
    started=rendered=np3d::preparationMilliseconds();
    ui::preparing();np3d::setProgressHandler(showProgress);
}
void planPatchProgress(np3d::SourcePtr card,const std::vector<std::string>& paths,bool dsOnly) {
    plannedCard=card;plannedPaths=paths;plannedDsOnly=dsOnly;
}
void finishPatchProgress(bool reused) {
    np3d::progress(reused?"Patch cache reused":"Patch cache prepared",1,1);
    np3d::setProgressHandler(nullptr);
}
