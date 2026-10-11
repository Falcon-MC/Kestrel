#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kestrel::util {

struct ZipEntry {
    std::string path;
    std::string data;
};

/**
 * Every file of a ZIP archive, stored or deflated. Fails on encrypted
 * entries, on paths that would leave the folder they are extracted to, and
 * once the files would expand past maxExpanded bytes, since archives come
 * from anywhere.
 */
bool readZip(const std::string& archive, uint64_t maxExpanded, std::vector<ZipEntry>& entries, std::string& error);

/**
 * A deflated ZIP archive of the entries, or empty when compression fails.
 */
std::string writeZip(const std::vector<ZipEntry>& entries);

}
