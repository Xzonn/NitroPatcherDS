#include "arm9_blz.h"
#include "blz.h"
#include <cstring>

namespace np3d {
namespace {

bool readSource(void* context, uint32_t offset, void* output, uint32_t length) {
    static_cast<Source*>(context)->read(offset, output, length);
    return true;
}
bool readTemporary(void* context, uint32_t offset, void* output, uint32_t length) {
    auto& disk = *static_cast<Disk*>(context);
    uint32_t take = offset < disk.size ? std::min(length, disk.size - offset) : 0;
    if (take)
        disk.read(offset, output, take);
    if (take < length)
        std::memset(static_cast<uint8_t*>(output) + take, 0, length - take);
    return true;
}
bool writeTemporary(void* context, uint32_t offset, const void* input, uint32_t length) {
    static_cast<Disk*>(context)->write(offset, input, length);
    return true;
}
bool writeMap(void* context, uint32_t offset, const void* input, uint32_t length) {
    auto& map = *static_cast<Map*>(context);
    require(offset == map.size, "Nonsequential BLZ output");
    map.literal(input, length);
    return true;
}
void append(Map& output, SourcePtr input, uint32_t offset, uint32_t length) {
    if (std::dynamic_pointer_cast<Disk>(input)) {
        auto buffer = readBytes(input, offset, length);
        output.literal(buffer.data(), buffer.size());
    } else
        output.copy(input, offset, length);
}
uint32_t word(SourcePtr input, uint32_t offset) {
    unsigned_int value;
    input->read(offset, &value, sizeof(value));
    return value;
}

}

Arm9Image unpackArm9(Workspace& work, SourcePtr input, Header& header, bool nitrocode) {
    patchRequire(input->size >= 24, "ARM9 data is truncated");
    uint32_t tail = nitrocode ? 12 : 0, init = uint32_t(header.offset_0x88) & 0x3fff;
    uint32_t end = init ? word(input, init + 0x14) : uint32_t(header.arm9_ram_address) + uint32_t(header.arm9_size);
    bool compressed =
        end > header.arm9_ram_address && uint64_t(end) + tail >= uint64_t(header.arm9_ram_address) + input->size;
    if (!compressed)
        return {input, false, 0};
    patchRequire(input->size >= tail + 8, "BLZ footer is truncated");
    auto packed = std::make_shared<Slice>(input, 0, input->size - tail);
    uint64_t size = uint64_t(packed->size) + word(packed, packed->size - 4);
    patchRequire(size <= 0x10000000 - tail, "BLZ output exceeds size limit");
    auto output = std::make_shared<Disk>(work.directory + "/arm9-" + std::to_string(work.next++) + ".bin", "w+b");
    auto result = blz::decompress({packed.get(), packed->size, readSource, nullptr},
                                  {output.get(), uint32_t(size), readTemporary, writeTemporary});
    patchRequire(result.error == blz::Error::None && result.length == size, "Cannot decompress ARM9 BLZ data");
    if (tail) {
        auto suffix = readBytes(input, input->size - tail, tail);
        output->write(size, suffix.data(), suffix.size());
    }
    output->flush();
    patchRequire(end >= header.arm9_ram_address && end - uint32_t(header.arm9_ram_address) <= input->size,
                 "Invalid ARM9 end pointer");
    return {output, true, init ? input->size - (end - uint32_t(header.arm9_ram_address)) : 0};
}

SourcePtr packArm9(Workspace& work, SourcePtr input, Header& header, bool nitrocode, uint32_t postSize) {
    uint32_t tail = nitrocode ? 12 : 0;
    patchRequire(input->size >= 0x4000 + tail, "ARM9 secure area is truncated");
    auto body = std::make_shared<Slice>(input, 0x4000, input->size - 0x4000 - tail);
    auto temporary = std::make_shared<Disk>(work.directory + "/blz-" + std::to_string(work.next++) + ".bin", "w+b");
    auto packed = work.map();
    auto result = blz::compress({body.get(), body->size, readSource, nullptr},
                                {temporary.get(), body->size, readTemporary, writeTemporary},
                                {packed.get(), body->size, nullptr, writeMap});
    patchRequire(result.error == blz::Error::None && result.length, "Cannot compress ARM9 BLZ data");
    packed->finish();
    auto output = work.map();
    append(*output, input, 0, 0x4000);
    output->copy(packed, 0, packed->size);
    if (tail)
        append(*output, input, input->size - tail, tail);
    output->finish();
    uint32_t init = uint32_t(header.offset_0x88) & 0x3fff;
    if (init) {
        patchRequire(postSize <= output->size, "Invalid ARM9 trailing size");
        unsigned_int end = output->size - postSize + uint32_t(header.arm9_ram_address);
        output = overwrite(work, output, init + 0x14, &end, sizeof(end));
    }
    return output;
}

}
