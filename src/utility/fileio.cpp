#include "fileio.h"

#include <fstream>

std::optional<fileio::BinaryData> fileio::loadEntireFileAsByteBuffer(const std::string& filename)
{
    // Open file as binary and immediately seek to the end
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open())
        return {};

    size_t sizeInBytes = file.tellg();
    fileio::BinaryData binaryData(sizeInBytes);

    file.seekg(0);
    file.read(binaryData.data(), sizeInBytes);

    file.close();
    return binaryData;
}
