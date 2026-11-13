#include "resources.h"
#include "../patch/core.h"
#include <unistd.h>

namespace np3d {
std::vector<uint8_t> readResource(const char* name) {
    auto source = std::make_shared<Disk>(std::string("boot:/") + name);
    return readBytes(source, 0, source->size);
}

void writeResource(const char* name, const std::string& path) {
    auto source = std::make_shared<Disk>(std::string("boot:/") + name);
    auto versionBytes = readResource((std::string(name) + ".version").c_str());
    std::string version(versionBytes.begin(), versionBytes.end());
    std::string marker = path + ".version";
    if (exists(path) && exists(marker) && Disk(path).size == source->size) {
        auto stored = std::make_shared<Disk>(marker);
        if (stored->size == version.size()) {
            auto bytes = readBytes(stored, 0, stored->size);
            if (std::string(bytes.begin(), bytes.end()) == version) {
                progress("Preparing launch files", 0, 0, true);
                return;
            }
        }
    }
    uint32_t offset = 0;
    uint32_t total = source->size;
    progress("Preparing launch files", 0, total, true);
    {
        Disk output(path + ".new", "wb");
        std::vector<uint8_t> buffer(BufferSize);
        while (offset < total) {
            uint32_t size = std::min<uint32_t>(buffer.size(), total - offset);
            source->read(offset, buffer.data(), size);
            output.write(offset, buffer.data(), size);
            offset += size;
            progress("Preparing launch files", offset, total, true);
        }
        output.sync();
    }
    if (exists(path))
        require(unlink(path.c_str()) == 0, "Cannot replace launch resource");
    require(std::rename((path + ".new").c_str(), path.c_str()) == 0, "Cannot publish launch resource");
    {
        Disk output(marker + ".new", "wb");
        output.write(0, version.data(), version.size());
        output.sync();
    }
    if (exists(marker))
        require(unlink(marker.c_str()) == 0, "Cannot replace launch resource version");
    require(std::rename((marker + ".new").c_str(), marker.c_str()) == 0, "Cannot publish launch resource version");
}

uint32_t resourceSize(const char* name) { return Disk(std::string("boot:/") + name).size; }
}
