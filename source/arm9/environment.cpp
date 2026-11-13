#include "environment.h"
#include "../patch/environment.h"

#include <nds.h>
#include <nds/libversion.h>
#include <malloc.h>
#include <newlib.h>
#include <sys/iosupport.h>
#include <cerrno>

extern "C" char __dtcm_start[], __dtcm_top[], __sp_usr[], __sp_irq[], __sp_svc[], __end__[];

namespace {

struct CpuState {
    uint32_t id, control, dtcm, itcm, dataPermissions, instructionPermissions;
    uint32_t regions[8];
};

__attribute__((target("arm"), noinline)) CpuState readCpuState() {
    CpuState state;
    asm volatile("mrc p15, 0, %0, c0, c0, 0" : "=r"(state.id));
    asm volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(state.control));
    asm volatile("mrc p15, 0, %0, c9, c1, 0" : "=r"(state.dtcm));
    asm volatile("mrc p15, 0, %0, c9, c1, 1" : "=r"(state.itcm));
    asm volatile("mrc p15, 0, %0, c5, c0, 2" : "=r"(state.dataPermissions));
    asm volatile("mrc p15, 0, %0, c5, c0, 3" : "=r"(state.instructionPermissions));
    asm volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(state.regions[0]));
    asm volatile("mrc p15, 0, %0, c6, c1, 0" : "=r"(state.regions[1]));
    asm volatile("mrc p15, 0, %0, c6, c2, 0" : "=r"(state.regions[2]));
    asm volatile("mrc p15, 0, %0, c6, c3, 0" : "=r"(state.regions[3]));
    asm volatile("mrc p15, 0, %0, c6, c4, 0" : "=r"(state.regions[4]));
    asm volatile("mrc p15, 0, %0, c6, c5, 0" : "=r"(state.regions[5]));
    asm volatile("mrc p15, 0, %0, c6, c6, 0" : "=r"(state.regions[6]));
    asm volatile("mrc p15, 0, %0, c6, c7, 0" : "=r"(state.regions[7]));
    return state;
}

__attribute__((target("arm"), noinline)) void recordMemory(FILE* report) {
    uintptr_t position;
    uint32_t status;
    asm volatile("mov %0, sp" : "=r"(position));
    asm volatile("mrs %0, cpsr" : "=r"(status));
    np3d::environmentRecord(report, "cpsr=%08lx sp=%08lx", static_cast<unsigned long>(status),
                            static_cast<unsigned long>(position));
    np3d::environmentRecord(report, "dtcm.linked.start=%p top=%p sp_usr=%p sp_irq=%p sp_svc=%p", __dtcm_start,
                            __dtcm_top, __sp_usr, __sp_irq, __sp_svc);
    uintptr_t base = reinterpret_cast<uintptr_t>(__dtcm_start);
    uintptr_t top = reinterpret_cast<uintptr_t>(__sp_usr);
    np3d::environmentRecord(report, "dtcm.user.capacity=%lu current_sp_above_base=%lu sp_within_user_area=%d",
                            static_cast<unsigned long>(top - base),
                            static_cast<unsigned long>(position >= base ? position - base : 0),
                            position >= base && position <= top);
    auto memory = mallinfo();
    np3d::environmentRecord(report, "program.end=%p heap.start=%p heap.end=%p heap.limit=%p", __end__, getHeapStart(),
                            getHeapEnd(), getHeapLimit());
    np3d::environmentRecord(report, "heap.arena=%u used=%u free_in_arena=%u", unsigned(memory.arena),
                            unsigned(memory.uordblks), unsigned(memory.fordblks));
}

}

void dumpEnvironment() {
    using np3d::environmentRecord;
    np3d::makeDirectory("sd:/nitro-patcher");
    np3d::makeDirectory("sd:/nitro-patcher/logs");
    FILE* report = std::fopen("sd:/nitro-patcher/logs/environment.log", "w+b");
    np3d::require(report, "Cannot open environment report");
    environmentRecord(report, "NitroPatcherDS environment report format=1");
    environmentRecord(report, "compiler=%s build=%s %s libnds=%s newlib=%d.%d diagnostic=%d", __VERSION__, __DATE__,
                      __TIME__, _LIBNDS_STRING, __NEWLIB__, __NEWLIB_MINOR__, NP3D_DIAGNOSTIC);
    environmentRecord(report, "abi pointer=%u long=%u size_t=%u off_t=%u fpos_t=%u stat=%u devoptab=%u",
                      unsigned(sizeof(void*)), unsigned(sizeof(long)), unsigned(sizeof(size_t)),
                      unsigned(sizeof(off_t)), unsigned(sizeof(fpos_t)), unsigned(sizeof(struct stat)),
                      unsigned(sizeof(devoptab_t)));
    environmentRecord(report, "hardware.begin dsi_mode=%d", isDSiMode());
    environmentRecord(report, "SCFG_ROM=%04x SCFG_CLK=%04x SCFG_EXT=%08lx SCFG_MC=%04x EXMEMCNT=%04x",
                      unsigned(REG_SCFG_ROM), unsigned(REG_SCFG_CLK), static_cast<unsigned long>(REG_SCFG_EXT),
                      unsigned(REG_SCFG_MC), unsigned(REG_EXMEMCNT));
    environmentRecord(report, "IME=%08lx IE=%08lx IF=%08lx", static_cast<unsigned long>(REG_IME),
                      static_cast<unsigned long>(REG_IE), static_cast<unsigned long>(REG_IF));
    environmentRecord(report, "cpu.read.begin");
    CpuState cpu = readCpuState();
    environmentRecord(report, "cpu.id=%08lx control=%08lx dtcm=%08lx itcm=%08lx", static_cast<unsigned long>(cpu.id),
                      static_cast<unsigned long>(cpu.control), static_cast<unsigned long>(cpu.dtcm),
                      static_cast<unsigned long>(cpu.itcm));
    environmentRecord(report, "mpu.data_permissions=%08lx instruction_permissions=%08lx",
                      static_cast<unsigned long>(cpu.dataPermissions),
                      static_cast<unsigned long>(cpu.instructionPermissions));
    for (unsigned int index = 0; index < 8; index++) {
        environmentRecord(report, "mpu.region.%u=%08lx", index, static_cast<unsigned long>(cpu.regions[index]));
    }
    recordMemory(report);
    environmentRecord(report, "filesystem.callbacks.begin");
    const devoptab_t* device = GetDeviceOpTab("sd:/");
    np3d::require(device, "Cannot locate SD filesystem interface");
    environmentRecord(report, "filesystem.device=%s file_state=%u directory_state=%u", device->name,
                      unsigned(device->structSize), unsigned(device->dirStateSize));
#define RECORD_CALLBACK(field)                                                                                         \
    environmentRecord(report, "callback.%s=%08lx", #field,                                                             \
                      static_cast<unsigned long>(reinterpret_cast<uintptr_t>(device->field)))
    RECORD_CALLBACK(open_r);
    RECORD_CALLBACK(close_r);
    RECORD_CALLBACK(read_r);
    RECORD_CALLBACK(write_r);
    RECORD_CALLBACK(seek_r);
    RECORD_CALLBACK(fstat_r);
    RECORD_CALLBACK(stat_r);
    RECORD_CALLBACK(mkdir_r);
    RECORD_CALLBACK(diropen_r);
    RECORD_CALLBACK(dirnext_r);
    RECORD_CALLBACK(dirclose_r);
    RECORD_CALLBACK(rename_r);
    RECORD_CALLBACK(unlink_r);
    RECORD_CALLBACK(fsync_r);
    RECORD_CALLBACK(statvfs_r);
    RECORD_CALLBACK(rmdir_r);
    RECORD_CALLBACK(lstat_r);
    RECORD_CALLBACK(ftruncate_r);
    RECORD_CALLBACK(chmod_r);
    RECORD_CALLBACK(utimes_r);
#undef RECORD_CALLBACK
    environmentRecord(report, "statvfs.begin");
    struct statvfs storage;
    errno = 0;
    int result = statvfs("sd:/", &storage);
    environmentRecord(report, "statvfs result=%d errno=%d", result, errno);
    np3d::require(result == 0, "Cannot query SD filesystem capacity");
    environmentRecord(
        report,
        "storage.block_size=%lu fragment_size=%lu blocks=%llu free_blocks=%llu available_blocks=%llu name_max=%lu",
        static_cast<unsigned long>(storage.f_bsize), static_cast<unsigned long>(storage.f_frsize),
        static_cast<unsigned long long>(storage.f_blocks), static_cast<unsigned long long>(storage.f_bfree),
        static_cast<unsigned long long>(storage.f_bavail), static_cast<unsigned long>(storage.f_namemax));
    np3d::probeFilesystem(report, "sd:/nitro-patcher/logs/environment-work");
    recordMemory(report);
    environmentRecord(report, "environment.report.complete");
    np3d::require(std::fclose(report) == 0, "Cannot close environment report");
}
