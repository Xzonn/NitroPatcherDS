#include <nds.h>
#include <fat.h>
#include <cstdio>
#include <sys/stat.h>
#include "common/nds_loader_arm9.h"
#include "common/nitrofs.h"
#include "np3d_load_bin.h"

[[noreturn]] static void stopWithMessage(const char* message) {
    consoleDemoInit();
    iprintf("%s\n", message);
    while (true)
        swiWaitForVBlank();
}

static void showStartup(uint16_t* upper, uint16_t* lower) {
    FILE* image = std::fopen("boot:/ui/startup.bin", "rb");
    if (!image)
        stopWithMessage("Cannot open startup screen.");
    uint16_t row[256];
    for (int y = 0; y < 192; y++) {
        if (std::fread(row, sizeof(uint16_t), 256, image) != 256)
            stopWithMessage("Cannot read startup screen.");
        for (int x = 0; x < 256; x++)
            upper[y * 256 + x] = row[x];
    }
    for (int y = 0; y < 192; y++) {
        if (std::fread(row, sizeof(uint16_t), 256, image) != 256)
            stopWithMessage("Cannot read startup screen.");
        for (int x = 0; x < 128; x++)
            lower[y * 128 + x] = (row[x * 2] & 31) | ((row[x * 2 + 1] & 31) << 8);
    }
    if (std::fclose(image) != 0)
        stopWithMessage("Cannot close startup screen.");
}

int main() {
    fifoSendValue32(FIFO_PM, PM_REQ_SLEEP_DISABLE);
    videoSetMode(MODE_5_2D);
    videoSetModeSub(MODE_5_2D);
    lcdMainOnTop();
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankH(VRAM_H_SUB_BG);
    vramSetBankI(VRAM_I_SUB_BG_0x06208000);
    uint16_t* upper = bgGetGfxPtr(bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0));
    uint16_t* lower = bgGetGfxPtr(bgInitSub(3, BgType_Bmp8, BgSize_B8_256x256, 0, 0));
    for (int color = 0; color < 32; color++)
        BG_PALETTE_SUB[color] = RGB15(color, color, color);
    dmaFillHalfWords(0xffff, upper, 256 * 192 * 2);
    dmaFillHalfWords(0x1f1f, lower, 256 * 192);
    bgUpdate();
    setBrightness(3, 0);
    if (!fatInitDefault())
        stopWithMessage("Unable to access the SD card.");
    const char* arguments[] = {"sd:/nitro-patcher/app/NitroPatcher3DS.nds"};
    struct stat info;
    if (stat(arguments[0], &info) != 0)
        stopWithMessage("Cannot open NitroPatcher3DS.nds.");
    if (!bootFSInit(arguments[0]))
        stopWithMessage("Cannot open app resources.");
    showStartup(upper, lower);
    int result = runNds(np3d_load_bin, np3d_load_bin_size, info.st_ino, true, false, false, arguments[0], 1, arguments,
                        false, false, true, true, false, false, -1, 0);
    char message[80];
    std::snprintf(message, sizeof(message), "Cannot start NitroPatcherDS: %i", result);
    stopWithMessage(message);
}
