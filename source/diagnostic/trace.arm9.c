#undef ARM7
#define ARM9
#include <nds/ndstypes.h>

extern u32 traceBlobStart[];
extern u32 traceBlobEnd[];
extern u32 traceBlobIRQ[];
extern u32 traceBlobCommand[];
extern u32 traceBlobOverlay[];
extern u32 traceBlobOriginalIRQ[];
extern void traceAutoload(void);

static u32 branch(u32 address, u32 target, bool link) {
    return (link ? 0xeb000000 : 0xea000000) | (((target - address - 8) >> 2) & 0xffffff);
}

static void publishCode(u32 address) {
    __asm__ volatile("mcr p15, 0, %0, c7, c10, 1\n"
                     "mcr p15, 0, %0, c7, c10, 4\n"
                     "mcr p15, 0, %0, c7, c5, 1"
                     :
                     : "r"(address)
                     : "memory");
}

static void stopDiagnostic(u32 code) {
    *(vu32*)0x02000200 = code;
    publishCode(0x02000200);
    while (1) {
    }
}

void traceArm9Prepare(void) {
    if (*(vu32*)0x020008A4 != 0xeb00005c) {
        stopDiagnostic(0xbad00001);
    }
    *(vu32*)0x020008A4 = branch(0x020008A4, (u32)traceAutoload, true);
    publishCode(0x020008A4);
}

void traceArm9Install(void) {
    if (*(vu32*)0x020D8494 != branch(0x020D8494, 0x020D210C, true) || *(vu32*)0x020DCAD8 != 0xe59f3050 ||
        *(vu32*)0x02000948 != 0x01FF8000 || *(vu32*)0x01FF8000 != 0xe92d4000) {
        stopDiagnostic(0xbad00002);
    }
    volatile u32* code = (volatile u32*)0x01FFC000;
    for (u32 index = 0; index < (u32)(traceBlobEnd - traceBlobStart); index++) {
        code[index] = traceBlobStart[index];
    }
    code[traceBlobOriginalIRQ - traceBlobStart] = 0x01FF8004;
    volatile u32* mailbox = (volatile u32*)0x02000200;
    for (u32 index = 0; index < 14; index++) {
        mailbox[index] = 0;
    }
    mailbox[11] = 0xffffffff;
    *(vu32*)0x01FF8000 = branch(0x01FF8000, 0x01FFC000 + (u32)((u8*)traceBlobIRQ - (u8*)traceBlobStart), false);
    *(vu32*)0x020DCAD8 = branch(0x020DCAD8, 0x01FFC000 + (u32)((u8*)traceBlobCommand - (u8*)traceBlobStart), false);
    *(vu32*)0x020D8494 = branch(0x020D8494, 0x01FFC000 + (u32)((u8*)traceBlobOverlay - (u8*)traceBlobStart), true);
    publishCode(0x02000200);
    publishCode(0x02000220);
    publishCode(0x01FF8000);
    publishCode(0x020DCAD8);
    publishCode(0x020D8494);
}
