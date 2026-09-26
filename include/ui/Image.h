#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kestrel::ui {

struct ImageRef {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    bool valid = false;
};

bool decodeImage(const std::string& encoded, uint32_t& width, uint32_t& height, std::vector<uint8_t>& outRgba);
bool decodeSquareImage(const std::string& encoded, uint32_t size, std::vector<uint8_t>& outRgba);

}
