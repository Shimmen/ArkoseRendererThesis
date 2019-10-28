#pragma once

#include "common.h"

namespace fileio {

using BinaryData = std::vector<char>;
std::optional<BinaryData> loadEntireFileAsByteBuffer(const std::string& filename);

}
