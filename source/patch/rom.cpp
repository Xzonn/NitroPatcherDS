#include "rom.h"
#include "package.h"
#include "banner.h"
#include "filename.h"
#include "fnt.h"
#include "twl.h"
#include <cstring>
#include <map>
#include <set>
#include <charconv>

namespace np3d {

struct Allocation { unsigned_int start, end; };
using RomFile = FileEntry;

static std::string overlayPath(unsigned int cpu, uint32_t id) {
    char name[48];
    std::snprintf(name, sizeof(name), cpu == 9 ? "overlay/overlay_%04u.bin" : "overlay/overlay7_%04u.bin", unsigned(id));
    return name;
}

static std::vector<OverlayEntry> readOverlays(SourcePtr data) {
    patchRequire(data->size % sizeof(OverlayEntry) == 0, "Invalid replacement overlay table");
    std::vector<OverlayEntry> table;
    for (uint32_t position = 0; position < data->size; position += sizeof(OverlayEntry)) {
        OverlayEntry item;
        data->read(position, &item, sizeof(item));
        if (item.id == 0xffffffff) break;
        table.push_back(item);
    }
    return table;
}

static uint32_t addNamedFile(FileTable& names, std::vector<NamedDirectory>& directories, const std::string& path) {
    std::vector<std::string> parts;
    for (size_t first = 0;;) {
        size_t end = path.find('/', first);
        parts.push_back(path.substr(first, end == std::string::npos ? end : end - first));
        if (end == std::string::npos) break;
        first = end + 1;
    }
    patchRequire(parts.size() >= 2 && normalize(parts.front()) == "data", "Invalid NitroFS path: " + path);
    patchRequire(parts.size() <= 258, "FNT directory depth exceeds 256");
    uint32_t parent = 0;
    std::string output = "data";
    for (size_t index = 1; index < parts.size(); index++) {
        const auto& name = parts[index];
        patchRequire(!name.empty() && name != "." && name != ".." && name.find('\0') == std::string::npos, "Invalid NitroFS path: " + path);
        std::string key = normalize(name);
        for (auto file : directories[parent].files)
            patchRequire(normalize(decodeName(names.get(file).rawName)) != key, "ROM path conflicts with a file: " + path);
        uint32_t child = UINT32_MAX;
        for (auto folder : directories[parent].children) if (normalize(directories[folder].name) == key) { child = folder; break; }
        if (index + 1 == parts.size()) {
            patchRequire(child == UINT32_MAX, "ROM path conflicts with a directory: " + path);
            uint32_t file = names.size();
            names.push_back({output + "/" + name, UINT32_MAX, nullptr, UINT32_MAX, 0, 0, encodeName(name), parent});
            directories[parent].files.push_back(file);
            return file;
        }
        if (child == UINT32_MAX) {
            patchRequire(directories.size() < 4096, "FNT directory count exceeds 4096");
            child = directories.size();
            auto raw = encodeName(name);
            directories.push_back({name, raw, parent, {}, {}});
            directories[parent].children.push_back(child);
        }
        parent = child;
        output += "/" + directories[parent].name;
    }
    fail("Invalid NitroFS path: " + path);
}

static SourcePtr layoutFiles(Workspace& work, SourcePtr input, FileTable& files,
    FileTable& names, std::vector<NamedDirectory>& directories,
    const std::vector<uint32_t>& positions, const std::vector<uint32_t>& addedOverlays, std::vector<Allocation>& allocations,
    std::vector<uint32_t>& order, std::vector<uint32_t>& oldIds) {
    std::vector<bool> named(allocations.size(), false), overlays(allocations.size(), false);
    std::vector<uint32_t> existing(allocations.size(), UINT32_MAX);
    for (uint32_t index = 0; index < names.size(); index++) {
        auto name = names.get(index);
        if (name.id != UINT32_MAX) named[name.id] = true;
    }
    for (uint32_t index = 0; index < files.size(); index++) {
        auto file = files.get(index);
        if (file.id == 0xffff || file.id == UINT32_MAX) continue;
        existing[file.id] = index;
        if (file.name.compare(0, 8, "overlay/") == 0) overlays[file.id] = true;
    }
    for (bool overlay : {true, false}) {
        for (uint32_t id = 0; id < allocations.size(); id++) {
            if (named[id] || overlays[id] != overlay) continue;
            uint32_t index = existing[id];
            if (index == UINT32_MAX) {
                index = files.size();
                files.push_back({"FAT entry " + std::to_string(id), id,
                    std::make_shared<Slice>(input, allocations[id].start, uint32_t(allocations[id].end) - uint32_t(allocations[id].start)), id,
                    allocations[id].start, uint32_t(allocations[id].end) - uint32_t(allocations[id].start)});
            }
            order.push_back(index);
        }
        if (overlay) order.insert(order.end(), addedOverlays.begin(), addedOverlays.end());
    }
    std::vector<uint32_t> pending{0}, orderedDirectories, firstFiles(directories.size());
    while (!pending.empty()) {
        uint32_t index = pending.back();
        pending.pop_back();
        auto& folder = directories[index];
        std::sort(folder.files.begin(), folder.files.end(), [&](uint32_t a, uint32_t b) { return names.get(a).rawName < names.get(b).rawName; });
        std::sort(folder.children.begin(), folder.children.end(), [&](uint32_t a, uint32_t b) { return directories[a].rawName < directories[b].rawName; });
        firstFiles[index] = order.size();
        orderedDirectories.push_back(index);
        for (auto file : folder.files) order.push_back(positions[file]);
        for (auto child = folder.children.rbegin(); child != folder.children.rend(); child++) pending.push_back(*child);
    }
    patchRequire(order.size() <= 0xf000, "Filesystem file IDs exceed 0xEFFF");
    oldIds.assign(allocations.size(), UINT32_MAX);
    for (uint32_t id = 0; id < order.size(); id++) {
        uint32_t old = files.get(order[id]).id;
        if (old != UINT32_MAX) oldIds[old] = id;
    }
    for (uint32_t index = 0; index < files.size(); index++) {
        auto file = files.get(index);
        if (file.id != 0xffff && file.id != UINT32_MAX) files.setId(index, oldIds[file.id]);
    }
    for (uint32_t id = 0; id < order.size(); id++) files.setId(order[id], id);
    std::vector<uint32_t> directoryIds(directories.size());
    for (uint32_t index = 0; index < orderedDirectories.size(); index++) directoryIds[orderedDirectories[index]] = 0xf000 + index;
    auto table = std::make_shared<Disk>(work.directory + "/fnt-" + std::to_string(work.next++) + ".bin", "w+b");
    unsigned int entry = directories.size() * 8;
    for (auto index : orderedDirectories) {
        const auto& folder = directories[index];
        std::vector<FntName> entries;
        std::vector<std::string> rawNames;
        rawNames.reserve(folder.files.size());
        for (auto file : folder.files) rawNames.push_back(names.get(file).rawName);
        for (const auto& name : rawNames) entries.push_back({name.data(), unsigned(name.size()), 0});
        for (auto child : folder.children) entries.push_back({directories[child].rawName.data(), unsigned(directories[child].rawName.size()), uint16_t(directoryIds[child])});
        uint32_t parent = folder.parent == UINT32_MAX ? directories.size() : directoryIds[folder.parent];
        require(WriteFntDirectory(table->handle, 0, directoryIds[index], entry, firstFiles[index], parent, entries), "Cannot write NDS file name table");
    }
    table->flush();
    table->size = entry;
    table->writing = true;
    auto output = work.map();
    std::vector<uint8_t> buffer(BufferSize);
    for (uint32_t offset = 0; offset < entry;) {
        uint32_t take = std::min(BufferSize, entry - offset);
        table->read(offset, buffer.data(), take);
        output->literal(buffer.data(), take);
        offset += take;
    }
    output->finish();
    return output;
}

std::shared_ptr<Map> overwrite(Workspace& work, SourcePtr input, uint32_t offset, const void* data, uint32_t length) {
    require(offset <= input->size && length <= input->size - offset, "Write outside virtual ROM");
    auto output = work.map();
    output->copy(input, 0, offset);
    output->literal(data, length);
    output->copy(input, offset + length, input->size - offset - length);
    output->finish();
    return output;
}

void secureArea(std::vector<uint8_t>& data, const Header& header, bool decrypted) {
    if (decrypted) {
        require(data.size() >= 0x800, "Secure area is truncated");
        uint32_t code;
        std::memcpy(&code, header.gamecode, sizeof(code));
        encrypt_arm9(code, data.data());
    }
}

static std::vector<uint8_t> bannerBytes(SourcePtr input, uint32_t hardSize) {
    unsigned_short version;
    input->read(0, &version, sizeof(version));
    uint32_t size = CalcBannerSize(version);
    if ((uint16_t(version) >> 8) == 1) size = hardSize && hardSize != 0xffffffff ? std::min<uint32_t>(hardSize, 0x23c0) : 0x23c0;
    auto bytes = readBytes(input, 0, size);
    uint32_t titles = version >= 3 ? 8 : version >= 2 ? 7 : 6;
    for (uint32_t language = 0; language < titles; language++) {
        auto text = reinterpret_cast<unsigned_short*>(bytes.data() + 0x240 + language * 256);
        bool ended = false;
        for (unsigned int index = 0; index < 128; index++) {
            if (ended) text[index] = 0;
            else if (text[index] == 0) ended = true;
        }
    }
    auto checksums = reinterpret_cast<unsigned_short*>(bytes.data() + 2);
    checksums[0] = CalcCrc16(bytes.data() + 0x20, 0x820);
    if (version >= 2) checksums[1] = CalcCrc16(bytes.data() + 0x20, 0x920);
    if (version >= 3) checksums[2] = CalcCrc16(bytes.data() + 0x20, 0xa20);
    if ((uint16_t(version) >> 8) >= 1) {
        require(size >= 0x23c0, "Animated banner is truncated");
        checksums[3] = CalcCrc16(bytes.data() + 0x1240, 0x1180);
    }
    return bytes;
}

static std::shared_ptr<Map> buildCompleteRom(Workspace& work, SourcePtr input, Package& package, CompleteTwl& twl,
    Header& h, std::vector<uint8_t> headerData, FileTable& files, SourcePtr fnt,
    std::vector<Allocation>& allocations, std::array<std::vector<OverlayEntry>, 2>& tables, bool changed) {
    progress("Preparing DSi files", 0, 0);
    auto system = [&](const std::string& name) -> RomFile {
        uint32_t found = files.find(name);
        require(found != UINT32_MAX, "Missing DSi file: " + name);
        return files.get(found);
    };
    auto patched = [&](const std::string& name) { return package.has(name) || package.has("xdelta/" + name); };
    auto arm9File = system("arm9.bin");
    auto arm9 = arm9File.data;
    uint32_t marker = 0;
    if (arm9->size >= 12) arm9->read(arm9->size - 12, &marker, sizeof(marker));
    bool nitrocode = marker == 0xdec00621;
    uint64_t secureMarker = 0;
    if (arm9->size >= 8) arm9->read(0, &secureMarker, sizeof(secureMarker));
    bool decrypted = secureMarker == UINT64_C(0xe7ffdeffe7ffdeff);
    auto oldHeader = std::make_unique<Header>();
    input->read(0, oldHeader.get(), sizeof(Header));
    if (!decrypted && std::memcmp(h.gamecode, oldHeader->gamecode, 4)) {
        auto secure = readBytes(arm9, 0, 0x4000);
        uint32_t previous, current;
        std::memcpy(&previous, oldHeader->gamecode, 4);
        std::memcpy(&current, h.gamecode, 4);
        decrypt_arm9(previous, secure.data());
        encrypt_arm9(current, secure.data());
        arm9 = overwrite(work, arm9, 0, secure.data(), secure.size());
    }
    std::vector<SourcePtr> signedOverlays;
    for (auto& entry : tables[0]) if (uint32_t(entry.reserved) & 0x2000000) signedOverlays.push_back(system(overlayPath(9, entry.id)).data);
    arm9 = twl.updateOverlays(arm9, h, nitrocode, signedOverlays, patched("arm9.bin"));
    patchRequire(arm9->size >= 0x4000 + (nitrocode ? 12 : 0), "ARM9 secure area is truncated");
    h.arm9_size = arm9->size - (nitrocode ? 12 : 0);
    auto secure = readBytes(arm9, 0, 0x4000);
    uint32_t code;
    std::memcpy(&code, h.gamecode, 4);
    if (!decrypted) decrypt_arm9(code, secure.data());
    unsigned_short crc = CalcCrc16(secure.data() + 0x10, 0x7f0);
    std::memcpy(secure.data() + 0xe, &crc, sizeof(crc));
    if (!decrypted) encrypt_arm9(code, secure.data());
    arm9 = overwrite(work, arm9, 0, secure.data(), secure.size());
    std::vector<TwlSection> sections;
    auto add = [&](const RomFile& file, SourcePtr data, std::function<void(uint32_t)> place) {
        sections.push_back({file.offset, file.size, data, place, {}});
    };
    add(arm9File, arm9, [&](uint32_t start) { h.arm9_rom_offset = start; });
    std::vector<Allocation> fat(changed ? 0 : allocations.size());
    if (changed) {
        uint32_t count = 0;
        for (uint32_t index = 0; index < files.size(); index++) {
            auto file = files.get(index);
            if (file.id != 0xffff && file.id != UINT32_MAX) count = std::max(count, file.id + 1);
        }
        fat.resize(count);
    }
    std::vector<uint32_t> byId(fat.size(), UINT32_MAX);
    for (uint32_t index = 0; index < files.size(); index++) {
        auto file = files.get(index);
        if (file.id != 0xffff && file.id != UINT32_MAX
            && (byId[file.id] == UINT32_MAX || file.name.compare(0, 8, "overlay/") == 0)) byId[file.id] = index;
    }
    std::vector<bool> written(fat.size());
    auto payload = [&](uint32_t id) {
        if (written[id]) return;
        written[id] = true;
        bool present = byId[id] != UINT32_MAX;
        auto file = present ? files.get(byId[id]) : RomFile{};
        SourcePtr data = present ? file.data : std::make_shared<Slice>(input, allocations[id].start, uint32_t(allocations[id].end) - uint32_t(allocations[id].start));
        uint32_t start = present ? file.offset : uint32_t(allocations[id].start), size = present ? file.size : data->size;
        sections.push_back({start, size, data, [&, id, data](uint32_t position) { fat[id].start = position; fat[id].end = position + data->size; }, {}});
    };
    auto overlays = [&](unsigned int cpu) {
        std::string name = cpu == 9 ? "overarm9.bin" : "overarm7.bin";
        uint32_t found = files.find(name);
        if (found == UINT32_MAX) return;
        auto tableFile = files.get(found);
        auto& table = tables[cpu == 9 ? 0 : 1];
        for (auto& entry : table) {
            auto file = system(overlayPath(cpu, entry.id));
            uint32_t flags = entry.reserved;
            if ((flags & 0x1000000) || (cpu == 7 && flags)) entry.reserved = (flags & 0xff000000) | (file.data->size & 0xffffff);
            entry.file_id = file.id;
        }
        auto data = work.map();
        data->literal(table.data(), table.size() * sizeof(OverlayEntry));
        data->finish();
        if (cpu == 9) h.arm9_overlay_size = data->size;
        else h.arm7_overlay_size = data->size;
        add(tableFile, data, [&, cpu](uint32_t start) { (cpu == 9 ? h.arm9_overlay_offset : h.arm7_overlay_offset) = start; });
        for (auto& entry : table) payload(entry.file_id);
    };
    overlays(9);
    auto arm7 = system("arm7.bin");
    h.arm7_size = arm7.data->size;
    add(arm7, arm7.data, [&](uint32_t start) { h.arm7_rom_offset = start; });
    overlays(7);
    h.fnt_size = fnt->size;
    add(system("fnt.bin"), fnt, [&](uint32_t start) { h.fnt_offset = start; });
    h.fat_size = fat.size() * sizeof(Allocation);
    auto emptyFat = work.map();
    emptyFat->append(Kind::Fill, 0, h.fat_size);
    emptyFat->finish();
    add(system("fat.bin"), emptyFat, [&](uint32_t start) { h.fat_offset = start; });
    sections.back().generate = [&]() {
        auto output = work.map();
        output->literal(fat.data(), fat.size() * sizeof(Allocation));
        output->finish();
        return output;
    };
    auto banner = system("banner.bin");
    auto bytes = bannerBytes(banner.data, patched("banner.bin") ? banner.data->size : uint32_t(h.banner_size));
    h.banner_size = bytes.size();
    auto data = work.map();
    data->literal(bytes.data(), bytes.size());
    data->finish();
    add(banner, data, [&](uint32_t start) { h.banner_offset = start; });
    for (uint32_t id = 0; id < fat.size(); id++) payload(id);
    for (unsigned int index : {2u, 3u}) {
        std::string name = index == 2 ? "arm9i.bin" : "arm7i.bin";
        if (patched(name)) twl.setProgram(index, system(name).data);
    }
    return twl.serialize(h, std::move(headerData), sections, changed, decrypted);
}

static std::shared_ptr<Map> buildRomInput(Workspace& work, SourcePtr input, Package& package) {
    require(input->size >= 512, "NDS header is truncated");
    auto headerStorage = std::make_unique<Header>();
    Header& h = *headerStorage;
    std::memset(&h, 0, sizeof(h));
    input->read(0, &h, std::min<uint32_t>(input->size, sizeof(h)));
    bool format2 = package.metadata.formatVersion == 2;
    if (format2) patchRequire(h.unitcode == 0 || h.unitcode == 2, "Unsupported ROM unit code for XZP format 2");
    uint32_t headerSize = h.rom_header_size;
    require(headerSize >= 0x170 && headerSize <= input->size, "Invalid NDS header size");
    if (h.unitcode & 2) require(headerSize >= 0x1000, "DSi header is truncated");
    auto headerData = readBytes(input, 0, headerSize);
    uint32_t marker = 0;
    uint32_t footer = uint32_t(h.arm9_rom_offset) + uint32_t(h.arm9_size);
    if (footer <= input->size - 4) input->read(footer, &marker, sizeof(marker));
    bool nitrocode = marker == 0xdec00621;
    uint64_t secureMarker = 0;
    if (input->size >= 0x4008) input->read(0x4000, &secureMarker, sizeof(secureMarker));
    bool decrypted = secureMarker == UINT64_C(0xe7ffdeffe7ffdeff);

    require(uint32_t(h.fat_size) % sizeof(Allocation) == 0 && uint32_t(h.fat_size) / 8 <= 65535, "Invalid NDS FAT size");
    std::vector<Allocation> allocations(uint32_t(h.fat_size) / sizeof(Allocation));
    input->read(h.fat_offset, allocations.data(), h.fat_size);
    std::vector<uint32_t> sorted;
    for (uint32_t index = 0; index < allocations.size(); index++) {
        auto& item = allocations[index];
        require(item.start <= item.end && item.end <= input->size, "NDS allocation outside ROM");
        sorted.push_back(index);
    }
    std::sort(sorted.begin(), sorted.end(), [&](uint32_t a, uint32_t b) {
        return allocations[a].start == allocations[b].start ? a < b : allocations[a].start < allocations[b].start;
    });
    FileTable files(work, input);
    auto add = [&](const std::string& name, uint32_t id, uint32_t offset, uint32_t size) {
        files.push_back({name, id, std::make_shared<Slice>(input, offset, size), id, offset, size});
    };
    auto bannerInput = std::make_shared<Slice>(input, h.banner_offset, input->size - uint32_t(h.banner_offset));
    auto originalBanner = bannerBytes(bannerInput, h.unitcode & 2 ? uint32_t(h.banner_size) : 0);
    add("header.bin", 0xffff, 0, headerSize);
    add("banner.bin", 0xffff, h.banner_offset, originalBanner.size());
    add("fnt.bin", 0xffff, h.fnt_offset, h.fnt_size);
    add("fat.bin", 0xffff, h.fat_offset, h.fat_size);
    add("arm9.bin", 0xffff, h.arm9_rom_offset, uint32_t(h.arm9_size) + (nitrocode ? 12 : 0));
    add("arm7.bin", 0xffff, h.arm7_rom_offset, h.arm7_size);
    if (h.arm9_overlay_size) add("overarm9.bin", 0xffff, h.arm9_overlay_offset, h.arm9_overlay_size);
    if (h.arm7_overlay_size) add("overarm7.bin", 0xffff, h.arm7_overlay_offset, h.arm7_overlay_size);
    bool addedTable = false;
    if (format2) for (const char* table : {"overarm9.bin", "overarm7.bin"}) {
        bool present = files.find(table) != UINT32_MAX;
        if (!present && (package.has(table) || package.has(std::string("xdelta/") + table))) {
            add(table, 0xffff, 0, 0);
            addedTable = true;
        }
    }
    progress("Reading game directory", 0, 0);
    std::vector<NamedDirectory> directories;
    auto names = enumerateFiles(input, h, work, format2, format2 ? &directories : nullptr);
    std::vector<uint32_t> namedPositions;
    for (uint32_t index = 0; index < names.size(); index++) {
        auto item = names.get(index);
        namedPositions.push_back(files.size());
        add(item.name, item.id, item.offset, item.size);
    }
    std::array<std::vector<uint32_t>, 2> originalOverlays;
    std::vector<SourcePtr> signedOverlays;
    for (unsigned int cpu : {9u, 7u}) {
        uint32_t offset = cpu == 9 ? uint32_t(h.arm9_overlay_offset) : uint32_t(h.arm7_overlay_offset);
        uint32_t size = cpu == 9 ? uint32_t(h.arm9_overlay_size) : uint32_t(h.arm7_overlay_size);
        require(size % sizeof(OverlayEntry) == 0, "Invalid NDS overlay table size");
        for (uint32_t position = 0; position < size; position += sizeof(OverlayEntry)) {
            OverlayEntry item;
            input->read(offset + position, &item, sizeof(item));
            if (item.id == 0xffffffff) break;
            require(item.file_id < allocations.size(), "Overlay file ID outside FAT");
            auto& allocation = allocations[item.file_id];
            originalOverlays[cpu == 9 ? 0 : 1].push_back(item.id);
            add(overlayPath(cpu, item.id), item.file_id, allocation.start, uint32_t(allocation.end) - uint32_t(allocation.start));
            if (cpu == 9 && (uint32_t(item.reserved) & 0x2000000)) signedOverlays.push_back(files.get(files.size() - 1).data);
        }
    }
    std::unique_ptr<CompleteTwl> twl;
    if (format2 && h.unitcode == 2 && !work.dsOnly) {
        twl = std::make_unique<CompleteTwl>(work, input, h, signedOverlays);
        files.push_back({"arm9i.bin", 0xffff, twl->program(2), 0xffff, h.dsi9_rom_offset, h.dsi9_size});
        files.push_back({"arm7i.bin", 0xffff, twl->program(3), 0xffff, h.dsi7_rom_offset, h.dsi7_size});
    }
    {
        std::vector<uint32_t> paths(files.size());
        for (uint32_t index = 0; index < paths.size(); index++) paths[index] = index;
        std::sort(paths.begin(), paths.end(), [&](uint32_t a, uint32_t b) { return normalize(files.name(a)) < normalize(files.name(b)); });
        for (uint32_t index = 1; index < paths.size(); index++)
            patchRequire(normalize(files.name(paths[index - 1])) != normalize(files.name(paths[index])), "Duplicate ROM path: " + files.name(paths[index]));
    }
    auto contains = [&](const std::string& name) { return files.find(name) != UINT32_MAX; };
    std::vector<std::pair<std::string, std::string>> additions;
    if (format2) {
        for (const auto& folder : directories) {
            std::set<std::string> children;
            for (auto file : folder.files) patchRequire(children.insert(normalize(decodeName(names.get(file).rawName))).second, "Duplicate FNT path");
            for (auto child : folder.children) patchRequire(children.insert(normalize(directories[child].name)).second, "Duplicate FNT path");
        }
        for (const auto& item : package.entries) {
            bool delta = item.first.compare(0, 7, "xdelta/") == 0;
            std::string target = delta ? item.first.substr(7) : item.first;
            if ((target.compare(0, 5, "data/") != 0 && target.compare(0, 8, "overlay/") != 0) || contains(target)) continue;
            if (!delta && package.has("xdelta/" + target)) continue;
            additions.push_back({delta ? item.second.name.substr(7) : item.second.name, target});
        }
        std::sort(additions.begin(), additions.end());
        patchRequire(additions.empty() || (!package.has("fnt.bin") && !package.has("xdelta/fnt.bin")), "Adding files conflicts with an fnt.bin replacement");
    }
    uint32_t completed = 0;
    uint32_t fileCount = files.size() + additions.size();
    bool identical = !addedTable && additions.empty();
    for (uint32_t index = 0; index < files.size(); index++) {
        auto file = files.get(index);
        progress("Preparing game files", completed, fileCount, false, file.name.c_str());
        auto original = file.data;
        file.data = package.replace(file.name, original);
        if (!file.data) return nullptr;
        files.setData(index, file.data);
        if (twl && identical && file.data != original && (!equalSources(file.data, original)
            || ((file.name == "arm9i.bin" || file.name == "arm7i.bin") && !file.data->size))) identical = false;
        progress("Preparing game files", ++completed, fileCount, false, file.name.c_str());
    }
    auto system = [&](const std::string& name) -> SourcePtr {
        uint32_t found = files.find(name);
        require(found != UINT32_MAX, "Missing system file: " + name);
        return files.get(found).data;
    };
    std::array<std::vector<OverlayEntry>, 2> overlayTables;
    if (format2) for (unsigned int cpu : {9u, 7u}) {
        std::string name = cpu == 9 ? "overarm9.bin" : "overarm7.bin";
        if (contains(name)) overlayTables[cpu == 9 ? 0 : 1] = readOverlays(system(name));
    }
    std::vector<std::pair<std::pair<unsigned int, uint32_t>, uint32_t>> newOverlayFiles;
    for (const auto& addition : additions) {
        bool overlay = addition.second.compare(0, 8, "overlay/") == 0;
        uint32_t file = UINT32_MAX;
        unsigned int cpu = 9;
        uint32_t overlayId = 0;
        if (overlay) {
            const auto& path = addition.second;
            bool arm7 = path.compare(0, 17, "overlay/overlay7_") == 0;
            size_t first = arm7 ? 17 : 16;
            bool syntax = path.size() >= first + 8 && path.compare(path.size() - 4, 4, ".bin") == 0;
            if (syntax) {
                auto end = path.data() + path.size() - 4;
                auto parsed = std::from_chars(path.data() + first, end, overlayId);
                syntax = parsed.ec == std::errc() && parsed.ptr == end && overlayId != UINT32_MAX;
            }
            cpu = arm7 ? 7 : 9;
            patchRequire(syntax && overlayPath(cpu, overlayId) == path, "Invalid overlay path: " + addition.first);
            std::string tableName = cpu == 9 ? "overarm9.bin" : "overarm7.bin";
            patchRequire(contains(tableName), "New overlay requires " + tableName + ": " + addition.first);
            auto& table = overlayTables[cpu == 9 ? 0 : 1];
            patchRequire(std::any_of(table.begin(), table.end(), [&](OverlayEntry& item) { return item.id == overlayId; }),
                "New overlay is missing its table entry: " + addition.first);
        } else file = addNamedFile(names, directories, addition.first);
        progress("Preparing game files", completed, fileCount, false, addition.first.c_str());
        auto data = package.replace(addition.second, std::make_shared<Slice>(input, 0, 0));
        if (!data) return nullptr;
        if (overlay) newOverlayFiles.push_back({{cpu == 9 ? 0u : 1u, overlayId}, files.size()});
        else namedPositions.push_back(files.size());
        files.push_back({overlay ? overlayPath(cpu, overlayId) : names.name(file), UINT32_MAX, data, UINT32_MAX});
        progress("Preparing game files", ++completed, fileCount, false, addition.first.c_str());
    }
    if (format2) for (unsigned int cpu : {9u, 7u}) {
        size_t index = cpu == 9 ? 0 : 1;
        std::set<uint32_t> ids;
        for (auto& item : overlayTables[index]) {
            patchRequire(ids.insert(item.id).second, "Duplicate overlay ID: " + std::to_string(uint32_t(item.id)));
            patchRequire(contains(overlayPath(cpu, item.id)), "Overlay table entry has no payload: " + overlayPath(cpu, item.id));
        }
        for (auto id : originalOverlays[index]) patchRequire(ids.count(id), "Removing overlays is unsupported");
    }
    if (twl && identical) {
        auto output = work.map();
        output->copy(input, 0, input->size);
        output->finish();
        return output;
    }
    SourcePtr fnt = system("fnt.bin");
    if (package.has("fnt.bin") || package.has("xdelta/fnt.bin")) {
        auto combined = work.map();
        combined->copy(input, 0, input->size);
        combined->copy(fnt, 0, fnt->size);
        combined->finish();
        auto replacementStorage = std::make_unique<Header>(h);
        Header& replacement = *replacementStorage;
        replacement.fnt_offset = input->size;
        replacement.fnt_size = fnt->size;
        auto newNames = enumerateFiles(combined, replacement, work, format2);
        std::vector<uint32_t> before, after;
        for (uint32_t index = 0; index < names.size(); index++) before.push_back(names.get(index).id);
        for (uint32_t index = 0; index < newNames.size(); index++) after.push_back(newNames.get(index).id);
        std::sort(before.begin(), before.end());
        std::sort(after.begin(), after.end());
        patchRequire(before == after, "Replacement FNT must preserve existing file IDs");
    }
    bool changed = !additions.empty() || addedTable;
    patchRequire(!changed || (!package.has("fnt.bin") && !package.has("xdelta/fnt.bin")), "Adding files conflicts with an fnt.bin replacement");
    std::vector<uint32_t> layout, oldIds;
    if (changed) {
        std::sort(newOverlayFiles.begin(), newOverlayFiles.end());
        std::vector<uint32_t> addedOverlays;
        for (const auto& file : newOverlayFiles) addedOverlays.push_back(file.second);
        fnt = layoutFiles(work, input, files, names, directories, namedPositions, addedOverlays, allocations, layout, oldIds);
        sorted.resize(layout.size());
        for (uint32_t index = 0; index < sorted.size(); index++) sorted[index] = index;
    }
    names.clear();
    directories.clear();
    directories.shrink_to_fit();
    if (package.has("header.bin") || package.has("xdelta/header.bin")) {
        auto replacementStorage = std::make_unique<Header>();
        Header& replacement = *replacementStorage;
        auto data = system("header.bin");
        patchRequire(data->size >= 0x170, "Replacement header is truncated");
        data->read(0, &replacement, std::min<uint32_t>(sizeof(replacement), data->size));
        patchRequire(uint32_t(replacement.rom_header_size) >= 0x170 && replacement.rom_header_size <= data->size,
            "Invalid replacement header size");
        if (twl) {
            patchRequire(data->size >= sizeof(Header) && replacement.unitcode == 2 && replacement.tid_high == h.tid_high
                && replacement.tid_low == h.tid_low && (!h.dsi9_size || replacement.dsi9_size) && (!h.dsi7_size || replacement.dsi7_size),
                "Replacement header changes or clears the DSi cartridge identity or programs");
            auto original = std::make_unique<Header>(h);
            h = replacement;
            for (auto range : {std::pair<uint32_t, uint32_t>{0x20, 0x40}, {0x40, 0x60}, {0x68, 0x6e}, {0x80, 0x88},
                {0x90, 0x94}, {0x1c0, 0x1c4}, {0x1cc, 0x1d4}, {0x1dc, 0x214}, {0x220, 0x230}, {0x300, 0x378}, {0x3a0, 0x3b4}, {0xf80, 0x1000}})
                std::memcpy(reinterpret_cast<uint8_t*>(&h) + range.first, reinterpret_cast<uint8_t*>(original.get()) + range.first, range.second - range.first);
            h.arm9_entry_address = replacement.arm9_entry_address;
            h.arm9_ram_address = replacement.arm9_ram_address;
            h.arm7_entry_address = replacement.arm7_entry_address;
            h.arm7_ram_address = replacement.arm7_ram_address;
            h.dsi7_ram_address = replacement.dsi7_ram_address;
        } else {
            std::memcpy(h.title, replacement.title, sizeof(h.title));
            std::memcpy(h.gamecode, replacement.gamecode, sizeof(h.gamecode));
        }
    }
    if (twl) return buildCompleteRom(work, input, package, *twl, h, std::move(headerData), files, fnt, allocations, overlayTables, changed);
    std::vector<uint8_t> signature;
    uint32_t logicalSize = h.application_end_offset;
    if (logicalSize >= headerSize && logicalSize <= input->size && input->size - logicalSize >= 0x88) {
        uint32_t tag;
        input->read(logicalSize, &tag, sizeof(tag));
        if (tag == 0x00016361) signature = readBytes(input, logicalSize, 0x88);
    }
    auto output = work.map();
    progress("Arranging game files", 0, input->size, true);
    output->copy(input, 0, headerSize);
    std::vector<Allocation> placements(changed ? layout.size() : allocations.size());
    std::vector<uint32_t> byId(placements.size(), UINT32_MAX);
    for (uint32_t index = 0; index < files.size(); index++) {
        auto file = files.get(index);
        if (file.id != 0xffff && (byId[file.id] == UINT32_MAX || (changed && file.name.compare(0, 8, "overlay/") == 0))) byId[file.id] = index;
    }
    std::vector<bool> written(placements.size(), false);
    std::memset(placements.data(), 0, placements.size() * sizeof(Allocation));
    auto pad = [&]() { output->append(Kind::Fill, 255, (512 - output->size % 512) % 512); };
    auto write = [&](SourcePtr data, uint32_t id, bool padding) {
        if (changed && id != 0xffff) {
            if (written[id]) return;
            written[id] = true;
            data = files.get(byId[id]).data;
        }
        progress("Arranging game files", 0, data->size, true);
        uint32_t start = output->size;
        output->copy(data, 0, data->size);
        if (id != 0xffff) { placements[id].start = start; placements[id].end = output->size; }
        if (padding) pad();
        progress("Arranging game files", data->size, data->size, true);
    };
    std::set<uint32_t> overlayIds;
    auto overlays = [&](unsigned int cpu) {
        std::string name = cpu == 9 ? "overarm9.bin" : "overarm7.bin";
        if (!contains(name)) return;
        auto data = system(name);
        auto table = format2 ? std::move(overlayTables[cpu == 9 ? 0 : 1]) : readOverlays(data);
        std::string prefix = cpu == 9 ? "overlay/overlay_" : "overlay/overlay7_";
        std::vector<uint32_t> payloads;
        for (uint32_t index = 0; index < files.size(); index++) if (files.name(index).compare(0, prefix.size(), prefix) == 0) payloads.push_back(index);
        std::stable_sort(payloads.begin(), payloads.end(), [&](uint32_t a, uint32_t b) { return files.get(a).original < files.get(b).original; });
        require(payloads.size() == table.size(), "Overlay table does not match its payloads");
        std::map<uint32_t, OverlayEntry*> entries;
        for (auto& item : table) entries.emplace(format2 ? uint32_t(item.id) : uint32_t(item.file_id), &item);
        for (auto index : payloads) {
            auto file = files.get(index);
            auto found = entries.end();
            if (format2) {
                found = std::find_if(entries.begin(), entries.end(), [&](auto& entry) { return overlayPath(cpu, entry.first) == file.name; });
            } else found = entries.find(file.original);
            require(found != entries.end(), "Replacement overlay table is missing a file ID");
            auto item = found->second;
            uint32_t size = changed ? files.get(byId[file.id]).data->size : file.data->size;
            uint32_t flags = item->reserved;
            if (((flags >> 24) & 1) || (cpu == 7 && flags > 0)) item->reserved = (flags & 0xff000000) | (size & 0xffffff);
            if (format2) item->file_id = file.id;
            else if (changed) for (auto& matching : table) if (matching.file_id == file.original) {
                uint32_t flags = matching.reserved;
                if (((flags >> 24) & 1) || (cpu == 7 && flags > 0)) matching.reserved = (flags & 0xff000000) | (size & 0xffffff);
            }
            overlayIds.insert(file.id);
        }
        if (changed && !format2) for (auto& item : table) {
            require(item.file_id < oldIds.size(), "Overlay file ID outside FAT");
            item.file_id = oldIds[item.file_id];
        }
        if (cpu == 9) { h.arm9_overlay_offset = output->size; h.arm9_overlay_size = table.size() * sizeof(OverlayEntry); }
        else { h.arm7_overlay_offset = output->size; h.arm7_overlay_size = table.size() * sizeof(OverlayEntry); }
        output->literal(table.data(), table.size() * sizeof(OverlayEntry));
        pad();
        for (auto index : payloads) {
            auto file = files.get(index);
            write(file.data, file.id, true);
        }
    };
    SourcePtr arm9 = system("arm9.bin");
    if (package.has("arm9.bin") || package.has("xdelta/arm9.bin")) {
        marker = 0;
        if (arm9->size >= 12) arm9->read(arm9->size - 12, &marker, sizeof(marker));
        nitrocode = marker == 0xdec00621;
    }
    if (work.dsOnly && h.unitcode == 2 && contains("overarm9.bin")) {
        auto table = format2 ? overlayTables[0] : readOverlays(system("overarm9.bin"));
        std::vector<SourcePtr> current;
        for (auto& entry : table) if (uint32_t(entry.reserved) & 0x2000000)
            current.push_back(system(overlayPath(9, entry.id)));
        arm9 = updateOverlayHashes(work, arm9, h, nitrocode, signedOverlays, current,
            package.has("arm9.bin") || package.has("xdelta/arm9.bin"));
    }
    if (work.dsOnly && h.unitcode == 2) {
        patchRequire(arm9->size >= 0x4000 + (nitrocode ? 12 : 0), "ARM9 secure area is truncated");
        auto secure = readBytes(arm9, 0, 0x4000);
        std::memcpy(&secureMarker, secure.data(), sizeof(secureMarker));
        bool plain = secureMarker == UINT64_C(0xe7ffdeffe7ffdeff);
        uint32_t previous, current;
        input->read(12, &previous, sizeof(previous));
        std::memcpy(&current, h.gamecode, sizeof(current));
        if (!plain) decrypt_arm9(previous, secure.data());
        unsigned_short crc = CalcCrc16(secure.data() + 0x10, 0x7f0);
        std::memcpy(secure.data() + 0xe, &crc, sizeof(crc));
        if (!plain) encrypt_arm9(current, secure.data());
        arm9 = overwrite(work, arm9, 0, secure.data(), secure.size());
    }
    h.arm9_rom_offset = output->size;
    h.arm9_size = arm9->size - (nitrocode ? 12 : 0);
    write(arm9, 0xffff, true);
    h.arm9_overlay_offset = 0;
    overlays(9);
    h.arm7_rom_offset = output->size;
    h.arm7_size = system("arm7.bin")->size;
    write(system("arm7.bin"), 0xffff, true);
    h.arm7_overlay_offset = 0;
    overlays(7);
    h.fnt_offset = output->size;
    h.fnt_size = fnt->size;
    write(fnt, 0xffff, true);
    h.fat_offset = output->size;
    h.fat_size = placements.size() * sizeof(Allocation);
    output->append(Kind::Fill, 0, h.fat_size);
    pad();
    h.banner_offset = output->size;
    SourcePtr banner = system("banner.bin");
    auto encoded = bannerBytes(banner, package.has("banner.bin") || package.has("xdelta/banner.bin") ? banner->size : h.unitcode & 2 ? uint32_t(h.banner_size) : 0);
    if (h.unitcode & 2) h.banner_size = encoded.size();
    output->literal(encoded.data(), encoded.size());
    pad();
    for (uint32_t index = 0; index < sorted.size(); index++) {
        uint32_t id = sorted[index];
        if (overlayIds.count(id)) continue;
        auto named = byId[id];
        if (named != UINT32_MAX) write(files.get(named).data, id, index + 1 < sorted.size());
        else if (allocations[id].end > allocations[id].start) {
            write(std::make_shared<Slice>(input, allocations[id].start, uint32_t(allocations[id].end) - uint32_t(allocations[id].start)), id, index + 1 < sorted.size());
        }
    }
    output->finish();
    output = overwrite(work, output, h.fat_offset, placements.data(), placements.size() * sizeof(Allocation));
    secureMarker = 0;
    if (output->size >= 0x4008) output->read(0x4000, &secureMarker, sizeof(secureMarker));
    decrypted = secureMarker == UINT64_C(0xe7ffdeffe7ffdeff);
    if (!work.dsOnly && (h.unitcode & 2) && (h.dsi_flags & 1) && h.tid_high && h.tid_high != 0xffffffff) output = writeTwl(work, output, input, h, decrypted);
    h.application_end_offset = output->size;
    uint32_t capacity = 0x20000;
    while (capacity < output->size + signature.size()) capacity *= 2;
    h.devicecap = 0;
    for (uint32_t value = capacity; value > 0x20000; value /= 2) h.devicecap++;
    auto secure = readBytes(output, 0x4000, std::min<uint32_t>(0x4000, output->size - 0x4000));
    secureArea(secure, h, decrypted);
    h.secure_area_crc = CalcCrc16(secure.data(), secure.size());
    std::memcpy(headerData.data(), &h, std::min<uint32_t>(headerSize, sizeof(h)));
    auto finalHeader = reinterpret_cast<Header*>(headerData.data());
    finalHeader->logo_crc = CalcCrc16(headerData.data() + 0xc0, 156);
    finalHeader->header_crc = CalcCrc16(headerData.data(), 0x15e);
    output = overwrite(work, output, 0, headerData.data(), headerData.size());
    output->literal(signature.data(), signature.size());
    output->append(Kind::Fill, 255, capacity - output->size);
    output->finish();
    return output;
}

std::shared_ptr<Map> buildRom(Workspace& work, SourcePtr input, Package& package) {
    try {
        return buildRomInput(work, input, package);
    } catch (const PatchError& error) {
        work.error = error.message;
    } catch (const std::bad_alloc&) {
        work.error = "Not enough memory to prepare this patch";
    }
    return nullptr;
}

}
