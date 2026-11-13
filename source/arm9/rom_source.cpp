#include "rom_source.h"
#include "saveMap.h"
#include <cstring>
#include <sys/stat.h>

np3d::SourcePtr openRomFile(const std::string& path, sNDSHeader& header, std::string& error) {
    struct stat info;
    if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0x8000 ||
        info.st_size > 0x40000000) {
        error = "Cannot open this Nintendo DS ROM.";
        return nullptr;
    }
    auto rom = std::make_shared<np3d::Disk>(path);
    rom->read(0, &header, sizeof(header));
    if (swiCRC16(0xffff, &header, 0x15e) != header.headerCRC16) {
        error = "ROM header checksum mismatch.";
        return nullptr;
    }
    if (header.unitCode != 0 && header.unitCode != 2) {
        error = "DSi-exclusive ROMs and DSiWare are unsupported.";
        return nullptr;
    }
    return rom;
}

std::string prepareRomSave(const sNDSHeader& header) {
    using namespace np3d;
    uint32_t code;
    std::memcpy(&code, header.gameCode, sizeof(code));
    Disk types("boot:/rom-save-types.bin");
    struct {
        uint32_t gameCode, type;
    } entry = {};
    uint32_t first = 0, count = types.size / sizeof(entry), end = count;
    while (first < end) {
        uint32_t middle = first + (end - first) / 2;
        types.read(middle * sizeof(entry), &entry, sizeof(entry));
        if (entry.gameCode < code)
            first = middle + 1;
        else
            end = middle;
    }
    require(first < count, "Unknown save type for this ROM.");
    types.read(first * sizeof(entry), &entry, sizeof(entry));
    require(entry.gameCode == code && entry.type < std::size(sramlen), "Unknown save type for this ROM.");
    char identity[32];
    std::snprintf(identity, sizeof(identity), "%.4s-%04x-%02x", header.gameCode, header.headerCRC16, header.romversion);
    makeDirectory("sd:/nitro-patcher/saves");
    std::string path = std::string("sd:/nitro-patcher/saves/") + identity + ".sav";
    if (exists(path) || sramlen[entry.type] == 0)
        return path;
    {
        Disk output(path + ".new", "wb");
        std::vector<uint8_t> buffer(16384, 0xff);
        for (uint32_t offset = 0, size = sramlen[entry.type]; offset < size;) {
            uint32_t take = std::min<uint32_t>(buffer.size(), size - offset);
            output.write(offset, buffer.data(), take);
            offset += take;
        }
        output.sync();
    }
    require(std::rename((path + ".new").c_str(), path.c_str()) == 0, "Cannot create ROM save file.");
    return path;
}
