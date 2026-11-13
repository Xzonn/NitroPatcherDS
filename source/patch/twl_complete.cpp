#include "twl.h"
#include "arm9_blz.h"
#include <cstring>

namespace np3d {
namespace {

struct Bytes final : Source {
    std::vector<uint8_t> bytes;
    explicit Bytes(std::vector<uint8_t> data) : bytes(std::move(data)) { size = bytes.size(); }
    void read(uint32_t offset, void* output, uint32_t length) override {
        require(offset <= size && length <= size - offset, "Read outside DSi digest input");
        std::memcpy(output, bytes.data() + offset, length);
    }
};

uint32_t align(uint64_t value, uint32_t alignment) {
    patchRequire(alignment && value <= 0x40000000, "DSi output exceeds size limit");
    uint64_t result = (value + alignment - 1) / alignment * alignment;
    patchRequire(result <= 0x40000000, "DSi output exceeds size limit");
    return result;
}
unsigned_int& offset(Header& h, unsigned int index) {
    return index == 0   ? h.arm9_rom_offset
           : index == 1 ? h.arm7_rom_offset
           : index == 2 ? h.dsi9_rom_offset
                        : h.dsi7_rom_offset;
}
unsigned_int& length(Header& h, unsigned int index) {
    return index == 0 ? h.arm9_size : index == 1 ? h.arm7_size : index == 2 ? h.dsi9_size : h.dsi7_size;
}
uint8_t* mac(Header& h, unsigned int index) {
    return index == 0 ? h.hmac_arm9 : index == 1 ? h.hmac_arm7 : index == 2 ? h.hmac_arm9i : h.hmac_arm7i;
}
bool padding(SourcePtr input, uint32_t start, uint32_t end, int value = -1) {
    require(start <= input->size && end <= input->size, "DSi padding outside ROM");
    if (end <= start)
        return true;
    std::vector<uint8_t> bytes(std::min(BufferSize, end - start));
    while (start < end) {
        uint32_t take = std::min<uint32_t>(bytes.size(), end - start);
        input->read(start, bytes.data(), take);
        if (value < 0) {
            value = bytes[0];
            if (value != 0 && value != 255)
                return false;
        }
        if (!std::all_of(bytes.begin(), bytes.begin() + take, [&](uint8_t byte) { return byte == value; }))
            return false;
        start += take;
    }
    return true;
}
std::shared_ptr<Map> replace(Workspace& work, SourcePtr input, uint32_t start, SourcePtr data) {
    require(start <= input->size && data->size <= input->size - start, "DSi replacement outside ROM");
    auto output = work.map();
    output->copy(input, 0, start);
    output->copy(data, 0, data->size);
    output->copy(input, start + data->size, input->size - start - data->size);
    output->finish();
    return output;
}
SourcePtr encryptedSecure(Workspace& work, SourcePtr arm9, Header& header, bool decrypted) {
    if (!decrypted)
        return arm9;
    auto secure = readBytes(arm9, 0, 0x4000);
    unsigned_short crc = CalcCrc16(secure.data() + 0x10, 0x7f0);
    std::memcpy(secure.data() + 0xe, &crc, sizeof(crc));
    secureArea(secure, header, true);
    return overwrite(work, arm9, 0, secure.data(), secure.size());
}

}

bool equalSources(SourcePtr first, SourcePtr second) {
    if (first->size != second->size)
        return false;
    if (first == second)
        return true;
    std::vector<uint8_t> a(BufferSize), b(BufferSize);
    for (uint32_t position = 0; position < first->size;) {
        uint32_t take = std::min(BufferSize, first->size - position);
        first->read(position, a.data(), take);
        second->read(position, b.data(), take);
        if (std::memcmp(a.data(), b.data(), take))
            return false;
        position += take;
    }
    return true;
}

CompleteTwl::CompleteTwl(Workspace& workspace, SourcePtr source, Header& header, const std::vector<SourcePtr>& overlays)
    : work(workspace), input(source), originalHeader(std::make_unique<Header>(header)), overlaySources(overlays) {
    Header& h = *originalHeader;
    patchRequire(h.unitcode == 2 && (!h.tid_high || h.tid_high == 0x30000),
                 "Complete DSi processing requires a DSi-enhanced cartridge");
    patchRequire(h.dsi9_ram_address && h.dsi9_ram_address != 0xffffffff && h.dsi7_ram_address &&
                     h.dsi7_ram_address != 0xffffffff,
                 "Missing DSi program load address");
    digests = h.digest_ntr_start || h.digest_ntr_size || h.digest_twl_start || h.digest_twl_size ||
              h.sector_hashtable_start || h.sector_hashtable_size || h.block_hashtable_start ||
              h.block_hashtable_size || h.digest_sector_size || h.digest_block_sectorcount;
    if (digests) {
        uint32_t sector = h.digest_sector_size, block = h.digest_block_sectorcount;
        patchRequire(sector && sector <= 0x4000 && 0x4000 % sector == 0 && block && block <= 0x100000,
                     "Invalid DSi digest geometry");
        patchRequire(h.digest_ntr_start && h.digest_ntr_size >= 0x4000 && uint32_t(h.digest_ntr_size) % sector == 0 &&
                         uint32_t(h.digest_twl_size) % sector == 0 && (!h.digest_twl_size || h.digest_twl_start),
                     "Incomplete DSi digest regions");
        patchRequire(h.sector_hashtable_start && h.block_hashtable_start && h.sector_hashtable_size &&
                         uint32_t(h.sector_hashtable_size) % (block * 20) == 0 &&
                         uint64_t(h.sector_hashtable_size) >=
                             (uint64_t(h.digest_ntr_size) + uint32_t(h.digest_twl_size)) / sector * 20 &&
                         h.block_hashtable_size == uint32_t(h.sector_hashtable_size) / block,
                     "Incomplete or invalid DSi digest tables");
        for (auto range : {std::pair<uint32_t, uint32_t>{h.digest_ntr_start, h.digest_ntr_size},
                           {h.digest_twl_start, h.digest_twl_size},
                           {h.sector_hashtable_start, h.sector_hashtable_size},
                           {h.block_hashtable_start, h.block_hashtable_size}})
            patchRequire(range.first <= input->size && range.second <= input->size - range.first,
                         "DSi digest region outside ROM");
        patchRequire(uint64_t(h.digest_ntr_start) + uint32_t(h.digest_ntr_size) <= h.sector_hashtable_start &&
                         uint64_t(h.sector_hashtable_start) + uint32_t(h.sector_hashtable_size) <=
                             h.block_hashtable_start &&
                         (!h.digest_twl_size ||
                          uint64_t(h.block_hashtable_start) + uint32_t(h.block_hashtable_size) <= h.digest_twl_start),
                     "DSi digest regions overlap");
    }
    std::vector<unsigned int> ordered;
    for (unsigned int index = 0; index < programs.size(); index++) {
        uint32_t start = offset(h, index), size = length(h, index);
        patchRequire(!size || start, "Missing DSi program ROM offset");
        patchRequire(start <= input->size && (size ? align(size, 16) : 0) <= input->size - start,
                     "DSi program outside ROM");
        auto data = std::make_shared<Slice>(input, start, size ? align(size, 16) : 0);
        programs[index] = {start, size, size, data, false};
        if (size)
            ordered.push_back(index);
    }
    std::sort(ordered.begin(), ordered.end(),
              [&](unsigned int a, unsigned int b) { return programs[a].start < programs[b].start; });
    for (size_t index = 1; index < ordered.size(); index++)
        patchRequire(uint64_t(programs[ordered[index - 1]].start) + programs[ordered[index - 1]].data->size <=
                         programs[ordered[index]].start,
                     "DSi program regions overlap");
    for (unsigned int id : {0u, 1u}) {
        uint32_t start = id == 0 ? uint32_t(h.modcrypt1_start) : uint32_t(h.modcrypt2_start);
        uint32_t size = id == 0 ? uint32_t(h.modcrypt1_size) : uint32_t(h.modcrypt2_size);
        if (!start && !size)
            continue;
        patchRequire(start && size && size % 16 == 0, "Invalid Modcrypt region");
        unsigned int found = UINT32_MAX;
        for (auto index : ordered)
            if (start >= programs[index].start &&
                uint64_t(start) + size <= uint64_t(programs[index].start) + programs[index].data->size) {
                patchRequire(found == UINT32_MAX, "Modcrypt region conflicts with program ranges");
                found = index;
            }
        patchRequire(found >= 2 && found < programs.size(), "Modcrypt region targets an unsupported program range");
        uint32_t first = start - programs[found].start;
        unsigned int kind = first == 0 && size == programs[found].data->size ? 0
                            : first == 0 && found == 2 && size == std::min<uint32_t>(0x4000, programs[found].data->size)
                                ? 1
                                : 2;
        regions.push_back({id, found, kind, first, size});
    }
    decryptPrograms();
    uint32_t boundary = (uint32_t(h.offset_0x90) >> 16) * 0x80000;
    uint32_t first = UINT32_MAX;
    for (unsigned int index : {2u, 3u})
        if (programs[index].length)
            first = std::min(first, programs[index].start);
    prefix = boundary && uint64_t(boundary) + 0x3000 <= input->size &&
             ((first != UINT32_MAX && first >= boundary + 0x3000) ||
              (h.digest_twl_size && h.digest_twl_start >= boundary + 0x3000) || h.total_rom_size >= boundary + 0x3000);
}

void CompleteTwl::decryptPrograms() {
    if (regions.empty())
        return;
    Header& h = *originalHeader;
    TwlHasher hash;
    std::array<SourcePtr, 4> solution;
    bool found = false;
    for (unsigned int mask = 0; mask < (1u << regions.size()); mask++) {
        std::array<SourcePtr, 4> views;
        for (unsigned int index = 0; index < views.size(); index++)
            views[index] = programs[index].data;
        for (size_t index = regions.size(); index-- > 0;)
            if (mask & (1u << index)) {
                auto& region = regions[index];
                views[region.program] = twlCrypt(work, views[region.program], region.offset, region.length, h,
                                                 region.id == 0 ? h.hmac_arm9 : h.hmac_arm7);
            }
        bool accepted = true;
        for (unsigned int index : {2u, 3u}) {
            bool relevant = std::any_of(regions.begin(), regions.end(),
                                        [&](const Region& region) { return region.program == index; });
            if (!relevant)
                continue;
            auto actual = hash(views[index], 0, programs[index].length, index == 2 ? "arm9i.bin" : "arm7i.bin");
            if (std::memcmp(actual.data(), mac(h, index), actual.size()) == 0)
                continue;
            if (!digests) {
                accepted = false;
                break;
            }
            for (auto& region : regions)
                if (region.program == index) {
                    uint32_t address = programs[index].start + region.offset;
                    bool twl = address >= h.digest_twl_start &&
                               uint64_t(address) < uint64_t(h.digest_twl_start) + uint32_t(h.digest_twl_size);
                    uint32_t base = twl ? uint32_t(h.digest_twl_start) : uint32_t(h.digest_ntr_start);
                    uint32_t size = twl ? uint32_t(h.digest_twl_size) : uint32_t(h.digest_ntr_size),
                             sector = h.digest_sector_size;
                    if (address < base || uint64_t(address) >= uint64_t(base) + size) {
                        accepted = false;
                        break;
                    }
                    uint32_t first = base + (address - base) / sector * sector;
                    uint32_t last = std::min<uint32_t>(base + size, align(uint64_t(address) + region.length, sector));
                    bool evidence = false;
                    for (uint32_t position = first; position < last; position += sector) {
                        auto bytes = readBytes(input, position, sector);
                        for (unsigned int p = 0; p < views.size(); p++) {
                            uint32_t start = std::max(position, programs[p].start),
                                     end = std::min(position + sector, programs[p].start + views[p]->size);
                            if (end > start)
                                views[p]->read(start - programs[p].start, bytes.data() + start - position, end - start);
                        }
                        auto data = std::make_shared<Bytes>(std::move(bytes));
                        auto value = hash(data, 0, data->size);
                        uint32_t digest = ((twl ? uint32_t(h.digest_ntr_size) : 0) + position - base) / sector * 20;
                        auto expected = readBytes(input, uint32_t(h.sector_hashtable_start) + digest, 20);
                        if (std::memcmp(value.data(), expected.data(), 20) == 0) {
                            evidence = true;
                            break;
                        }
                    }
                    if (!evidence) {
                        accepted = false;
                        break;
                    }
                }
            if (!accepted)
                break;
        }
        if (!accepted)
            continue;
        bool same = found;
        if (same)
            for (unsigned int index = 0; index < views.size(); index++)
                if (!equalSources(views[index], solution[index])) {
                    same = false;
                    break;
                }
        patchRequire(!found || same, "Cannot determine Modcrypt state uniquely");
        solution = views;
        found = true;
    }
    patchRequire(found, "Cannot determine Modcrypt state");
    for (unsigned int index = 0; index < programs.size(); index++)
        programs[index].data = solution[index];
}

SourcePtr CompleteTwl::program(unsigned int index) {
    return std::make_shared<Slice>(programs.at(index).data, 0, programs.at(index).length);
}

void CompleteTwl::setProgram(unsigned int index, SourcePtr data) {
    auto& item = programs.at(index);
    auto output = work.map();
    output->copy(data, 0, data->size);
    uint32_t size = align(data->size, 16);
    if (item.length == data->size)
        output->copy(item.data, data->size, size - data->size);
    else
        output->append(Kind::Fill, 255, size - data->size);
    output->finish();
    item.length = data->size;
    item.data = output;
    item.replaced = true;
}

SourcePtr CompleteTwl::updateOverlays(SourcePtr arm9, Header& header, bool nitrocode,
                                      const std::vector<SourcePtr>& overlays, bool replaced) {
    return updateOverlayHashes(work, arm9, header, nitrocode, overlaySources, overlays, replaced);
}

SourcePtr updateOverlayHashes(Workspace& work, SourcePtr arm9, Header& header, bool nitrocode,
                              const std::vector<SourcePtr>& overlaySources, const std::vector<SourcePtr>& overlays,
                              bool replaced) {
    if (overlays.size() != overlaySources.size()) {
        patchRequire(replaced, "Changing signed overlay count requires an ARM9 replacement");
        return arm9;
    }
    if (overlays == overlaySources)
        return arm9;
    std::vector<uint8_t> previous, current;
    TwlHasher hash(work.dsOnly ? "Checking game data" : "Checking DSi data");
    for (size_t index = 0; index < overlays.size(); index++) {
        auto original = hash(overlaySources[index], 0, overlaySources[index]->size, "overarm9.bin");
        previous.insert(previous.end(), original.begin(), original.end());
        auto value = overlays[index] == overlaySources[index]
                         ? original
                         : hash(overlays[index], 0, overlays[index]->size, "overarm9.bin");
        current.insert(current.end(), value.begin(), value.end());
    }
    if (previous == current)
        return arm9;
    auto plain = unpackArm9(work, arm9, header, nitrocode);
    uint32_t match = 0, matches = 0;
    bool updated = false;
    std::vector<uint8_t> buffer(BufferSize + previous.size());
    for (unsigned int sequence = 0; sequence < 2; sequence++) {
        auto& bytes = sequence == 0 ? previous : current;
        for (uint32_t position = 0x4000; position < plain.data->size;) {
            uint32_t take = std::min<uint32_t>(buffer.size(), plain.data->size - position);
            plain.data->read(position, buffer.data(), take);
            uint32_t step = std::min(BufferSize, plain.data->size - position);
            auto first = buffer.begin(), end = first + take;
            while ((first = std::search(first, end, bytes.begin(), bytes.end())) != end) {
                uint32_t local = first - buffer.begin();
                if (local >= step)
                    break;
                match = position + local;
                updated = sequence == 1;
                matches++;
                first++;
            }
            position += step;
        }
    }
    patchRequire(matches == 1, "ARM9 overlay HMAC table cannot be uniquely located");
    if (updated)
        return arm9;
    if (plain.compressed) {
        auto disk = std::dynamic_pointer_cast<Disk>(plain.data);
        require(bool(disk), "Missing decompressed ARM9 data");
        disk->write(match, current.data(), current.size());
        disk->flush();
        return packArm9(work, disk, header, nitrocode, plain.postSize);
    }
    return overwrite(work, plain.data, match, current.data(), current.size());
}

std::shared_ptr<Map> CompleteTwl::serialize(Header& h, std::vector<uint8_t> headerData,
                                            std::vector<TwlSection>& sections, bool forceLayout, bool decrypted) {
    Header& old = *originalHeader;
    TwlHasher hash;
    uint32_t originalEnd = std::max({uint32_t(old.application_end_offset), uint32_t(old.total_rom_size),
                                     uint32_t(old.dsi9_rom_offset) + align(old.dsi9_size, 16),
                                     uint32_t(old.dsi7_rom_offset) + align(old.dsi7_size, 16),
                                     uint32_t(old.digest_twl_start) + uint32_t(old.digest_twl_size)});
    patchRequire(originalEnd <= input->size, "DSi declared size exceeds ROM");
    uint32_t boundary = (uint32_t(old.offset_0x90) >> 16) * 0x80000;
    uint32_t ntrLimit = digests ? uint32_t(old.digest_ntr_start) + uint32_t(old.digest_ntr_size) : boundary;
    if (!ntrLimit) {
        ntrLimit = UINT32_MAX;
        for (unsigned int index : {2u, 3u})
            if (programs[index].length)
                ntrLimit = std::min(ntrLimit, programs[index].start);
    }
    std::vector<size_t> ordered;
    for (size_t index = 0; index < sections.size(); index++)
        if (sections[index].originalSize || sections[index].data->size)
            ordered.push_back(index);
    std::stable_sort(ordered.begin(), ordered.end(),
                     [&](size_t a, size_t b) { return sections[a].start < sections[b].start; });
    bool preserve = !forceLayout;
    for (size_t index = 0; preserve && index < ordered.size(); index++) {
        auto& item = sections[ordered[index]];
        uint32_t next = index + 1 < ordered.size() ? sections[ordered[index + 1]].start : ntrLimit;
        if (item.start < h.rom_header_size || uint64_t(item.start) + item.originalSize > next ||
            uint64_t(item.start) + item.data->size > next)
            preserve = false;
        else if (item.data->size > item.originalSize &&
                 !padding(input, item.start + item.originalSize, item.start + item.data->size))
            preserve = false;
    }
    std::vector<unsigned int> dsi;
    for (unsigned int index : {2u, 3u})
        if (programs[index].originalLength)
            dsi.push_back(index);
    std::sort(dsi.begin(), dsi.end(),
              [&](unsigned int a, unsigned int b) { return programs[a].start < programs[b].start; });
    for (size_t index = 0; index < dsi.size(); index++) {
        auto& item = programs[dsi[index]];
        uint32_t next = index + 1 < dsi.size() ? programs[dsi[index + 1]].start : originalEnd;
        if (uint64_t(item.start) + item.data->size > next)
            preserve = false;
        else if (item.data->size > align(item.originalLength, 16) &&
                 !padding(input, item.start + align(item.originalLength, 16), item.start + item.data->size))
            preserve = false;
    }
    for (unsigned int index : {2u, 3u})
        if (programs[index].length && !programs[index].originalLength)
            preserve = false;
    uint32_t ntrEnd, end;
    if (preserve) {
        for (auto& item : sections)
            item.place(item.start);
        ntrEnd = old.application_end_offset;
        end = originalEnd;
    } else {
        uint32_t position = boundary + (prefix ? 0x3000 : 0);
        for (auto index : dsi) {
            auto& item = programs[index];
            patchRequire(item.start >= position, "TWL input regions overlap");
            patchRequire(padding(input, position, item.start),
                         "TWL contains unrecognized bytes whose relocation is unsupported");
            position = item.start + align(item.originalLength, 16);
        }
        patchRequire(position <= originalEnd && padding(input, position, originalEnd),
                     "TWL contains unrecognized trailing data whose relocation is unsupported");
        position = std::max<uint32_t>(h.rom_header_size, 0x4000);
        for (auto& item : sections) {
            item.start = position;
            item.place(position);
            position = align(uint64_t(position) + item.data->size, 0x200);
        }
        uint32_t ntrDataEnd = position;
        if (digests) {
            h.digest_ntr_start = h.arm9_rom_offset;
            h.digest_ntr_size = align(ntrDataEnd - uint32_t(h.digest_ntr_start), h.digest_sector_size);
            h.sector_hashtable_start = align(uint64_t(h.digest_ntr_start) + uint32_t(h.digest_ntr_size), 0x200);
        }
        uint32_t twlSize = 0;
        for (unsigned int index : {2u, 3u})
            if (programs[index].length)
                twlSize = align(twlSize, 0x400) + programs[index].data->size;
        if (digests) {
            h.digest_twl_size = twlSize ? align(twlSize, h.digest_sector_size) : 0;
            uint64_t count =
                (uint64_t(h.digest_ntr_size) + uint32_t(h.digest_twl_size)) / uint32_t(h.digest_sector_size);
            h.sector_hashtable_size = align(count * 20, uint32_t(h.digest_block_sectorcount) * 20);
            h.block_hashtable_start =
                align(uint64_t(h.sector_hashtable_start) + uint32_t(h.sector_hashtable_size), 0x200);
            h.block_hashtable_size = uint32_t(h.sector_hashtable_size) / uint32_t(h.digest_block_sectorcount);
            ntrEnd = uint32_t(h.block_hashtable_start) + uint32_t(h.block_hashtable_size);
        } else
            ntrEnd = ntrDataEnd;
        uint32_t alignment = boundary ? std::max<uint32_t>(0x80000, boundary & -boundary) : 0x80000;
        boundary = align(ntrEnd, alignment);
        position = boundary + 0x3000;
        uint32_t first = 0;
        for (unsigned int index : {2u, 3u}) {
            auto& item = programs[index];
            if (!item.length) {
                offset(h, index) = 0;
                continue;
            }
            position = align(position, 0x400);
            offset(h, index) = position;
            if (!first)
                first = position;
            position = align(uint64_t(position) + item.data->size, 1);
        }
        if (digests)
            h.digest_twl_start = first;
        end = digests && first ? first + uint32_t(h.digest_twl_size) : position;
    }
    uint32_t capacity = 0x20000;
    while (capacity < end) {
        patchRequire(capacity < 0x40000000, "DSi output exceeds size limit");
        capacity *= 2;
    }
    patchRequire(old.devicecap <= 13, "Invalid DSi cartridge capacity");
    capacity = std::max<uint32_t>(capacity, uint32_t(0x20000) << old.devicecap);
    struct Placement {
        uint32_t start, erase;
        SourcePtr data;
    };
    std::vector<Placement> writes;
    for (auto& item : sections) {
        if (item.generate)
            item.data = item.generate();
        writes.push_back({item.start, preserve ? item.originalSize : 0, item.data});
    }
    for (unsigned int index : {2u, 3u}) {
        auto& item = programs[index];
        length(h, index) = item.length;
        if (!item.length && item.replaced) {
            offset(h, index) = 0;
            std::memset(mac(h, index), 0, 20);
        }
        if (item.length || (preserve && item.replaced && item.originalLength))
            writes.push_back({item.length ? uint32_t(offset(h, index)) : item.start,
                              preserve ? align(item.originalLength, 16) : 0, item.data});
    }
    if (!preserve && digests) {
        uint32_t from = uint32_t(old.sector_hashtable_start) + uint32_t(old.sector_hashtable_size);
        if (from < old.block_hashtable_start && padding(input, from, old.block_hashtable_start, 0)) {
            auto gap = work.map();
            gap->append(Kind::Fill, 0,
                        uint32_t(h.block_hashtable_start) - uint32_t(h.sector_hashtable_start) -
                            uint32_t(h.sector_hashtable_size));
            gap->finish();
            writes.push_back({uint32_t(h.sector_hashtable_start) + uint32_t(h.sector_hashtable_size), 0, gap});
        }
    }
    std::stable_sort(writes.begin(), writes.end(),
                     [](const Placement& a, const Placement& b) { return a.start < b.start; });
    auto output = work.map();
    auto gap = [&](uint32_t next) {
        require(next >= output->size && next <= capacity, "DSi output sections overlap");
        uint32_t copied = preserve ? std::min(next, input->size)
                                   : std::min<uint32_t>(next, std::max<uint32_t>(h.rom_header_size, 0x4000));
        if (copied > output->size)
            output->copy(input, output->size, copied - output->size);
        output->append(Kind::Fill, 255, next - output->size);
    };
    for (const auto& item : writes) {
        gap(item.start);
        output->copy(item.data, 0, item.data->size);
        if (item.erase > item.data->size)
            output->append(Kind::Fill, 255, item.erase - item.data->size);
    }
    gap(capacity);
    output->finish();
    std::array<SourcePtr, 4> plain;
    for (unsigned int index = 0; index < plain.size(); index++) {
        plain[index] = index < 2 ? std::make_shared<Slice>(output, offset(h, index),
                                                           index == 0 ? uint32_t(h.arm9_size) : align(h.arm7_size, 16))
                                 : programs[index].data;
        if (!length(h, index))
            continue;
        auto data = index == 0 ? encryptedSecure(work, plain[index], h, decrypted) : plain[index];
        auto value = hash(data, 0, length(h, index),
                          index == 0   ? "arm9.bin"
                          : index == 1 ? "arm7.bin"
                          : index == 2 ? "arm9i.bin"
                                       : "arm7i.bin");
        std::memcpy(mac(h, index), value.data(), value.size());
        if (index == 0)
            plain[0] = data;
    }
    require(h.arm9_size >= 0x4000, "ARM9 secure area is truncated");
    auto value = hash(plain[0], 0x4000, uint32_t(h.arm9_size) - 0x4000, "arm9.bin");
    std::memcpy(h.hmac_arm9_no_secure, value.data(), value.size());
    value = hash(output, h.banner_offset, h.banner_size, "banner.bin");
    std::memcpy(h.hmac_icon_title, value.data(), value.size());
    if (digests) {
        auto sectors = work.map();
        std::vector<uint8_t> hashes;
        hashes.reserve(BufferSize);
        uint32_t total = uint32_t(h.digest_ntr_size) + uint32_t(h.digest_twl_size), completed = 0;
        progress("Checking game data", 0, total, true);
        auto put = [&](const std::array<uint8_t, 20>& digest) {
            hashes.insert(hashes.end(), digest.begin(), digest.end());
            if (hashes.size() >= BufferSize - 20) {
                sectors->literal(hashes.data(), hashes.size());
                hashes.clear();
            }
        };
        for (unsigned int part : {0u, 1u}) {
            uint32_t start = part == 0 ? uint32_t(h.digest_ntr_start) : uint32_t(h.digest_twl_start);
            uint32_t size = part == 0 ? uint32_t(h.digest_ntr_size) : uint32_t(h.digest_twl_size),
                     sector = h.digest_sector_size;
            auto view = work.map();
            uint32_t position = start;
            std::vector<unsigned int> orderedPrograms{0, 1, 2, 3};
            std::sort(orderedPrograms.begin(), orderedPrograms.end(),
                      [&](unsigned int a, unsigned int b) { return offset(h, a) < offset(h, b); });
            for (auto index : orderedPrograms) {
                uint32_t address = offset(h, index),
                         limit = index == 0 ? uint32_t(h.arm9_size) : align(length(h, index), 16);
                uint32_t first = std::max(start, address), last = std::min(start + size, address + limit);
                if (last <= first)
                    continue;
                require(first >= position, "DSi digest programs overlap");
                view->copy(output, position, first - position);
                view->copy(plain[index], first - address, last - first);
                position = last;
            }
            view->copy(output, position, start + size - position);
            view->finish();
            for (uint32_t position = 0; position < size; position += sector) {
                put(hash(view, position, sector));
                completed += sector;
                progress("Checking game data", completed, total, true);
            }
        }
        sectors->literal(hashes.data(), hashes.size());
        sectors->append(Kind::Fill, 0, uint32_t(h.sector_hashtable_size) - sectors->size);
        sectors->finish();
        auto blocks = work.map();
        hashes.clear();
        for (uint32_t position = 0; position < sectors->size; position += uint32_t(h.digest_block_sectorcount) * 20) {
            auto digest = hash(sectors, position, uint32_t(h.digest_block_sectorcount) * 20);
            hashes.insert(hashes.end(), digest.begin(), digest.end());
            if (hashes.size() >= BufferSize - 20) {
                blocks->literal(hashes.data(), hashes.size());
                hashes.clear();
            }
        }
        blocks->literal(hashes.data(), hashes.size());
        blocks->finish();
        require(blocks->size == h.block_hashtable_size, "DSi block digest size mismatch");
        value = hash(blocks, 0, blocks->size);
        std::memcpy(h.hmac_digest_master, value.data(), value.size());
        output = replace(work, output, h.sector_hashtable_start, sectors);
        output = replace(work, output, h.block_hashtable_start, blocks);
    }
    for (const auto& region : regions) {
        auto& item = programs[region.program];
        uint32_t size = !item.length       ? 0
                        : region.kind == 0 ? item.data->size
                        : region.kind == 1 ? std::min<uint32_t>(0x4000, item.data->size)
                                           : region.length;
        patchRequire(!size || uint64_t(region.offset) + size <= item.data->size,
                     "Modcrypt region exceeds program after replacement");
        uint32_t start = size ? uint32_t(offset(h, region.program)) + region.offset : 0;
        if (region.id == 0) {
            h.modcrypt1_start = start;
            h.modcrypt1_size = size;
        } else {
            h.modcrypt2_start = start;
            h.modcrypt2_size = size;
        }
        if (size && (h.dsi_flags & 2)) {
            auto bytes = twlCrypt(work, std::make_shared<Slice>(output, start, size), 0, size, h,
                                  region.id == 0 ? h.hmac_arm9 : h.hmac_arm7);
            output = replace(work, output, start, bytes);
        }
    }
    h.application_end_offset = ntrEnd;
    h.total_rom_size = end;
    h.offset_0x90 = boundary / 0x80000 * 0x10001;
    h.devicecap = 0;
    for (uint32_t size = capacity; size > 0x20000; size /= 2)
        h.devicecap++;
    auto secure = readBytes(plain[0], 0, 0x4000);
    h.secure_area_crc = CalcCrc16(secure.data(), secure.size());
    std::memcpy(h.rsa_signature, old.rsa_signature, sizeof(h.rsa_signature));
    std::memcpy(headerData.data(), &h, sizeof(h));
    auto finalHeader = reinterpret_cast<Header*>(headerData.data());
    finalHeader->logo_crc = CalcCrc16(headerData.data() + 0xc0, 156);
    finalHeader->header_crc = CalcCrc16(headerData.data(), 0x15e);
    output = overwrite(work, output, 0, headerData.data(), headerData.size());
    auto originalMirror = std::make_shared<Slice>(input, 0x8000, 0x1000);
    auto mirror = std::make_shared<Slice>(output, 0x8000, 0x1000);
    for (uint32_t start = 0x1000; start < 0x4000; start += 0x1000)
        if (equalSources(std::make_shared<Slice>(input, start, 0x1000), originalMirror))
            output = replace(work, output, start, mirror);
    if (prefix) {
        uint32_t oldBoundary = (uint32_t(old.offset_0x90) >> 16) * 0x80000;
        for (unsigned int index = 0; index < 3; index++) {
            auto data = std::make_shared<Slice>(input, oldBoundary + index * 0x1000, 0x1000);
            output = replace(work, output, boundary + index * 0x1000,
                             equalSources(data, originalMirror) ? SourcePtr(mirror) : SourcePtr(data));
        }
    }
    return output;
}

}
