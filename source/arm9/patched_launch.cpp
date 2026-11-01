#include "patched_launch.h"
#include "../../vendor/nds-bootstrap/retail/common/include/np3d_rom.h"
#include "common/nds_loader_arm9.h"
#include "resources.h"
#include "i18n.h"
#include "../patch/nds_adapter.h"
#include "../../vendor/nds-bootstrap/retail/arm9/include/twlClockExcludeMap.h"
#include <nds.h>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

namespace {

uint32_t cluster(const std::string& path, uint32_t* size = nullptr)
{
    struct stat info;
    np3d::require(stat(path.c_str(), &info) == 0 && (info.st_size == 0 || (info.st_ino >= 2 && info.st_ino < 0x0ffffff0)),
        "Cannot locate launch file: " + path);
    if (size) *size = info.st_size;
    return info.st_size ? info.st_ino : 0;
}

}

GameLaunchSettings gameLaunchSettings(bool dsiHardware, uint8_t unitCode, const char* gameCode, const std::string& language)
{
    bool boost = dsiHardware;
    for (const auto& code : twlClockExcludeList) if (std::memcmp(gameCode, code, 3) == 0) boost = false;
    std::string guiLanguage = language;
    if (language == "zh-hans") guiLanguage = "zh-CN";
    else if (language == "zh-hant") guiLanguage = "zh-TW";
    return {dsiHardware && unitCode == 2 && boost, boost, guiLanguage};
}

void writePatchedLaunch(const np3d::Prepared& prepared, np3d::SourcePtr card, uint32_t chipId, int consoleModel, bool apPatch, const std::string& appPath)
{
    using namespace np3d;
    makeDirectory("sd:/nitro-patcher/config");
    makeDirectory("sd:/nitro-patcher/runtime");
    writeResource("bootstrap.nds", "sd:/nitro-patcher/runtime/bootstrap.nds");
    if (consoleModel >= 2) {
        makeDirectory("sd:/nitro-patcher/runtime/bootstrap");
        const uint32_t frontendId[] = {0x4e503344, 0x00030004};
        Disk frontend("sd:/nitro-patcher/runtime/bootstrap/srFrontendId.bin", "wb");
        frontend.write(0, frontendId, sizeof(frontendId));
        frontend.sync();
    }
    Np3dLaunchFile launchFile = {};
    auto& launch = launchFile.rom;
    launch.magic = NP3D_LAUNCH_MAGIC;
    launch.version = NP3D_LAUNCH_VERSION;
    require(appPath.size() + 2 < sizeof(launchFile.returnPath), "App path is too long for returning from a game.");
    std::memcpy(launchFile.returnPath, appPath.c_str(), appPath.size() + 1);
    launch.romSize = prepared.rom->size;
    launch.cardSize = card->size;
    launch.cardId = chipId;
    require(prepared.directory.size() < sizeof(launch.cachePath), "Cache path exceeds launch descriptor capacity");
    std::memcpy(launch.cachePath, prepared.directory.c_str(), prepared.directory.size() + 1);
    auto prefix = readBytes(card, 0, 0x8000);
    std::memcpy(&launch.cardControl, prefix.data() + 0x60, 4);
    std::memcpy(&launch.cardCode, prefix.data() + 12, 4);
    sNDSHeader virtualHeader;
    prepared.rom->read(0, &virtualHeader, sizeof(virtualHeader));
    auto settings = gameLaunchSettings(isDSiMode(), virtualHeader.unitCode, reinterpret_cast<const char*>(&launch.cardCode), ui::languageCodes[ui::currentLanguage()]);
    {
        auto& header = *reinterpret_cast<Header*>(prefix.data());
        if (settings.dsiMode) {
            launch.twlStart = (uint32_t(header.offset_0x90) >> 16) * 0x80000u;
            require(launch.twlStart >= 0x8000 && header.total_rom_size > launch.twlStart && header.total_rom_size <= card->size,
                "Invalid DSi cartridge region");
            launch.twlSize = uint32_t(header.total_rom_size) - launch.twlStart;
        }
        std::string path = prepared.directory + "/card-prefix.bin";
        bool reused = false;
        if (exists(path)) {
            auto stored = std::make_shared<Disk>(path);
            reused = stored->size == prefix.size() + launch.twlSize && readBytes(stored, 0, prefix.size()) == prefix;
        }
        if (!reused) {
            {
                Disk output(path + ".new", "wb");
                output.write(0, prefix.data(), prefix.size());
                std::vector<uint8_t> buffer(BufferSize);
                for (uint32_t offset = 0; offset < launch.twlSize;) {
                    uint32_t take = std::min<uint32_t>(buffer.size(), launch.twlSize - offset);
                    progress("Preparing launch files", offset, launch.twlSize, true);
                    card->read(launch.twlStart + offset, buffer.data(), take);
                    output.write(prefix.size() + offset, buffer.data(), take);
                    offset += take;
                }
                output.sync();
            }
            if (exists(path)) require(unlink(path.c_str()) == 0, "Cannot replace card prefix");
            require(std::rename((path + ".new").c_str(), path.c_str()) == 0, "Cannot publish card prefix");
        }
    }
    launch.mapCluster = cluster(prepared.directory + "/map.bin", &launch.mapSize);
    launch.dataCluster = cluster(prepared.directory + "/data.bin", &launch.dataSize);
    launch.prefixCluster = cluster(prepared.directory + "/card-prefix.bin");
    {
        Disk descriptor("sd:/nitro-patcher/config/launch.bin", "wb");
        descriptor.write(0, &launchFile, sizeof(launchFile));
        descriptor.sync();
    }
    const std::string configuration =
        "[NDS-BOOTSTRAP]\nNDS_PATH=np3d:/rom.nds\nQUIT_PATH=" + appPath + "\nSAVE_RELOCATION=0\nDSI_MODE=" + std::to_string(settings.dsiMode)
        + "\nCONSOLE_MODEL=" + std::to_string(consoleModel) + "\nAP_PATCH=" + std::to_string(apPatch)
        + "\nGUI_LANGUAGE=" + settings.guiLanguage + "\nBOOST_CPU=" + std::to_string(settings.boostCpu)
        + "\nBOOST_VRAM=0\nASYNC_CARD_READ=-1\nCARD_READ_DMA=-1\nDEBUG=0\nLOGGING=0\n";
    Disk configurationFile("sd:/nitro-patcher/config/nds-bootstrap.ini", "wb");
    configurationFile.write(0, configuration.data(), configuration.size());
    configurationFile.sync();
#if NP3D_LAUNCH_TRACE
    static const char stage[] = "launcher: launch files ready; starting bootstrap\n";
    Disk trace("sd:/nitro-patcher/logs/launch.log", "wb");
    trace.write(0, stage, sizeof(stage) - 1);
    trace.sync();
#endif
}

void startPatchedGame()
{
    const char* arguments[] = {"sd:/nitro-patcher/runtime/bootstrap.nds"};
    int result = runNdsFile(arguments[0], 1, arguments, isDSiMode(), !isDSiMode(), true, false, isDSiMode(), false, false, -1, 0);
    np3d::fail("Cannot start patched game: " + std::to_string(result));
}
