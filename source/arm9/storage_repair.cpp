#include "storage_repair.h"
extern "C" {
#include "../../vendor/libfat/source/partition.h"
#include "../../vendor/libfat/source/directory.h"
#include "../../vendor/libfat/source/file_allocation_table.h"
#include "../../vendor/libfat/source/fatfile.h"
}
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>

namespace storage {
namespace {
constexpr const char* Device = "sd:";
constexpr const char* BackupRoot = "sd:/nitro-patcher/repair";
constexpr uint32_t Batch = 16;
bool blocked = false;
struct Frame {
    DIR_ENTRY entry;
    uint32_t cluster;
    bool first;
    std::string path;
};
struct Frames {
    Frame* data = static_cast<Frame*>(std::malloc(sizeof(Frame) * 32));
    Frames() {
        if (data)
            for (int index = 0; index < 32; index++)
                new (data + index) Frame;
    }
    ~Frames() {
        if (data) {
            for (int index = 0; index < 32; index++)
                data[index].~Frame();
            std::free(data);
        }
    }
    Frame& operator[](int index) { return data[index]; }
};
struct View {
    PARTITION part;
    explicit View(PARTITION* live, unsigned int copy) : part(*live) {
        part.fat.fatStart += copy * part.fat.sectorsPerFat;
        part.cache = _FAT_cache_constructor(2, Batch, part.disc, live->cache->endOfPartition, part.bytesPerSector);
        part.readOnly = true;
    }
    ~View() {
        if (part.cache)
            _FAT_cache_destructor(part.cache);
    }
};
struct Buffers {
    alignas(4) unsigned char source[Batch * 512], target[Batch * 512];
};
void notify(Progress progress, Stage stage, unsigned int copy, uint32_t done, uint32_t total) {
    if (progress)
        progress(stage, copy, done, total);
}
PARTITION* partition(Result& result) {
    auto part = _FAT_partition_getPartitionFromPath(Device);
    if (!part || part->readOnly || part->bytesPerSector != 512 || !part->bytesPerCluster ||
        part->filesysType == FS_UNKNOWN || part->fat.lastCluster < 2 || !part->fat.sectorsPerFat ||
        part->cache->fatCopies < 1 || part->cache->fatCopies > 2 || part->fat.fatStart != part->cache->fatStart ||
        uint64_t(part->fat.fatStart) + uint64_t(part->fat.sectorsPerFat) * part->cache->fatCopies > part->dataStart)
        return nullptr;
    unsigned int bits = part->filesysType == FS_FAT12 ? 12 : part->filesysType == FS_FAT16 ? 16 : 32;
    if ((uint64_t(part->fat.lastCluster) + 1) * bits > uint64_t(part->fat.sectorsPerFat) * 512 * 8 ||
        (part->filesysType == FS_FAT32 && (!part->fsInfoSector || part->fsInfoSector >= part->cache->fatStart)))
        return nullptr;
    for (auto file = part->firstOpenFile; file; file = file->nextOpenFile) {
        if (file->write || file->modified) {
            result.status = Status::Busy;
            return nullptr;
        }
    }
    if (!_FAT_cache_flush(part->cache)) {
        result.status = Status::IoError;
        return nullptr;
    }
    result.count = part->cache->fatCopies;
    return part;
}
bool used(const unsigned char* bits, uint32_t cluster) { return bits[cluster / 8] & (1u << (cluster % 8)); }
bool scan(PARTITION* live, unsigned int index, Copy& report, Status& status, Progress progress) {
    View view(live, index);
    auto part = &view.part;
    std::unique_ptr<unsigned char, decltype(&std::free)> bits(
        static_cast<unsigned char*>(std::calloc((uint64_t(part->fat.lastCluster) + 8) / 8, 1)), std::free);
    Frames frames;
    if (!part->cache || !bits || !frames.data) {
        status = Status::Memory;
        return false;
    }
    uint32_t visited = 0;
    auto chain = [&](uint32_t start, uint32_t expected, bool directory, const std::string& path) {
        if (!start && !directory && !expected)
            return true;
        uint32_t length = 0, cluster = start;
        while (cluster != CLUSTER_EOF) {
            if (!_FAT_fat_isValidCluster(part, cluster)) {
                report.invalid++;
                break;
            }
            if (used(bits.get(), cluster)) {
                report.conflicts++;
                break;
            }
            bits.get()[cluster / 8] |= 1u << (cluster % 8);
            length++;
            visited++;
            cluster = _FAT_fat_nextClusterChecked(part, cluster);
            if (cluster == CLUSTER_ERROR) {
                status = Status::IoError;
                return false;
            }
            if ((visited & 1023) == 0)
                notify(progress, Stage::Directories, index, visited, part->fat.lastCluster - 1);
        }
        if (cluster != CLUSTER_EOF || (!directory && length != expected)) {
            if (cluster == CLUSTER_EOF)
                report.invalid++;
            if (report.problem.empty())
                report.problem = path;
            return false;
        }
        return true;
    };
    notify(progress, Stage::Directories, index, 0, part->fat.lastCluster - 1);
    int depth = 0;
    if (part->filesysType == FS_FAT32 && !chain(part->rootDirCluster, 0, true, "/")) {
        if (status == Status::IoError)
            return false;
        depth = -1;
    }
    frames[0].cluster = part->rootDirCluster;
    frames[0].first = true;
    frames[0].path = "/";
    while (depth >= 0) {
        auto& frame = frames[depth];
        errno = 0;
        bool found = frame.first ? _FAT_directory_getFirstEntry(part, &frame.entry, frame.cluster)
                                 : _FAT_directory_getNextEntry(part, &frame.entry);
        frame.first = false;
        if (!found) {
            if (errno == EIO) {
                status = Status::IoError;
                return false;
            }
            if (errno) {
                report.invalid++;
                if (report.problem.empty())
                    report.problem = frame.path;
            }
            depth--;
            continue;
        }
        if (_FAT_directory_isDot(&frame.entry))
            continue;
        std::string path = frame.path + frame.entry.filename;
        struct stat info = {};
        _FAT_directory_entryStat(part, &frame.entry, &info);
        bool directory = _FAT_directory_isDirectory(&frame.entry);
        uint32_t cluster = _FAT_directory_entryGetCluster(part, frame.entry.entryData);
        report.files++;
        bool valid = chain(cluster, (uint64_t(info.st_size) + part->bytesPerCluster - 1) / part->bytesPerCluster,
                           directory, path);
        if (status == Status::IoError)
            return false;
        if (directory && valid) {
            if (depth == 31 || path.size() > 1024) {
                report.invalid++;
                if (report.problem.empty())
                    report.problem = path;
                continue;
            }
            depth++;
            frames[depth].cluster = cluster;
            frames[depth].first = true;
            frames[depth].path = path + "/";
        }
        if ((report.files & 127) == 0)
            notify(progress, Stage::Directories, index, visited, part->fat.lastCluster - 1);
    }
    for (uint32_t cluster = 2; cluster <= part->fat.lastCluster; cluster++) {
        uint32_t next = _FAT_fat_nextClusterChecked(part, cluster);
        if (next == CLUSTER_ERROR) {
            status = Status::IoError;
            return false;
        }
        if (next == CLUSTER_FREE)
            report.free++;
        else if (next != CLUSTER_BAD && !used(bits.get(), cluster))
            report.lost++;
        if ((cluster & 1023) == 0 || cluster == part->fat.lastCluster)
            notify(progress, Stage::Allocation, index, cluster - 1, part->fat.lastCluster - 1);
    }
    return true;
}
bool compare(PARTITION* part, Result& result, Progress progress) {
    if (result.count < 2)
        return true;
    std::unique_ptr<Buffers, decltype(&std::free)> buffers(static_cast<Buffers*>(std::malloc(sizeof(Buffers))),
                                                           std::free);
    if (!buffers) {
        result.status = Status::Memory;
        return false;
    }
    for (uint32_t sector = 0; sector < part->fat.sectorsPerFat; sector += Batch) {
        uint32_t count = std::min(Batch, part->fat.sectorsPerFat - sector);
        if (!part->disc->readSectors(part->fat.fatStart + sector, count, buffers->source) ||
            !part->disc->readSectors(part->fat.fatStart + part->fat.sectorsPerFat + sector, count, buffers->target)) {
            result.status = Status::IoError;
            return false;
        }
        for (uint32_t offset = 0; offset < count; offset++)
            if (std::memcmp(buffers->source + offset * 512, buffers->target + offset * 512, 512))
                result.differentSectors++;
        if ((sector & 127) == 0)
            notify(progress, Stage::Compare, 0, sector, part->fat.sectorsPerFat);
    }
    if (result.differentSectors && result.copies[0].valid() && result.copies[1].valid()) {
        View first(part, 0), second(part, 1);
        if (!first.part.cache || !second.part.cache) {
            result.status = Status::Memory;
            return false;
        }
        for (uint32_t cluster = 2; cluster <= part->fat.lastCluster; cluster++) {
            uint32_t a = _FAT_fat_nextClusterChecked(&first.part, cluster),
                     b = _FAT_fat_nextClusterChecked(&second.part, cluster);
            if (a == CLUSTER_ERROR || b == CLUSTER_ERROR) {
                result.status = Status::IoError;
                return false;
            }
            if (a != b) {
                result.status = Status::Ambiguous;
                return false;
            }
            if ((cluster & 1023) == 0)
                notify(progress, Stage::Compare, 0, cluster - 1, part->fat.lastCluster - 1);
        }
    }
    return true;
}
bool directory(const char* path) {
    if (::mkdir(path, 0777) == 0)
        return true;
    struct stat info = {};
    return errno == EEXIST && ::stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}
bool finish(FILE* file, bool success) {
    if (std::fflush(file) != 0)
        success = false;
    if (::fsync(::fileno(file)) != 0)
        success = false;
    if (std::fclose(file) != 0)
        success = false;
    return success;
}
bool reserve(const std::string& path, uint32_t size) {
    auto file = std::fopen(path.c_str(), "wb");
    if (!file)
        return false;
    bool success = std::fseek(file, size - 1, SEEK_SET) == 0 && std::fputc(0, file) != EOF;
    return finish(file, success);
}
bool snapshot(PARTITION* part, const std::string& path, uint32_t sector, uint32_t length, Buffers& buffers,
              Progress progress) {
    auto file = std::fopen(path.c_str(), "r+b");
    if (!file)
        return false;
    bool success = true;
    for (uint32_t offset = 0; offset < length && success; offset += Batch) {
        uint32_t count = std::min(Batch, length - offset);
        success = part->disc->readSectors(sector + offset, count, buffers.source) &&
                  std::fwrite(buffers.source, 512, count, file) == count;
        if ((offset & 127) == 0)
            notify(progress, Stage::Backup, 0, offset, length);
    }
    if (!finish(file, success))
        return false;
    file = std::fopen(path.c_str(), "rb");
    if (!file)
        return false;
    for (uint32_t offset = 0; offset < length && success; offset += Batch) {
        uint32_t count = std::min(Batch, length - offset);
        success = part->disc->readSectors(sector + offset, count, buffers.source) &&
                  std::fread(buffers.target, 512, count, file) == count &&
                  !std::memcmp(buffers.source, buffers.target, count * 512);
        if ((offset & 127) == 0)
            notify(progress, Stage::Verify, 0, offset, length);
    }
    if (std::fclose(file) != 0)
        success = false;
    return success;
}
}
Result inspect(Progress progress) {
    Result result;
    auto part = partition(result);
    if (!part)
        return result;
    for (unsigned int index = 0; index < result.count; index++)
        if (!scan(part, index, result.copies[index], result.status, progress))
            return result;
    if (!compare(part, result, progress))
        return result;
    if (!result.copies[0].valid()) {
        result.status = Status::Damaged;
        return result;
    }
    if (part->filesysType == FS_FAT32) {
        uint32_t free, next;
        if (!_FAT_partition_getFSinfo(part, &free, &next)) {
            result.status = Status::IoError;
            return result;
        }
        result.fsInfo =
            free != result.copies[0].free || (next != CLUSTER_ERROR && !_FAT_fat_isValidCluster(part, next));
    }
    result.status = result.differentSectors || result.fsInfo ? Status::Repairable : Status::Clean;
    return result;
}
bool writeBlocked() { return blocked; }
Result repair(Progress progress) {
    auto result = inspect(progress);
    if (result.status != Status::Repairable)
        return result;
    auto part = partition(result);
    if (!part)
        return result;
    std::unique_ptr<Buffers, decltype(&std::free)> buffers(static_cast<Buffers*>(std::malloc(sizeof(Buffers))),
                                                           std::free);
    if (!buffers) {
        result.status = Status::Memory;
        return result;
    }
    result.status = Status::IoError;
    // Keep the untouched mirror available until its backup has been verified.
    struct Mirror {
        PARTITION* part;
        unsigned int copies;
        bool complete = false;
        ~Mirror() {
            part->cache->fatCopies = copies;
            if (!complete) {
                _FAT_cache_discard(part->cache);
                part->readOnly = true;
                blocked = true;
            }
        }
    } mirror{part, part->cache->fatCopies};
    part->cache->fatCopies = 1;
    if (!directory(BackupRoot))
        return result;
    for (uint32_t number = 1; number < 100000; number++) {
        auto path = std::string(BackupRoot) + "/" + std::to_string(number);
        if (::mkdir(path.c_str(), 0777) == 0) {
            result.backup = path;
            break;
        }
        if (errno != EEXIST)
            return result;
    }
    if (result.backup.empty())
        return result;
    std::string paths[3] = {result.backup + "/fat-1.bin", result.backup + "/fat-2.bin", result.backup + "/fsinfo.bin"};
    for (unsigned int index = 0; index < result.count; index++)
        if (!reserve(paths[index], part->fat.sectorsPerFat * 512))
            return result;
    if (part->filesysType == FS_FAT32 && !reserve(paths[2], 512))
        return result;
    auto info = std::fopen((result.backup + "/geometry.txt").c_str(), "wb");
    if (!info)
        return result;
    bool written =
        std::fprintf(info, "sector_size=512\nfat_start=%lu\nfat_sectors=%lu\nfat_copies=%u\nfsinfo_sector=%lu\n",
                     static_cast<unsigned long>(part->fat.fatStart),
                     static_cast<unsigned long>(part->fat.sectorsPerFat), result.count,
                     static_cast<unsigned long>(part->filesysType == FS_FAT32 ? part->fsInfoSector : 0)) > 0;
    if (!finish(info, written) || !_FAT_cache_flush(part->cache))
        return result;
    for (unsigned int index = 0; index < result.count; index++)
        if (!snapshot(part, paths[index], part->fat.fatStart + index * part->fat.sectorsPerFat, part->fat.sectorsPerFat,
                      *buffers, progress))
            return result;
    if (part->filesysType == FS_FAT32 && !snapshot(part, paths[2], part->fsInfoSector, 1, *buffers, progress))
        return result;
    Copy current;
    Status scanned = Status::Clean;
    if (!scan(part, 0, current, scanned, progress)) {
        result.status = scanned;
        return result;
    }
    if (!current.valid()) {
        result.status = Status::Damaged;
        return result;
    }
    if (!_FAT_cache_flush(part->cache))
        return result;
    _FAT_cache_invalidate(part->cache);
    for (unsigned int index = 1; index < result.count; index++) {
        for (uint32_t sector = 0; sector < part->fat.sectorsPerFat; sector += Batch) {
            uint32_t count = std::min(Batch, part->fat.sectorsPerFat - sector);
            uint32_t source = part->fat.fatStart + sector, target = source + index * part->fat.sectorsPerFat;
            if (!part->disc->readSectors(source, count, buffers->source) ||
                !part->disc->readSectors(target, count, buffers->target))
                return result;
            if (std::memcmp(buffers->source, buffers->target, count * 512) &&
                (!part->disc->writeSectors(target, count, buffers->source) ||
                 !part->disc->readSectors(target, count, buffers->target) ||
                 std::memcmp(buffers->source, buffers->target, count * 512)))
                return result;
            if ((sector & 127) == 0)
                notify(progress, Stage::Repair, index, sector, part->fat.sectorsPerFat);
        }
    }
    if (part->filesysType == FS_FAT32 && !_FAT_partition_setFSinfo(part, current.free, CLUSTER_ERROR))
        return result;
    part->fat.numberFreeCluster = current.free;
    part->cache->fatCopies = mirror.copies;
    _FAT_cache_invalidate(part->cache);
    auto checked = inspect(progress);
    checked.backup = result.backup;
    if (checked.status == Status::Clean) {
        checked.status = Status::Repaired;
        mirror.complete = true;
    }
    return checked;
}
}
