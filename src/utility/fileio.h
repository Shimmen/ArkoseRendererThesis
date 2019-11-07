#pragma once

#include "types.h"
#include <optional>
#include <vector>

namespace fileio {

using BinaryData = std::vector<char>;
std::optional<BinaryData> loadEntireFileAsByteBuffer(const std::string& filePath);

bool isFileReadable(const std::string& filePath);

}
