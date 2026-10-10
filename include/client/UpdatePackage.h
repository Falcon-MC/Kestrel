#pragma once

#include <string>
#include <string_view>

namespace kestrel {

bool newerRelease(std::string_view tag, std::string_view current);
std::string updateBinary(std::string_view archive, std::string_view path, bool zip);

}
