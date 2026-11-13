#include "core.h"

#include <cerrno>
#include <cstring>
#ifdef ARM9
#include <nds/timers.h>
#else
#include <chrono>
#endif
#include <dirent.h>
#include <iconv.h>
#include "uni_algo/case.h"
#include <sys/stat.h>
#include <unistd.h>

#include "mbedtls/md5.h"
#include "miniz.h"

namespace np3d {

static FatalHandler fatalHandler;
static ProgressHandler progressHandler;
static FILE* preparationLog;
static uint64_t logStarted;
static uint64_t logUpdated;
static uint64_t logFlushed;
static std::string logStage;

#ifdef ARM9
static uint32_t clockTicks;
static uint64_t clockTotal;
#endif

uint64_t preparationMilliseconds() {
#ifdef ARM9
    uint16_t high, low;
    do {
        high = TIMER3_DATA;
        low = TIMER2_DATA;
    } while (high != TIMER3_DATA);
    uint32_t ticks = uint32_t(high) << 16 | low;
    clockTotal += uint32_t(ticks - clockTicks);
    clockTicks = ticks;
    return clockTotal * 1024000 / BUS_CLOCK;
#else
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
#endif
}

void logPreparation(const std::string& message) {
    if (!preparationLog)
        return;
    int savedError = errno;
    FILE* output = preparationLog;
    preparationLog = nullptr;
#ifdef ARM9
    uintptr_t position;
    asm volatile("mov %0, sp" : "=r"(position));
    require(std::fprintf(output, "[%lu s sp=%08lx] %s\n",
                         static_cast<unsigned long>((preparationMilliseconds() - logStarted) / 1000),
                         static_cast<unsigned long>(position), message.c_str()) >= 0,
#else
    require(std::fprintf(output, "[%lu s] %s\n",
                         static_cast<unsigned long>((preparationMilliseconds() - logStarted) / 1000),
                         message.c_str()) >= 0,
#endif
            "Cannot write preparation log");
    uint64_t now = preparationMilliseconds();
    if (now - logFlushed >= 5000) {
        require(std::fflush(output) == 0, "Cannot flush preparation log");
        logFlushed = now;
    }
    preparationLog = output;
    errno = savedError;
}

void beginPreparationLog(const std::string& root, const std::string& patch) {
    makeDirectory(root);
    makeDirectory(root + "/logs");
    preparationLog = std::fopen((root + "/logs/prepare.log").c_str(), "w");
    require(preparationLog, "Cannot open preparation log");
#ifdef ARM9
    TIMER2_CR = 0;
    TIMER3_CR = 0;
    TIMER2_DATA = 0;
    TIMER3_DATA = 0;
    clockTicks = 0;
    clockTotal = 0;
    TIMER3_CR = TIMER_ENABLE | TIMER_CASCADE;
    TIMER2_CR = TIMER_ENABLE | TIMER_DIV_1024;
#endif
    logStarted = logUpdated = logFlushed = preparationMilliseconds();
    logStage.clear();
    logPreparation("BEGIN patch=" + patch);
}

void finishPreparationLog(bool reused, const std::string& error) {
    logPreparation(error.empty() ? (reused ? "SUCCESS cache reused" : "SUCCESS cache prepared") : "ERROR " + error);
    FILE* output = preparationLog;
    preparationLog = nullptr;
    if (output) {
        require(std::fflush(output) == 0 && fsync(fileno(output)) == 0, "Cannot sync preparation log");
        require(std::fclose(output) == 0, "Cannot close preparation log");
    }
#ifdef ARM9
    TIMER2_CR = 0;
    TIMER3_CR = 0;
#endif
}

void setFatalHandler(FatalHandler handler) { fatalHandler = handler; }

void setProgressHandler(ProgressHandler handler) { progressHandler = handler; }

void progress(const char* stage, uint32_t completed, uint32_t total, bool bytes, const char* detail) {
    if (preparationLog) {
        uint64_t now = preparationMilliseconds();
        if (logStage != stage || now - logUpdated >= 5000) {
            logPreparation(std::string(stage) + " | " + std::to_string(completed) + "/" + std::to_string(total) +
                           (bytes ? " bytes | " : " items | ") + detail);
            logStage = stage;
            logUpdated = now;
        }
    }
    if (progressHandler)
        progressHandler(stage, completed, total, bytes, detail);
}

[[noreturn]] void fail(const std::string& message) {
    int error = errno;
    logPreparation("ERROR " + message + " | errno snapshot=" + std::to_string(error) + " (" + std::strerror(error) +
                   ")");
    if (preparationLog) {
        FILE* output = preparationLog;
        preparationLog = nullptr;
        require(std::fflush(output) == 0 && fsync(fileno(output)) == 0, "Cannot sync preparation error log");
        preparationLog = output;
    }
    if (fatalHandler)
        fatalHandler(message.c_str());
    std::fprintf(stderr, "%s\n", message.c_str());
    std::exit(EXIT_FAILURE);
}

void require(bool condition, const std::string& message) {
    if (!condition)
        fail(message);
}

void patchRequire(bool condition, const std::string& message) {
    if (!condition)
        throw PatchError{message};
}

struct FileSlot {
    Disk* owner = nullptr;
    uint32_t age = 0;
};
static std::array<FileSlot, 16> openTables;
static uint32_t fileAge;
static bool mapFileLimit;

void setMapFileLimit(bool enabled) { mapFileLimit = enabled; }

Disk::Disk(const std::string& name, const char* mode, bool limited)
    : path(name), pooled(limited), writable(mode[0] != 'r' || std::strchr(mode, '+')) {
    NP3D_PREPARE_LOG("FILE open begin mode=" + std::string(mode) + " path=" + path);
    open(mode);
    require(std::fseek(handle, 0, SEEK_END) == 0, "Cannot seek " + path);
    long length = std::ftell(handle);
    require(length >= 0 && uint64_t(length) <= 0x40000000, "Invalid file size: " + path);
    size = length;
    position = size;
    NP3D_PREPARE_LOG("FILE open done size=" + std::to_string(size) + " path=" + path);
}

Disk::~Disk() { close(); }

void Disk::open(const char* mode) {
    if (pooled) {
        FileSlot* slot = nullptr;
        for (auto& item : openTables) {
            if (item.owner == this) {
                slot = &item;
                break;
            }
            if (!item.owner)
                slot = &item;
        }
        if (!slot) {
            slot = &openTables[0];
            for (auto& item : openTables)
                if (item.age < slot->age)
                    slot = &item;
            slot->owner->close();
        }
        slot->owner = this;
        slot->age = ++fileAge;
    }
    if (handle)
        return;
    handle = std::fopen(path.c_str(), mode);
    require(handle, "Cannot open " + path);
    position = 0;
    reading = writing = false;
}

void Disk::close() {
    if (!handle)
        return;
    NP3D_PREPARE_LOG("FILE close begin " + path);
    require(std::fclose(handle) == 0, "Cannot close " + path);
    handle = nullptr;
    if (pooled)
        for (auto& item : openTables)
            if (item.owner == this)
                item.owner = nullptr;
    NP3D_PREPARE_LOG("FILE close done " + path);
}

void Disk::read(uint32_t offset, void* output, uint32_t length) {
    if (!(offset <= size && length <= size - offset))
        fail("Read outside " + path);
    if (pooled)
        open(writable ? "r+b" : "rb");
    if ((position != offset || writing) && std::fseek(handle, offset, SEEK_SET) != 0)
        fail("Cannot seek " + path);
    if (std::fread(output, 1, length, handle) != length)
        fail("Cannot read " + path);
    position = offset + length;
    reading = true;
    writing = false;
}

void Disk::write(uint32_t offset, const void* input, uint32_t length) {
    require(offset <= 0x40000000 && length <= 0x40000000 - offset, "Output exceeds ROM size limit");
    if (pooled)
        open(writable ? "r+b" : "rb");
    if ((position != offset || reading) && std::fseek(handle, offset, SEEK_SET) != 0)
        fail("Cannot seek " + path);
    if (std::fwrite(input, 1, length, handle) != length)
        fail("Cannot write " + path);
    position = offset + length;
    writing = true;
    reading = false;
    size = std::max(size, offset + length);
}

void Disk::sync() {
    NP3D_PREPARE_LOG("FILE sync begin " + path);
    if (pooled)
        open(writable ? "r+b" : "rb");
    flush();
    require(fsync(fileno(handle)) == 0, "Cannot sync " + path);
    NP3D_PREPARE_LOG("FILE sync done " + path);
}

void Disk::flush() {
    if (handle && std::fflush(handle) != 0)
        fail("Cannot flush " + path);
}

Cached::Cached(SourcePtr source) : input(source) { size = input->size; }

void Cached::read(uint32_t offset, void* output, uint32_t length) {
    size = input->size;
    require(offset <= input->size && length <= input->size - offset, "Read outside cached source");
    auto bytes = static_cast<uint8_t*>(output);
    while (length) {
        uint32_t take;
        if (length >= 512) {
            take = length / 512 * 512;
            input->read(offset, bytes, take);
        } else {
            uint32_t block = offset / 512, slot = block % blocks.size();
            if (blocks[slot] != block + 1 || offset % 512 >= lengths[slot]) {
                lengths[slot] = std::min<uint32_t>(512, input->size - block * 512);
                input->read(block * 512, buffer.data() + slot * 512, lengths[slot]);
                blocks[slot] = block + 1;
            }
            take = std::min<uint32_t>(length, lengths[slot] - offset % 512);
            std::memcpy(bytes, buffer.data() + slot * 512 + offset % 512, take);
        }
        offset += take;
        bytes += take;
        length -= take;
    }
}

Slice::Slice(SourcePtr source, uint32_t offset, uint32_t length) : input(source), base(offset) {
    require(offset <= input->size && length <= input->size - offset, "Slice outside source");
    size = length;
}

void Slice::read(uint32_t offset, void* output, uint32_t length) {
    require(offset <= size && length <= size - offset, "Read outside source slice");
    input->read(base + offset, output, length);
}

Map::Map(const std::string& path, std::shared_ptr<Disk> data, SourcePtr original, bool create)
    : table(std::make_shared<Disk>(path, create ? "w+b" : "rb", mapFileLimit)), payload(data), card(original) {
    uint32_t header[8] = {0x4433504e, 0x0050414d, FormatVersion, RulesVersion, 0, 0, 16, 0};
    if (create) {
        table->write(0, header, sizeof(header));
    } else {
        table->read(0, header, sizeof(header));
        require(header[0] == 0x4433504e && header[1] == 0x0050414d && header[2] == FormatVersion &&
                    header[3] == RulesVersion && header[6] == sizeof(Extent),
                "Unsupported cache mapping format");
        count = header[4];
        size = header[5];
        require(uint64_t(count) * sizeof(Extent) + 32 == table->size && count > 0, "Incomplete cache mapping");
        require(at(0).start == 0, "Incomplete virtual ROM");
        last = at(count - 1);
        require(last.start + last.length == size, "Incomplete virtual ROM");
    }
}

struct MappingPage {
    Map* owner = nullptr;
    uint32_t first = 0, age = 0;
    std::array<Extent, 256> entries;
};
static std::array<MappingPage, 16> cached;
static uint32_t cacheAge;
static MappingPage* recent;

Map::~Map() {
    for (auto& page : cached)
        if (page.owner == this)
            page.owner = nullptr;
    if (recent && recent->owner == nullptr)
        recent = nullptr;
}

Extent Map::at(uint32_t number) {
    require(number < count, "Mapping entry outside table");
    Extent item;
    if (pending && number >= pendingFirst)
        item = (*pending)[number - pendingFirst];
    else {
        uint32_t first = number & ~255u;
        if (!recent || recent->owner != this || recent->first != first) {
            MappingPage* oldest = &cached[0];
            recent = nullptr;
            for (auto& page : cached) {
                if (page.owner == this && page.first == first) {
                    recent = &page;
                    break;
                }
                if (!page.owner || page.age < oldest->age)
                    oldest = &page;
            }
            if (!recent) {
                recent = oldest;
                table->read(32 + first * sizeof(Extent), recent->entries.data(),
                            std::min<uint32_t>(256, count - first) * sizeof(Extent));
                recent->owner = this;
                recent->first = first;
            }
        }
        recent->age = ++cacheAge;
        item = recent->entries[number - first];
    }
    require(item.start < size && item.length && item.length <= size - item.start, "Invalid cache mapping range");
    require(uint32_t(item.kind) <= uint32_t(Kind::Fill), "Invalid cache data source");
    return item;
}

void Map::flush() {
    if (pending) {
        table->write(32 + pendingFirst * sizeof(Extent), pending->data(),
                     std::min<uint32_t>(256, count - pendingFirst) * sizeof(Extent));
        for (auto& page : cached)
            if (page.owner == this)
                page.owner = nullptr;
        if (recent && recent->owner == nullptr)
            recent = nullptr;
    }
}

Extent Map::find(uint32_t offset) {
    require(offset < size, "Address outside virtual ROM");
    if (offset >= last.start)
        return last;
    uint32_t first = 0, end = count;
    if (!index.empty()) {
        auto found = std::upper_bound(index.begin(), index.end(), offset,
                                      [](uint32_t address, const Extent& item) { return address < item.start; });
        if (found != index.begin())
            first = uint32_t(found - index.begin() - 1) * 256;
        end = std::min(count, first + 256);
    }
    while (first < end) {
        uint32_t middle = first + (end - first) / 2;
        Extent item = at(middle);
        if (offset < item.start)
            end = middle;
        else if (offset >= item.start + item.length)
            first = middle + 1;
        else
            return item;
    }
    fail("Virtual ROM mapping has a gap");
}

void Map::append(Kind kind, uint32_t offset, uint32_t length) {
    if (!length)
        return;
    require(size <= 0x40000000 && length <= 0x40000000 - size, "Virtual ROM exceeds size limit");
    bool merge = count && last.kind == kind &&
                 (kind == Kind::Fill ? last.source == offset : last.source + last.length == offset);
    uint32_t slot = merge ? count - 1 : count;
    uint32_t first = slot & ~255u;
    if (!pending || pendingFirst != first) {
        flush();
        pending = std::make_unique<std::array<Extent, 256>>();
        pendingFirst = first;
        if (first < count)
            table->read(32 + first * sizeof(Extent), pending->data(), (count - first) * sizeof(Extent));
    }
    if (merge)
        last.length += length;
    else {
        last = {size, length, kind, offset};
        if (count % 256 == 0) {
            index.push_back(last);
        }
        count++;
    }
    (*pending)[slot - pendingFirst] = last;
    size += length;
}

void Map::literal(const void* input, uint32_t length) {
    if (!length)
        return;
    uint32_t offset = payload->size;
    payload->write(offset, input, length);
    append(Kind::Patch, offset, length);
}

void Map::copy(SourcePtr input, uint32_t offset, uint32_t length) {
    require(offset <= input->size && length <= input->size - offset, "Copy outside source");
    if (auto slice = std::dynamic_pointer_cast<Slice>(input)) {
        copy(slice->input, slice->base + offset, length);
    } else if (auto map = std::dynamic_pointer_cast<Map>(input)) {
        while (length) {
            Extent item = map->find(offset);
            uint32_t take = std::min(length, item.length - (offset - item.start));
            append(item.kind, item.source + (item.kind == Kind::Fill ? 0 : offset - item.start), take);
            offset += take;
            length -= take;
        }
    } else {
        require(input == card, "Unregistered mapping source");
        append(Kind::Card, offset, length);
    }
}

void Map::read(uint32_t offset, void* output, uint32_t length) {
    require(offset <= size && length <= size - offset, "Read outside virtual ROM");
    auto bytes = static_cast<uint8_t*>(output);
    while (length) {
        Extent item = find(offset);
        uint32_t take = std::min(length, item.length - (offset - item.start));
        if (item.kind == Kind::Fill) {
            require(item.source <= 255, "Invalid cache fill byte");
            std::memset(bytes, item.source, take);
        } else {
            SourcePtr input = item.kind == Kind::Card ? card : payload;
            require(item.source <= input->size && item.length <= input->size - item.source,
                    "Cache source outside file");
            input->read(item.source + offset - item.start, bytes, take);
        }
        bytes += take;
        offset += take;
        length -= take;
    }
}

void Map::finish() {
    flush();
    uint32_t values[] = {count, size};
    table->write(16, values, sizeof(values));
    table->flush();
    payload->flush();
    pending.reset();
    pendingFirst = UINT32_MAX;
}

bool exists(const std::string& path) {
    struct stat info;
    if (stat(path.c_str(), &info) == 0)
        return true;
    require(errno == ENOENT, "Cannot inspect " + path);
    return false;
}

void makeDirectory(const std::string& path) {
    if (mkdir(path.c_str(), 0777) == 0)
        return;
    require(errno == EEXIST, "Cannot create directory " + path);
    struct stat info;
    require(stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode), "Expected directory " + path);
}

void removeTree(const std::string& path) {
    if (!exists(path))
        return;
    logPreparation("DIRECTORY open " + path);
    DIR* directory = opendir(path.c_str());
    require(directory, "Cannot open directory " + path);
    while (dirent* entry = readdir(directory)) {
        std::string name = entry->d_name;
        if (name == "." || name == "..")
            continue;
        std::string child = path + "/" + name;
        struct stat info;
        require(stat(child.c_str(), &info) == 0, "Cannot inspect " + child);
        if (S_ISDIR(info.st_mode))
            removeTree(child);
        else {
            logPreparation("FILE remove begin " + child);
            require(unlink(child.c_str()) == 0, "Cannot remove " + child);
            logPreparation("FILE remove done " + child);
        }
    }
    require(closedir(directory) == 0, "Cannot close directory " + path);
    logPreparation("DIRECTORY remove begin " + path);
    require(rmdir(path.c_str()) == 0, "Cannot remove directory " + path);
    logPreparation("DIRECTORY remove done " + path);
}

std::vector<uint8_t> readBytes(SourcePtr source, uint32_t offset, uint32_t length) {
    std::vector<uint8_t> data(length);
    source->read(offset, data.data(), length);
    return data;
}

void copyFile(const std::string& source, const std::string& destination) {
    auto input = std::make_shared<Disk>(source);
    Disk output(destination, "wb");
    std::vector<uint8_t> buffer(BufferSize);
    for (uint32_t offset = 0; offset < input->size;) {
        uint32_t length = std::min(BufferSize, input->size - offset);
        input->read(offset, buffer.data(), length);
        output.write(offset, buffer.data(), length);
        offset += length;
    }
    output.sync();
}

static std::string hex(const uint8_t* data, unsigned int length) {
    static const char digits[] = "0123456789abcdef";
    std::string result;
    for (unsigned int index = 0; index < length; index++) {
        result += digits[data[index] >> 4];
        result += digits[data[index] & 15];
    }
    return result;
}

static std::string crcHex(mz_ulong crc) {
    char value[9];
    std::snprintf(value, sizeof(value), "%08lx", static_cast<unsigned long>(crc));
    return value;
}

std::string digest(SourcePtr input, bool crc32, const char* stage) {
    mbedtls_md5_context md5;
    if (!crc32) {
        mbedtls_md5_init(&md5);
        require(mbedtls_md5_starts_ret(&md5) == 0, "Cannot initialize digest");
    }
    mz_ulong crc = MZ_CRC32_INIT;
    std::vector<uint8_t> storage(BufferSize + 31);
    auto buffer = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(storage.data()) + 31) & ~uintptr_t(31));
    if (stage)
        progress(stage, 0, input->size, true);
    for (uint32_t offset = 0; offset < input->size;) {
        uint32_t length = std::min(BufferSize, input->size - offset);
        input->read(offset, buffer, length);
        if (crc32)
            crc = mz_crc32(crc, buffer, length);
        else
            require(mbedtls_md5_update_ret(&md5, buffer, length) == 0, "Cannot update digest");
        offset += length;
        if (stage)
            progress(stage, offset, input->size, true);
    }
    if (crc32)
        return crcHex(crc);
    uint8_t bytes[16];
    require(mbedtls_md5_finish_ret(&md5, bytes) == 0, "Cannot finish digest");
    mbedtls_md5_free(&md5);
    return hex(bytes, 16);
}

std::string digest(const void* input, uint32_t length, bool crc32) {
    if (crc32)
        return crcHex(mz_crc32(MZ_CRC32_INIT, static_cast<const uint8_t*>(input), length));
    uint8_t bytes[16];
    require(mbedtls_md5_ret(static_cast<const uint8_t*>(input), length, bytes) == 0, "Cannot calculate digest");
    return hex(bytes, 16);
}

std::string normalize(std::string value) {
    for (char& byte : value) {
        if (byte == '\\')
            byte = '/';
        else if (byte >= 'A' && byte <= 'Z')
            byte += 'a' - 'A';
    }
    if (std::any_of(value.begin(), value.end(), [](unsigned char byte) { return byte >= 128; })) {
        value = una::cases::to_lowercase_utf8(value);
    }
    return value;
}

bool isDeltaPatch(const std::string& path) {
    return path.size() >= 7 && normalize(path.substr(path.size() - 7)) == ".xdelta";
}

std::string convert(const std::string& input, const char* encoding) {
    iconv_t converter = iconv_open("UTF-8", encoding);
    require(converter != (iconv_t)-1, "Cannot initialize filename conversion");
    std::string output(input.size() * 4 + 4, '\0');
    char* source = const_cast<char*>(input.data());
    char* target = output.data();
    size_t sourceLength = input.size(), targetLength = output.size();
    require(iconv(converter, &source, &sourceLength, &target, &targetLength) != size_t(-1) && sourceLength == 0,
            "Invalid filename encoding");
    require(iconv_close(converter) == 0, "Cannot close filename converter");
    output.resize(output.size() - targetLength);
    return output;
}

Workspace::Workspace(const std::string& path, SourcePtr input, bool ds) : directory(path), card(input), dsOnly(ds) {
    makeDirectory(directory);
    payload = std::make_shared<Disk>(directory + "/data.bin", "w+b");
}

std::shared_ptr<Map> Workspace::map() {
    return std::make_shared<Map>(directory + "/" + std::to_string(next++) + ".map", payload, card, true);
}

}
