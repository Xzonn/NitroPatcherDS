#include <nds/ndstypes.h>
#include <nds/system.h>
#include <nds/input.h>
#include <nds/interrupts.h>
#include <nds/ipc.h>
#include <nds/card.h>
#include <nds/memory.h>
#include <string.h>

#include "my_fat.h"
#include "my_sdmmc.h"

static aFile traceFile;
static u32 records[128][32];
static u32 sequence;
static u32 frames;
static u32 outputOffset;
static u32 bootReadIndex;
static u32 pauseFrames;
static bool paused;
static u32 sector[128];
static u32 verifySector[128];

extern u8 __bss_start[];
extern u8 __bss_end[];

__asm__(".section .bss\n.balign 8\ntraceIrqStack:\n.space 8192\n"
        ".global traceIrqStackEnd\ntraceIrqStackEnd:\n.previous\n");

static bool writeTrace(const void* data, u32 offset, u32 length) {
    const char* bytes = data;
    for (u32 position = 0; position < length; position += 512) {
        if (fileWrite(bytes + position, &traceFile, offset + position, 512) != 512 || (getMMCDevice(1)->error & 4)) {
            return false;
        }
    }
    return true;
}

bool traceInit(u32 cluster, const void* header) {
    memset(__bss_start, 0, __bss_end - __bss_start);
    if (!FAT_InitFiles(true)) {
        return false;
    }
    getFileFromCluster(&traceFile, cluster);
    outputOffset = 0x8000;
    sector[0] = 0x4433504e;
    sector[1] = 2;
    sector[2] = outputOffset;
    sector[3] = 0x400000;
    sector[4] = sizeof(records[0]);
    sector[5] = 128;
    sector[6] = 512;
    sector[7] = 0; // The diagnostic observes the original AP checks.
    if (!writeTrace(sector, 0, sizeof(sector))) {
        return false;
    }
    resetPrevSect(&traceFile);
    if (fileRead((char*)verifySector, &traceFile, 0, sizeof(verifySector)) != sizeof(verifySector) ||
        (getMMCDevice(1)->error & 4) || memcmp(sector, verifySector, sizeof(sector)) != 0) {
        return false;
    }
    return writeTrace(header, 512, 512) && writeTrace((const void*)0x027FFC00, 1024, 512);
}

bool traceBootRead(u32 address, const void* data) {
    const u32 offset = 0x1000 + bootReadIndex * 1024;
    if (data) {
        bootReadIndex++;
        return writeTrace(data, offset + 512, 512);
    }
    memset(sector, 0, sizeof(sector));
    sector[0] = 0x44525041;
    sector[1] = address;
    sector[2] = REG_EXMEMSTAT;
    sector[3] = REG_ROMCTRL;
    sector[4] = REG_SCFG_EXT;
    return writeTrace(sector, offset, sizeof(sector));
}

bool traceBootComplete(u32 handler) {
    memset(sector, 0, sizeof(sector));
    sector[0] = 0x544f4f42;
    sector[1] = handler;
    sector[2] = REG_SCFG_EXT;
    sector[3] = REG_SCFG_CLK;
    return writeTrace(sector, 0x3000, sizeof(sector));
}

static void flushTrace(void) {
    if (!writeTrace(records, outputOffset, sizeof(records))) {
        REG_IME = 0;
        while (1) {
        }
    }
    outputOffset += sizeof(records);
    if (outputOffset == 0x408000) {
        outputOffset = 0x8000;
    }
}

u32* traceIrq(u32* saved) {
    if (paused) {
        return saved;
    }
    const u32 pending = REG_IF;
    const bool vblank = (pending & REG_IE & IRQ_VBLANK) != 0;
    if (vblank) {
        frames++;
    }
    u32* record = records[sequence & 127];
    record[0] = ++sequence;
    record[1] = frames;
    record[2] = saved[19]; // BIOS IRQ return address, before subtracting four.
    record[3] = REG_IE;
    record[4] = pending;
    record[5] = REG_IME;
    record[6] = REG_IPC_SYNC;
    record[7] = REG_IPC_FIFO_CR;
    record[8] = REG_EXMEMSTAT;
    record[9] = REG_AUXSPICNT;
    record[10] = REG_ROMCTRL;
    record[11] = REG_SCFG_EXT;
    record[12] = REG_SCFG_CLK;
    record[13] = REG_VCOUNT;
    const volatile u32* arm9 = (const volatile u32*)0x02000200;
    for (unsigned int index = 0; index < 14; index++) {
        record[14 + index] = arm9[index];
    }
    record[28] = *(vu32*)0x027FFFA8;
    record[29] = *(vu32*)0x027FFFC0;
    record[30] = *(vu32*)0x027FFFC4;
    record[31] = *(vu32*)0x027FFFD0;
    if (vblank) {
        if ((REG_KEYINPUT & (KEY_L | KEY_R | KEY_UP)) == 0 && (*(vu16*)0x04000136 & 1) == 0) {
            pauseFrames++;
        } else {
            pauseFrames = 0;
        }
        if (pauseFrames == 120) {
            paused = true;
            flushTrace();
        } else if ((frames % 60) == 0) {
            flushTrace();
        }
    }
    return saved;
}
