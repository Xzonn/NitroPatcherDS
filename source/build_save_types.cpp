#include <cstdint>
#include <cstdio>
using u32 = uint32_t;
#include "../vendor/TWiLightMenu/universal/include/ROMList.h"

int main(int argc, char** argv) {
    if (argc != 2)
        return 1;
    FILE* output = std::fopen(argv[1], "wb");
    if (!output)
        return 1;
    static_assert(sizeof(ROMListEntry) == 8);
    bool written = std::fwrite(ROMList, sizeof(ROMList), 1, output) == 1;
    return std::fclose(output) == 0 && written ? 0 : 1;
}
