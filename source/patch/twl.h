#pragma once
#include "rom.h"
#include <functional>

namespace np3d {

class TwlHasher {
    struct State;
    std::unique_ptr<State> state;
public:
    explicit TwlHasher(const char* stage = "Checking DSi data");
    ~TwlHasher();
    std::array<uint8_t, 20> operator()(SourcePtr input, uint32_t offset, uint32_t length, const char* detail = nullptr);
};
SourcePtr twlCrypt(Workspace& work, SourcePtr input, uint32_t offset, uint32_t length,
    const Header& header, const uint8_t* counter);
SourcePtr updateOverlayHashes(Workspace& work, SourcePtr arm9, Header& header, bool nitrocode,
    const std::vector<SourcePtr>& original, const std::vector<SourcePtr>& overlays, bool replaced);

struct TwlSection {
    uint32_t start, originalSize;
    SourcePtr data;
    std::function<void(uint32_t)> place;
    std::function<SourcePtr()> generate;
};

class CompleteTwl {
    struct Program {
        uint32_t start, length, originalLength;
        SourcePtr data;
        bool replaced = false;
    };
    struct Region { unsigned int id, program, kind; uint32_t offset, length; };
    Workspace& work;
    SourcePtr input;
    std::unique_ptr<Header> originalHeader;
    std::array<Program, 4> programs;
    std::vector<Region> regions;
    std::vector<SourcePtr> overlaySources;
    bool digests, prefix;
    void decryptPrograms();
public:
    CompleteTwl(Workspace& work, SourcePtr input, Header& header, const std::vector<SourcePtr>& overlays);
    SourcePtr program(unsigned int index);
    void setProgram(unsigned int index, SourcePtr data);
    SourcePtr updateOverlays(SourcePtr arm9, Header& header, bool nitrocode,
        const std::vector<SourcePtr>& overlays, bool replaced);
    std::shared_ptr<Map> serialize(Header& header, std::vector<uint8_t> headerData,
        std::vector<TwlSection>& sections, bool forceLayout, bool decrypted);
};

bool equalSources(SourcePtr first, SourcePtr second);

}
