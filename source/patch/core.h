#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace np3d {

constexpr uint32_t FormatVersion = 1;
constexpr uint32_t RulesVersion = 3;
constexpr uint32_t BufferSize = 65536;
constexpr const char* DsModeDeltaError = "Prepare whole-ROM xdelta patches for DSi-enhanced games on a 3DS or DSi, then copy the prepared cache to the NDS. Use the same Game Card, patches and patch order.";

using FatalHandler = void (*)(const char*);
void setFatalHandler(FatalHandler handler);
[[noreturn]] void fail(const std::string& message);
void require(bool condition, const std::string& message);
inline void require(bool condition, const char* message) { if (!condition) fail(message); }

struct PatchError { std::string message; };
void patchRequire(bool condition, const std::string& message);
inline void patchRequire(bool condition, const char* message) { if (!condition) patchRequire(false, std::string(message)); }

#if NP3D_PREPARE_TRACE
#define NP3D_PREPARE_LOG(message) ::np3d::logPreparation(message)
#else
#define NP3D_PREPARE_LOG(message) ((void)0)
#endif

using ProgressHandler = void (*)(const char*, uint32_t, uint32_t, bool, const char*);
void setProgressHandler(ProgressHandler handler);
void progress(const char* stage, uint32_t completed, uint32_t total, bool bytes = false, const char* detail = "");
void beginPreparationLog(const std::string& root, const std::string& patch);
void finishPreparationLog(bool reused, const std::string& error = "");
void logPreparation(const std::string& message);
uint64_t preparationMilliseconds();

struct Source {
    uint32_t size = 0;
    virtual ~Source() = default;
    virtual void read(uint32_t offset, void* output, uint32_t length) = 0;
};
using SourcePtr = std::shared_ptr<Source>;

struct Disk final : Source {
    FILE* handle;
    std::string path;
    uint32_t position = 0;
    bool reading = false, writing = false;
    Disk(const std::string& path, const char* mode = "rb");
    ~Disk();
    void read(uint32_t offset, void* output, uint32_t length) override;
    void write(uint32_t offset, const void* input, uint32_t length);
    void sync();
    void flush();
};

struct Cached final : Source {
    SourcePtr input;
    std::array<uint32_t, BufferSize / 512> blocks = {};
    std::array<uint16_t, BufferSize / 512> lengths = {};
    std::vector<uint8_t> buffer = std::vector<uint8_t>(BufferSize);
    explicit Cached(SourcePtr input);
    void read(uint32_t offset, void* output, uint32_t length) override;
};

struct Slice final : Source {
    SourcePtr input;
    uint32_t base;
    Slice(SourcePtr input, uint32_t offset, uint32_t length);
    void read(uint32_t offset, void* output, uint32_t length) override;
};

enum class Kind : uint32_t { Card = 0, Patch = 1, Fill = 2 };
struct Extent {
    uint32_t start;
    uint32_t length;
    Kind kind;
    uint32_t source;
};
static_assert(sizeof(Extent) == 16);

struct Map final : Source {
    std::shared_ptr<Disk> table;
    std::shared_ptr<Disk> payload;
    SourcePtr card;
    uint32_t count = 0;
    Extent last = {};
    std::vector<Extent> index;
    std::unique_ptr<std::array<Extent, 256>> pending;
    uint32_t pendingFirst = UINT32_MAX;
    void flush();
    Map(const std::string& path, std::shared_ptr<Disk> payload, SourcePtr card, bool create);
    ~Map();
    Extent at(uint32_t number);
    Extent find(uint32_t offset);
    void append(Kind kind, uint32_t source, uint32_t length);
    void literal(const void* data, uint32_t length);
    void copy(SourcePtr source, uint32_t offset, uint32_t length);
    void read(uint32_t offset, void* output, uint32_t length) override;
    void finish();
};

void makeDirectory(const std::string& path);
bool exists(const std::string& path);
void removeTree(const std::string& path);
std::vector<uint8_t> readBytes(SourcePtr source, uint32_t offset, uint32_t length);
void copyFile(const std::string& input, const std::string& output);
std::string digest(SourcePtr source, bool crc32, const char* stage = nullptr);
std::string digest(const void* data, uint32_t length, bool crc32);
std::string normalize(std::string value);
bool isDeltaPatch(const std::string& path);
std::string convert(const std::string& value, const char* encoding);

struct Package;
struct Workspace {
    std::string directory;
    SourcePtr card;
    std::shared_ptr<Disk> payload;
    unsigned int next = 0;
    std::string error;
    bool dsOnly = false;
    Workspace(const std::string& directory, SourcePtr card, bool dsOnly = false);
    std::shared_ptr<Map> map();
};
std::shared_ptr<Map> applyDelta(Workspace& work, SourcePtr source, const std::string& delta, const std::string& label = "");
std::shared_ptr<Map> applyDelta(Workspace& work, SourcePtr source, SourcePtr delta, const std::string& label);
std::shared_ptr<Map> buildRom(Workspace& work, SourcePtr original, Package& package);

struct Prepared {
    std::string directory;
    std::shared_ptr<Map> rom;
    bool reused = false;
    std::string error;
    bool mismatch = false;
};
Prepared prepare(SourcePtr card, const std::string& patch, const std::string& root, bool dsOnly = false);
Prepared prepare(SourcePtr card, const std::vector<std::string>& patches, const std::string& root, bool dsOnly = false);
std::vector<std::string> listPatches(const std::string& root);

}
