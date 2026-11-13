#pragma once
#include "nds_adapter.h"

namespace np3d {

std::shared_ptr<Map> overwrite(Workspace& work, SourcePtr input, uint32_t offset, const void* data, uint32_t length);
std::shared_ptr<Map> writeTwl(Workspace& work, std::shared_ptr<Map> output, SourcePtr original, Header& header,
                              bool decrypted);
void secureArea(std::vector<uint8_t>& data, const Header& header, bool decrypted);

}
