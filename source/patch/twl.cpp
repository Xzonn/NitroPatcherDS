#include "rom.h"
#include "twl.h"
#include "mbedtls/aes.h"
#include "mbedtls/md.h"
#include <array>
#include <cstring>

namespace np3d {

static const uint8_t hmacKey[64] = {0x21, 0x06, 0xc0, 0xde, 0xba, 0x98, 0xce, 0x3f, 0xa6, 0x92, 0xe3, 0x9d, 0x46,
                                    0xf2, 0xed, 0x01, 0x76, 0xe3, 0xcc, 0x08, 0x56, 0x23, 0x63, 0xfa, 0xca, 0xd4,
                                    0xec, 0xdf, 0x9a, 0x62, 0x78, 0x34, 0x8f, 0x6d, 0x63, 0x3c, 0xfe, 0x22, 0xca,
                                    0x92, 0x20, 0x88, 0x97, 0x23, 0xd2, 0xcf, 0xae, 0xc2, 0x32, 0x67, 0x8d, 0xfe,
                                    0xca, 0x83, 0x64, 0x98, 0xac, 0xfd, 0x3e, 0x37, 0x87, 0x46, 0x58, 0x24};

struct TwlHasher::State {
    mbedtls_md_context_t context;
    std::vector<uint8_t> buffer;
    const char* stage;
};

TwlHasher::TwlHasher(const char* stage) : state(std::make_unique<State>()) {
    state->stage = stage;
    mbedtls_md_init(&state->context);
    require(mbedtls_md_setup(&state->context, mbedtls_md_info_from_type(MBEDTLS_MD_SHA1), 1) == 0 &&
                mbedtls_md_hmac_starts(&state->context, hmacKey, sizeof(hmacKey)) == 0,
            "Cannot initialize TWL HMAC");
}

TwlHasher::~TwlHasher() { mbedtls_md_free(&state->context); }

std::array<uint8_t, 20> TwlHasher::operator()(SourcePtr input, uint32_t offset, uint32_t length, const char* detail) {
    auto& context = state->context;
    auto& buffer = state->buffer;
    buffer.resize(std::min(BufferSize, std::max<uint32_t>(length, 1)));
    require(mbedtls_md_hmac_reset(&context) == 0, "Cannot reset TWL HMAC");
    if (detail)
        progress(state->stage, 0, length, true, detail);
    for (uint32_t position = 0; position < length;) {
        uint32_t take = std::min<uint32_t>(buffer.size(), length - position);
        input->read(offset + position, buffer.data(), take);
        require(mbedtls_md_hmac_update(&context, buffer.data(), take) == 0, "Cannot update TWL HMAC");
        position += take;
        if (detail)
            progress(state->stage, position, length, true, detail);
    }
    std::array<uint8_t, 20> result;
    require(mbedtls_md_hmac_finish(&context, result.data()) == 0, "Cannot finish TWL HMAC");
    return result;
}

static std::array<uint8_t, 20> hash(SourcePtr input, uint32_t offset, uint32_t length) {
    return TwlHasher()(input, offset, length);
}

static std::array<uint8_t, 16> key(const Header& h) {
    std::array<uint8_t, 16> result = {};
    if ((h.dsi_flags & 4) || (h.appflags & 0x80)) {
        std::memcpy(result.data(), h.title, 12);
        std::memcpy(result.data() + 12, h.gamecode, 4);
        return result;
    }
    std::memcpy(result.data(), "Nintendo", 8);
    for (unsigned int index = 0; index < 4; index++) {
        result[8 + index] = h.gamecode[index];
        result[12 + index] = h.gamecode[3 - index];
    }
    const uint8_t add[16] = {0x79, 0x3e, 0x4f, 0x1a, 0x5f, 0x0f, 0x68, 0x2a,
                             0x58, 0x02, 0x59, 0x29, 0x4e, 0xfb, 0xfe, 0xff};
    unsigned int carry = 0;
    for (unsigned int index = 0; index < 16; index++) {
        unsigned int sum = (result[index] ^ h.hmac_arm9i[index]) + add[index] + carry;
        result[index] = sum;
        carry = sum >> 8;
    }
    auto original = result;
    result.fill(0);
    for (unsigned int bit = 0; bit < 128; bit++) {
        unsigned int target = (bit + 42) % 128;
        result[target / 8] |= ((original[bit / 8] >> (bit % 8)) & 1) << (target % 8);
    }
    return result;
}

static SourcePtr crypt(Workspace& work, SourcePtr input, uint32_t offset, uint32_t length, const Header& h,
                       const uint8_t* counter) {
    require(offset <= input->size && length <= input->size - offset, "Modcrypt range outside DSi program");
    auto output = work.map();
    output->copy(input, 0, offset);
    auto cipherKey = key(h);
    std::reverse(cipherKey.begin(), cipherKey.end());
    std::array<uint8_t, 16> ctr;
    std::reverse_copy(counter, counter + 16, ctr.begin());
    mbedtls_aes_context cipher;
    mbedtls_aes_init(&cipher);
    require(mbedtls_aes_setkey_enc(&cipher, cipherKey.data(), 128) == 0, "Cannot initialize modcrypt");
    std::vector<uint8_t> buffer(BufferSize);
    for (uint32_t position = 0; position < length;) {
        uint32_t take = std::min(BufferSize, length - position);
        input->read(offset + position, buffer.data(), take);
        for (uint32_t block = 0; block < take; block += 16) {
            uint8_t mask[16];
            require(mbedtls_aes_crypt_ecb(&cipher, MBEDTLS_AES_ENCRYPT, ctr.data(), mask) == 0,
                    "Cannot apply modcrypt");
            for (uint32_t byte = 0; byte < std::min<uint32_t>(16, take - block); byte++)
                buffer[block + byte] ^= mask[15 - byte];
            for (int byte = 15; byte >= 0; byte--)
                if (++ctr[byte])
                    break;
        }
        output->literal(buffer.data(), take);
        position += take;
    }
    output->copy(input, offset + length, input->size - offset - length);
    output->finish();
    mbedtls_aes_free(&cipher);
    return output;
}

static std::shared_ptr<Map> splice(Workspace& work, SourcePtr input, uint32_t offset, SourcePtr replacement) {
    require(offset <= input->size && replacement->size <= input->size - offset, "TWL section outside ROM");
    auto output = work.map();
    output->copy(input, 0, offset);
    output->copy(replacement, 0, replacement->size);
    output->copy(input, offset + replacement->size, input->size - offset - replacement->size);
    output->finish();
    return output;
}

SourcePtr twlCrypt(Workspace& work, SourcePtr input, uint32_t offset, uint32_t length, const Header& header,
                   const uint8_t* counter) {
    return crypt(work, input, offset, length, header, counter);
}

std::shared_ptr<Map> writeTwl(Workspace& work, std::shared_ptr<Map> output, SourcePtr original, Header& h,
                              bool decrypted) {
    progress("Preparing DSi files", 0, 0);
    auto sourceHeaderStorage = std::make_unique<Header>();
    Header& sourceHeader = *sourceHeaderStorage;
    original->read(0, &sourceHeader, sizeof(sourceHeader));
    uint32_t sector = h.digest_sector_size, blockSectors = h.digest_block_sectorcount;
    require(sector && sector <= 0x4000 && 0x4000 % sector == 0 && blockSectors && blockSectors <= 0x100000,
            "Invalid TWL digest geometry");
    uint32_t ntrSize = h.digest_ntr_size, twlSize = h.digest_twl_size;
    require(ntrSize >= 0x4000 && ntrSize % sector == 0 && twlSize % sector == 0, "Invalid TWL digest region size");
    uint32_t sectorStart = h.sector_hashtable_start, sectorSize = h.sector_hashtable_size;
    uint32_t blockStart = h.block_hashtable_start, blockSize = h.block_hashtable_size;
    require((uint64_t(ntrSize) + twlSize) / sector * 20 <= sectorSize &&
                ((uint64_t(sectorSize) + blockSectors * 20 - 1) / (blockSectors * 20)) * 20 <= blockSize,
            "TWL digest tables are too small");
    uint32_t dsi9Start = h.dsi9_rom_offset, dsi7Start = h.dsi7_rom_offset;
    uint32_t dsi9Size = std::max(uint32_t(h.dsi9_size), uint32_t(h.modcrypt1_size));
    uint32_t dsi7Size = std::max(uint32_t(h.dsi7_size), uint32_t(h.modcrypt2_size));
    require(output->size <= sectorStart && uint64_t(sectorStart) + sectorSize <= blockStart &&
                uint64_t(blockStart) + blockSize <= dsi9Start && uint64_t(dsi9Start) + dsi9Size <= dsi7Start &&
                uint64_t(dsi7Start) + dsi7Size <= uint32_t(h.total_rom_size),
            "DSi sections overlap");
    SourcePtr programs[2] = {std::make_shared<Slice>(original, dsi9Start, dsi9Size),
                             std::make_shared<Slice>(original, dsi7Start, dsi7Size)};
    bool encrypted = false;
    for (unsigned int index = 0; index < 2; index++) {
        uint32_t start = index == 0 ? uint32_t(h.modcrypt1_start) : uint32_t(h.modcrypt2_start);
        uint32_t size = index == 0 ? uint32_t(h.modcrypt1_size) : uint32_t(h.modcrypt2_size);
        if (size && start >= uint32_t(h.digest_twl_start) && start < 0xffffffff) {
            uint32_t number = (start - uint32_t(h.digest_twl_start)) / sector;
            auto actual = hash(original, uint32_t(h.digest_twl_start) + number * sector, sector);
            auto expected = readBytes(original, sectorStart + (ntrSize / sector + number) * 20, 20);
            encrypted = std::memcmp(actual.data(), expected.data(), 20) != 0;
            break;
        }
    }
    if (encrypted) {
        if (h.modcrypt1_size)
            programs[0] = crypt(work, programs[0], uint32_t(h.modcrypt1_start) - dsi9Start, h.modcrypt1_size,
                                sourceHeader, sourceHeader.hmac_arm9);
        if (h.modcrypt2_size)
            programs[1] = crypt(work, programs[1], uint32_t(h.modcrypt2_start) - dsi7Start, h.modcrypt2_size,
                                sourceHeader, sourceHeader.hmac_arm7);
    }
    auto fill = [&](uint32_t end) {
        require(end >= output->size, "DSi section precedes NTR output");
        output->append(Kind::Fill, 255, end - output->size);
    };
    fill(sectorStart);
    output->append(Kind::Fill, 0, sectorSize);
    fill(blockStart);
    output->append(Kind::Fill, 0, blockSize);
    uint32_t total = h.total_rom_size;
    uint32_t capacity = h.devicecap < 14 ? 1u << (17 + h.devicecap) : 0;
    bool trimmed = total ? total >= original->size : original->size != capacity;
    if (!trimmed) {
        uint32_t copies = uint32_t(h.digest_twl_start) - 0x3000;
        fill(copies);
        output->copy(original, copies, 0x3000);
    }
    fill(dsi9Start);
    output->copy(programs[0], 0, programs[0]->size);
    fill(dsi7Start);
    output->copy(programs[1], 0, programs[1]->size);
    fill(total);
    output->finish();
    auto secure = readBytes(output, h.digest_ntr_start, 0x4000);
    secureArea(secure, h, decrypted);
    auto encryptedSecure = work.map();
    encryptedSecure->literal(secure.data(), secure.size());
    encryptedSecure->finish();
    auto sectors = work.map();
    std::vector<uint8_t> hashes;
    hashes.reserve(BufferSize);
    auto put = [&](const std::array<uint8_t, 20>& value) {
        hashes.insert(hashes.end(), value.begin(), value.end());
        if (hashes.size() >= BufferSize - 20) {
            sectors->literal(hashes.data(), hashes.size());
            hashes.clear();
        }
    };
    progress("Checking game data", 0, ntrSize + twlSize, true);
    for (uint32_t position = 0; position < 0x4000; position += sector) {
        put(hash(encryptedSecure, position, sector));
        progress("Checking game data", position + sector, ntrSize + twlSize, true);
    }
    for (uint32_t position = 0x4000; position < ntrSize; position += sector) {
        put(hash(output, uint32_t(h.digest_ntr_start) + position, sector));
        progress("Checking game data", position + sector, ntrSize + twlSize, true);
    }
    for (uint32_t position = 0; position < twlSize; position += sector) {
        put(hash(output, uint32_t(h.digest_twl_start) + position, sector));
        progress("Checking game data", ntrSize + position + sector, ntrSize + twlSize, true);
    }
    sectors->literal(hashes.data(), hashes.size());
    sectors->append(Kind::Fill, 0, sectorSize - sectors->size);
    sectors->finish();
    output = splice(work, output, sectorStart, sectors);
    auto blocks = work.map();
    hashes.clear();
    for (uint32_t position = 0; position < sectorSize; position += blockSectors * 20) {
        auto value = hash(output, sectorStart + position, blockSectors * 20);
        hashes.insert(hashes.end(), value.begin(), value.end());
        if (hashes.size() >= BufferSize - 20) {
            blocks->literal(hashes.data(), hashes.size());
            hashes.clear();
        }
    }
    blocks->literal(hashes.data(), hashes.size());
    blocks->append(Kind::Fill, 0, blockSize - blocks->size);
    blocks->finish();
    output = splice(work, output, blockStart, blocks);
    auto master = hash(output, blockStart, blockSize);
    std::memcpy(h.hmac_digest_master, master.data(), master.size());
    if (h.dsi_flags & 2) {
        for (unsigned int index = 0; index < 2; index++) {
            uint32_t start = index == 0 ? uint32_t(h.modcrypt1_start) : uint32_t(h.modcrypt2_start);
            uint32_t size = index == 0 ? uint32_t(h.modcrypt1_size) : uint32_t(h.modcrypt2_size);
            if (size) {
                auto bytes = crypt(work, std::make_shared<Slice>(output, start, size), 0, size, h,
                                   index == 0 ? h.hmac_arm9 : h.hmac_arm7);
                output = splice(work, output, start, bytes);
            }
        }
    }
    return output;
}

}
