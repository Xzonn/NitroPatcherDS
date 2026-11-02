#include "cheats.h"
#include "cheat.h"
#include "miniz.h"
#include <cstring>
#include <unistd.h>

namespace np3d {

struct CheatData {
    CheatCodelist list;
    std::vector<int> parents;
};

CheatIdentity cheatIdentity(SourcePtr source) {
    auto bytes = readBytes(source, 0, 512);
    CheatIdentity result;
    std::memcpy(&result.gameCode, bytes.data() + 12, 4);
    result.crc32 = ~uint32_t(mz_crc32(MZ_CRC32_INIT, bytes.data(), bytes.size()));
    return result;
}

Cheats::Cheats() : data(std::make_unique<CheatData>()) {}
Cheats::~Cheats() = default;

CheatStatus Cheats::load(const std::string& database, SourcePtr rom, SourcePtr card) try {
    data = std::make_unique<CheatData>();
    identity = cheatIdentity(rom);
    auto original = cheatIdentity(card);
    patched = !(identity == original);
    if (!exists(database)) return CheatStatus::MissingDatabase;
    Disk input(database);
    long offset;
    size_t length;
    if (!data->list.searchCheatData(input.handle, identity.gameCode, identity.crc32, offset, length)) {
        if (identity == original) return CheatStatus::NotFound;
        identity = original;
        patched = false;
        if (!data->list.searchCheatData(input.handle, identity.gameCode, identity.crc32, offset, length)) return CheatStatus::NotFound;
    }
    patchRequire(data->list.parseInternal(input.handle, identity.gameCode, identity.crc32), "Cannot read the game's cheat list");
    int parent = -1;
    const auto& items = data->list.items();
    for (size_t index = 0; index < items.size(); index++) {
        if (items[index]._flags & cCheatDatItem::EFolder) {
            data->parents.push_back(-1);
            parent = index;
        } else {
            data->parents.push_back((items[index]._flags & cCheatDatItem::EInFolder) ? parent : -1);
            data->list.setSelected(index, false);
        }
    }
    return CheatStatus::Ready;
} catch (const PatchError&) {
    data = std::make_unique<CheatData>();
    return CheatStatus::InvalidDatabase;
}

const std::string& Cheats::gameTitle() const { return data->list.gameTitle(); }
const std::string& Cheats::title(size_t index) const { return data->list.items().at(index)._title; }
const std::string& Cheats::description(size_t index) const { return data->list.items().at(index)._comment; }
bool Cheats::folder(size_t index) const { return data->list.items().at(index)._flags & cCheatDatItem::EFolder; }
bool Cheats::selected(size_t index) const { return data->list.items().at(index)._flags & cCheatDatItem::ESelected; }
void Cheats::select(size_t index, bool enabled) { data->list.setSelected(index, enabled); }

std::vector<size_t> Cheats::children(int folder) const {
    std::vector<size_t> result;
    for (size_t index = 0; index < data->parents.size(); index++) if (data->parents[index] == folder) result.push_back(index);
    return result;
}

std::string Cheats::key(size_t index) const {
    const auto& entry = data->list.items().at(index);
    int parent = data->parents.at(index);
    std::string result = (parent < 0 ? std::string() : title(parent)) + "\n" + entry._title + "\n";
    char word[9];
    for (uint32_t value : entry._cheat) {
        std::snprintf(word, sizeof(word), "%08lx", static_cast<unsigned long>(value));
        result += word;
    }
    return result;
}

bool Cheats::restore(const CheatSelection& selection) {
    for (size_t index = 0; index < data->parents.size(); index++) data->list.setSelected(index, false);
    if (selection.enabled.empty()) return true;
    if (!(selection.identity == identity)) return false;
    size_t found = 0;
    for (size_t index = 0; index < data->parents.size(); index++) {
        if (folder(index)) continue;
        std::string identifier = key(index);
        if (std::find(selection.enabled.begin(), selection.enabled.end(), identifier) != selection.enabled.end()) {
            data->list.setSelected(index, true);
            found++;
        }
    }
    if (found != selection.enabled.size()) {
        for (size_t index = 0; index < data->parents.size(); index++) data->list.setSelected(index, false);
        return false;
    }
    return true;
}

CheatSelection Cheats::selection() const {
    CheatSelection result;
    result.identity = identity;
    for (size_t index = 0; index < data->parents.size(); index++) if (selected(index)) result.enabled.push_back(key(index));
    return result;
}

uint32_t Cheats::byteSize() const {
    uint32_t bytes = 0;
    for (const auto& entry : data->list.items()) if (entry._flags & cCheatDatItem::ESelected) bytes += entry._cheat.size() * 4;
    return bytes ? bytes + 4 : 0;
}

void Cheats::write(const std::string& path) {
    auto words = data->list.getCheats();
    words.push_back(0xCF000000);
    Disk output(path, "wb");
    output.write(0, words.data(), words.size() * sizeof(uint32_t));
    output.sync();
}

CheatStatus writeCheats(const std::string& root, SourcePtr rom, SourcePtr card, const CheatSelection& selection, uint32_t limit) {
    std::string directory = root + "/runtime/bootstrap", path = directory + "/cheatData.bin";
    if (selection.enabled.empty()) {
        if (exists(path)) require(unlink(path.c_str()) == 0, "Cannot clear previous cheat codes");
        return CheatStatus::Ready;
    }
    Cheats cheats;
    CheatStatus status = cheats.load(root + "/cheats/usrcheat.dat", rom, card);
    if (status != CheatStatus::Ready) return status;
    if (!cheats.restore(selection)) return CheatStatus::SelectionChanged;
    if (cheats.byteSize() > limit) return CheatStatus::TooLarge;
    makeDirectory(root + "/runtime");
    makeDirectory(directory);
    cheats.write(path);
    return CheatStatus::Ready;
}

}
