#include "core.h"
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
    using namespace np3d;
    require(argc == 5, "Usage: np3d-prepare ORIGINAL.nds PATCH.(xzp|zip|xdelta) SD_DIRECTORY OUTPUT.nds");
    std::string root = std::string(argv[3]) + "/nitro-patcher";
    makeDirectory(argv[3]);
    makeDirectory(root);
    makeDirectory(root + "/patches");
    std::string input = argv[2];
    std::string patch = root + "/patches/" + input.substr(input.find_last_of("/\\") + 1);
    if (input != patch)
        copyFile(input, patch);
    beginPreparationLog(root, patch);
    auto card = std::make_shared<Disk>(argv[1]);
    Prepared prepared = prepare(card, patch, root);
    finishPreparationLog(prepared.reused, prepared.error);
    if (!prepared.error.empty()) {
        std::fprintf(stderr, "%s\n", prepared.error.c_str());
        return EXIT_FAILURE;
    }
    Disk output(argv[4], "wb");
    std::vector<uint8_t> buffer(BufferSize);
    for (uint32_t offset = 0; offset < prepared.rom->size;) {
        uint32_t take = std::min(BufferSize, prepared.rom->size - offset);
        prepared.rom->read(offset, buffer.data(), take);
        output.write(offset, buffer.data(), take);
        offset += take;
    }
    output.sync();
    uint32_t counts[3] = {};
    for (uint32_t number = 0; number < prepared.rom->count; number++) {
        Extent item = prepared.rom->at(number);
        counts[uint32_t(item.kind)] += item.length;
    }
    std::printf("%s\n%s\nROM %u bytes; CARD %u; PATCH %u; FILL %u\n",
                prepared.reused ? "Cache reused" : "Cache prepared", prepared.directory.c_str(), prepared.rom->size,
                counts[0], counts[1], counts[2]);
    return 0;
}
