#include "filename.h"
#include "core.h"
#include "uni_algo/conv.h"
#include <iconv.h>

namespace np3d {

std::string decodeName(const std::string& bytes) {
    auto text = una::utf8to32u(convert(bytes, "CP932"));
    for (auto& value : text) {
        switch (value) {
        case 0x301c:
            value = 0xff5e;
            break;
        case 0x2016:
            value = 0x2225;
            break;
        case 0x2212:
            value = 0xff0d;
            break;
        case 0x00a2:
            value = 0xffe0;
            break;
        case 0x00a3:
            value = 0xffe1;
            break;
        case 0x00ac:
            value = 0xffe2;
            break;
        }
    }
    return una::utf32to8(text);
}

std::string encodeName(const std::string& name) {
    una::error error;
    auto text = una::strict::utf8to32u(name, error);
    patchRequire(!error, "FNT name is not UTF-8");
    iconv_t converter = iconv_open("CP932", "UTF-32LE");
    require(converter != (iconv_t)-1, "Cannot initialize FNT name conversion");
    std::string result;
    for (auto value : text) {
        char bytes[4];
        char* input = reinterpret_cast<char*>(&value);
        char* output = bytes;
        size_t inputSize = sizeof(value), outputSize = sizeof(bytes);
        bool encoded = iconv(converter, &input, &inputSize, &output, &outputSize) != size_t(-1) && inputSize == 0;
        if (!encoded) {
            require(iconv_close(converter) == 0, "Cannot close FNT name converter");
            patchRequire(false, "FNT name cannot be represented in Shift_JIS: " + name);
        }
        // iconv-lite selects these canonical bytes for duplicate CP932 mappings.
        uint16_t canonical = 0;
        if (value >= 0x2160 && value <= 0x2169)
            canonical = 0x8754 + value - 0x2160;
        else if (value == 0x2116)
            canonical = 0x8782;
        else if (value == 0x2121)
            canonical = 0x8784;
        else if (value == 0x3231)
            canonical = 0x878a;
        else if (value == 0xffe2)
            canonical = 0x81ca;
        if (canonical) {
            result += char(canonical >> 8);
            result += char(canonical);
        } else
            result.append(bytes, sizeof(bytes) - outputSize);
    }
    require(iconv_close(converter) == 0, "Cannot close FNT name converter");
    patchRequire(decodeName(result) == name, "FNT name cannot be represented in Shift_JIS: " + name);
    patchRequire(!result.empty() && result.size() <= 127, "FNT name must contain 1 to 127 encoded bytes");
    return result;
}

}
