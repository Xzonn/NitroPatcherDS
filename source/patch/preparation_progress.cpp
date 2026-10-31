#include "preparation_progress.h"
#include "nds_adapter.h"
#include "package.h"
#include "miniz.h"
#include <cstring>
#include <string_view>

namespace np3d {
void PreparationWork::update(uint32_t current,uint32_t total) {
    if (total) completed=size*std::min(current,total)/total;
}
int64_t PreparationWork::remaining(uint64_t elapsed) const {
    if (size && completed==size) return 0;
    if (!completed || elapsed<1000) return -1;
    return std::max<uint64_t>(1,(size-completed)*elapsed/completed);
}

void PreparationPlan::initialize(SourcePtr source,const std::vector<std::string>& paths,uint32_t launchSize,bool reused,bool dsOnly,bool skipRomMd5) {
    *this={};
    launch.size=launchSize;finish.size=1;
    if (reused) {
        patches.resize(paths.size());
        aggregate.size=launch.size+finish.size;ready=true;
        return;
    }
    bool archives=std::any_of(paths.begin(),paths.end(),[](const std::string& path){return !isDeltaPatch(path);});
    auto header=std::make_unique<Header>();
    std::fill_n(reinterpret_cast<uint8_t*>(header.get()),sizeof(Header),0);
    if (archives) source->read(0,header.get(),std::min<uint32_t>(sizeof(Header),source->size));
    std::map<std::string,uint32_t> sizes;
    sizes["header.bin"]=header->rom_header_size;sizes["banner.bin"]=0x840;
    sizes["arm9.bin"]=uint32_t(header->arm9_size)+12;sizes["arm7.bin"]=header->arm7_size;
    sizes["fnt.bin"]=header->fnt_size;sizes["fat.bin"]=header->fat_size;
    sizes["overarm9.bin"]=header->arm9_overlay_size;sizes["overarm7.bin"]=header->arm7_overlay_size;
    sizes["arm9i.bin"]=header->dsi9_size;sizes["arm7i.bin"]=header->dsi7_size;
    uint64_t fileCount=uint32_t(header->fat_size)/8+8+uint32_t(header->arm9_overlay_size)/sizeof(OverlayEntry)
        +uint32_t(header->arm7_overlay_size)/sizeof(OverlayEntry);
    for (const auto& path : paths) {
        progress("Reading patch directory",patches.size()+1,paths.size(),false,path.c_str());
        patches.emplace_back();auto& item=patches.back();
        item.hash.size=Disk(path).size;
        if (isDeltaPatch(path)) {
            item.application[normalize(path.substr(path.find_last_of('/')+1))].size=source->size;
            cache.size+=source->size;
            continue;
        }
        item.files.size=item.layout.size=fileCount;
        Package package(path);
        if ((!skipRomMd5 || package.hasPreprocessing()) && patches.size()>1 && package.needsMd5()) item.verify.size=source->size;
        if (!dsOnly && package.metadata.formatVersion==2 && header->unitcode==2)
            item.dsi.size=uint64_t(header->digest_ntr_size)+uint32_t(header->digest_twl_size)
                +uint64_t(header->arm9_size)*2+uint32_t(header->arm7_size)
                +uint64_t(header->dsi9_size)*5+uint64_t(header->dsi7_size)*5+uint32_t(header->banner_size);
        for (const auto& member : package.entries) {
            mz_zip_archive_file_stat info;
            require(mz_zip_reader_file_stat(&package.archive,member.second.index,&info),"Cannot read XZP directory");
            const auto& name=member.first;
            if (name=="readme.md" || name=="readme.txt") continue;
            if (skipRomMd5 && name=="md5.txt") continue;
            if (dsOnly && (name=="arm9i.bin" || name=="arm7i.bin"
                || name=="xdelta/arm9i.bin" || name=="xdelta/arm7i.bin")) continue;
            item.extraction[name].size=info.m_uncomp_size;
            if (name.compare(0,7,"xdelta/")==0) {
                auto file=name.substr(7);auto found=sizes.find(file);
                item.application[file].size=found==sizes.end()?info.m_uncomp_size:found->second;
                cache.size+=item.application[file].size;
            } else if (name.compare(0,14,"preprocessing/")==0) item.application["game preprocessing"].size=source->size;
            else if (name!="md5.txt" && name!="metadata.json") {
                item.application[name].size=info.m_uncomp_size;cache.size+=info.m_uncomp_size;
            }
        }
    }
    auto add=[&](const PreparationWork& task){aggregate.size+=task.size;aggregate.completed+=task.completed;};
    for (const auto* task : {&card,&cache,&check,&launch,&finish}) add(*task);
    for (const auto& item : patches) {
        for (const auto* task : {&item.hash,&item.files,&item.layout,&item.verify,&item.dsi}) add(*task);
        for (const auto* tasks : {&item.extraction,&item.application}) for (const auto& task : *tasks) add(task.second);
    }
    ready=true;
}

void PreparationPlan::set(PreparationWork& task,uint64_t size,uint64_t completed) {
    aggregate.size=aggregate.size-task.size+size;
    aggregate.completed=aggregate.completed-task.completed+completed;
    task.size=size;task.completed=completed;
}

void PreparationPlan::finishPatch() {
    auto& item=patches[patch];
    if (item.finished) return;
    item.finished=true;
    for (auto* tasks : {&item.extraction,&item.application})
        for (auto& task : *tasks) set(task.second,task.second.completed,task.second.completed);
    for (auto* task : {&item.files,&item.layout,&item.verify,&item.dsi}) set(*task,task->completed,task->completed);
}

void PreparationPlan::update(const char* stage,uint32_t completed,uint32_t total,const char* detail) {
    if (!ready || patches.empty()) return;
    auto update=[&](PreparationWork& task){if (total) set(task,task.size,task.size*std::min(completed,total)/total);};
    std::string_view name(stage);
    if (name=="Reading Game Card") {cardRead=true;set(card,total,completed);}
    else if (name=="Reading patch") {
        if (!completed) hash=nextHash++;
        update(patches.at(hash).hash);
    } else if (name=="Patch sequence") {
        if (completed>1) finishPatch();
        patch=completed-1;dsi=false;windowName.clear();windowBase=dsiBase=0;
    } else if (name=="Using prepared cache") {
        for (patch=0;patch<patches.size();patch++) finishPatch();
        patch=patches.size()-1;set(cache,0,0);
    } else if (name=="Extracting patch file") update(patches[patch].extraction[normalize(detail)]);
    else if (name=="Applying replacement") {
        auto& task=patches[patch].application[normalize(detail)];set(task,total,completed);
    } else if (name=="Applying patch section") {
        if (windowName!=detail) {
            windowName=detail;windowBase=0;windowCompleted=windowTotal=0;
            windowWork=&patches[patch].application[normalize(detail)];
        }
        else if (!completed && windowCompleted) windowBase+=windowTotal;
        windowCompleted=completed;windowTotal=total;
        auto& task=*windowWork;
        set(task,std::max(task.size,windowBase+total),windowBase+completed);
    } else if (name=="Preparing game files") {
        auto& task=patches[patch].files;set(task,total,completed);
    } else if (name=="Arranging game files") {
        if (total && completed==total) {
            auto& task=patches[patch].layout;set(task,task.size,std::min(task.size,task.completed+1));
        }
    } else if (name=="Preparing DSi files") dsi=true;
    else if (name=="Checking DSi data" || (name=="Checking game data" && dsi)) {
        auto& task=patches[patch].dsi;
        if (!completed) dsiBase=task.completed;
        set(task,std::max(task.size,dsiBase+total),dsiBase+completed);
    } else if (name=="Checking game data") {
        set(patches[patch].verify,total,completed);
    } else if (name=="Writing cache") {finishPatch();update(cache);}
    else if (name=="Preparing launch files") set(launch,total,completed);
    else if (name=="Patch cache reused" || name=="Patch cache prepared") {
        finishPatch();set(cache,cache.completed,cache.completed);set(check,0,0);
        set(launch,launch.size,launch.size);set(finish,finish.size,finish.size);
    }
}

PreparationWork PreparationPlan::work() const { return ready?aggregate:PreparationWork{}; }
}
