#pragma once
#include "core.h"
#include "file_table.h"
#include "ndstool.h"
#include "overlay.h"
#include "crc.h"
#include "encryption.h"

namespace np3d {

struct NamedDirectory {
    std::string name, rawName;
    uint32_t parent;
    std::vector<uint32_t> files, children;
};
FileTable enumerateFiles(SourcePtr source, const Header& header, Workspace& work, bool canonical = false,
                         std::vector<NamedDirectory>* directories = nullptr);

}
