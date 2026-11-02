#pragma once
#include "core.h"

namespace np3d {

struct CheatIdentity {
    uint32_t gameCode = 0, crc32 = 0;
    bool operator==(const CheatIdentity& other) const { return gameCode == other.gameCode && crc32 == other.crc32; }
};
struct CheatSelection {
    CheatIdentity identity;
    std::vector<std::string> enabled;
};
enum class CheatStatus { Ready, MissingDatabase, NotFound, SelectionChanged, TooLarge, InvalidDatabase };
CheatIdentity cheatIdentity(SourcePtr source);

struct CheatData;
class Cheats {
public:
    Cheats();
    ~Cheats();
    CheatStatus load(const std::string& database, SourcePtr rom, SourcePtr card);
    const std::string& gameTitle() const;
    const std::string& title(size_t index) const;
    const std::string& description(size_t index) const;
    bool folder(size_t index) const;
    bool selected(size_t index) const;
    void select(size_t index, bool enabled);
    std::vector<size_t> children(int folder = -1) const;
    bool restore(const CheatSelection& selection);
    CheatSelection selection() const;
    uint32_t byteSize() const;
    void write(const std::string& path);
    bool patched = false;
    CheatIdentity identity;
private:
    std::unique_ptr<CheatData> data;
    std::string key(size_t index) const;
};

CheatStatus writeCheats(const std::string& root, SourcePtr rom, SourcePtr card, const CheatSelection& selection, uint32_t limit);

}
