#pragma once
#include "core.h"
#include "miniz.h"
#include "ui_data.h"
#include <map>

namespace np3d {

struct Package {
    mz_zip_archive archive = {};
#if NP3D_PREPARE_TRACE
    mz_file_read_func reader = nullptr;
    void* readOpaque = nullptr;
#endif
    struct Member {
        unsigned int index;
        std::string name;
    };
    std::map<std::string, Member> entries;
    PatchInfo metadata;
    Workspace* work = nullptr;
    explicit Package(const std::string& path);
    Package(const std::string& path, Workspace& work);
    ~Package();
    bool has(const std::string& name) const;
    bool needsMd5() const;
    bool hasPreprocessing() const;
    SourcePtr delta(const std::string& name, SourcePtr source, const std::string& label);
    std::vector<uint8_t> bytes(const std::string& name);
    SourcePtr replace(const std::string& name, SourcePtr source);
};

}
