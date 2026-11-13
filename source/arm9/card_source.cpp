#include "card_source.h"
#include <nds.h>
#include <cstring>

extern "C" {
#include "read_card.h"
}
extern void encrypt_arm9(uint32_t gameCode, unsigned char* data);

namespace {

struct GameCard final : np3d::Source {
    alignas(4) uint8_t prefix[0x8000] = {};
    alignas(4) uint8_t headerData[0x1000] = {};
    alignas(32) uint32_t block[1024];
    alignas(4) uint32_t identity[128];
    uint32_t blockOffset = UINT32_MAX;

    GameCard(uint32_t* chipId, std::string& error) {
        using namespace np3d;
        auto& header = *reinterpret_cast<sNDSHeaderExt*>(headerData);
        progress("Reading Game Card header", 0, 0x1000, true);
        sysSetCardOwner(BUS_OWNER_ARM9);
        uint32_t id = cardReadID(CARD_CLK_SLOW);
        uint32_t control = CARD_ACTIVATE | CARD_nRESET | CARD_CLK_SLOW | CARD_DELAY1(0x1fff) | CARD_DELAY2(0x3f);
        if (id & 0x80000000) {
            for (uint32_t offset = 0; offset < 0x1000; offset += 512) {
                cardParamCommand(CARD_CMD_HEADER_READ, offset, control | CARD_BLK_SIZE(1),
                                 reinterpret_cast<uint32_t*>(prefix + offset), 512 / sizeof(uint32_t));
            }
        } else {
            cardParamCommand(CARD_CMD_HEADER_READ, 0, control | CARD_BLK_SIZE(4), reinterpret_cast<uint32_t*>(prefix),
                             0x1000 / sizeof(uint32_t));
        }
        progress("Initializing Game Card", 0, 0);
        if (cardInit(&header, &id) != 0) {
            error = "Cannot initialize Game Card for patch preparation";
            return;
        }
        if (chipId)
            *chipId = id;
        if (header.deviceSize > 13) {
            error = "Game Card exceeds supported ROM size";
            return;
        }
        size = 0x20000u << header.deviceSize;
        progress("Reading secure area", 0, 0x4000, true);
        cardRead(0x4000, reinterpret_cast<uint32_t*>(prefix + 0x4000), 0x4000);
        progress("Checking secure area", 0, 0);
        std::vector<uint8_t> secure(prefix + 0x4000, prefix + 0x8000);
        uint32_t code;
        std::memcpy(&code, header.gameCode, sizeof(code));
        encrypt_arm9(code, secure.data());
        if (swiCRC16(0xffff, secure.data(), secure.size()) != header.secureCRC16) {
            error = "Game Card secure area checksum mismatch";
            return;
        }
        if (!isDSiMode())
            cardRead(0x8000, identity, sizeof(identity));
    }

    void read(uint32_t offset, void* output, uint32_t length) override {
        using namespace np3d;
        require(offset <= size && length <= size - offset, "Read outside Game Card");
        auto bytes = static_cast<uint8_t*>(output);
        if (offset < sizeof(prefix)) {
            uint32_t take = std::min<uint32_t>(length, sizeof(prefix) - offset);
            std::memcpy(bytes, prefix + offset, take);
            offset += take;
            bytes += take;
            length -= take;
        }
        while (length) {
            uint32_t base = offset & ~0xfffu;
            uint32_t take = std::min(length, 4096 - offset % 4096);
            auto& header = *reinterpret_cast<sNDSHeaderExt*>(headerData);
            require(header.unitCode == 0 || base <= header.romSize || isDSiMode(),
                    "Reading DSi cartridge regions requires DSi mode");
            uint32_t source = base;
            uint32_t twlStart = uint32_t(header.twl_rom_region_start) * 0x80000u;
            if (header.unitCode != 0 && twlStart != 0 && base >= twlStart && base - twlStart < 0x3000) {
                source = 0x8000 + (base - twlStart) % 0x1000;
            }
            if (offset == base && take == sizeof(block) && (reinterpret_cast<uintptr_t>(bytes) & 31) == 0) {
                cardRead(source, reinterpret_cast<uint32_t*>(bytes), take);
            } else {
                if (base != blockOffset) {
                    cardRead(source, block, sizeof(block));
                    blockOffset = base;
                }
                std::memcpy(bytes, reinterpret_cast<uint8_t*>(block) + offset % 4096, take);
            }
            bytes += take;
            offset += take;
            length -= take;
        }
    }
};

}

np3d::SourcePtr openGameCard(uint32_t* chipId, std::string& error) {
    error.clear();
    auto card = std::make_shared<GameCard>(chipId, error);
    return error.empty() ? card : nullptr;
}
bool gameCardMatches(np3d::SourcePtr card) {
    alignas(4) uint32_t current[128];
    cardRead(0x8000, current, sizeof(current));
    return std::memcmp(std::static_pointer_cast<GameCard>(card)->identity, current, sizeof(current)) == 0;
}
