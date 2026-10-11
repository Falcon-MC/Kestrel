#include "client/UpdatePackage.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <zlib.h>

namespace {

void check(bool value, const char* description)
{
    if (!value) { std::fprintf(stderr, "FAIL %s\n", description); std::exit(1); }
}

void number(std::string& data, size_t at, unsigned value, size_t width)
{
    for (size_t i = 0; i < width; ++i) data[at + i] = static_cast<char>(value >> (8 * i));
}

std::string zipped(const std::string& name, const std::string& binary)
{
    std::string local(30, '\0');
    number(local, 0, 0x04034b50, 4);
    number(local, 18, binary.size(), 4);
    number(local, 22, binary.size(), 4);
    number(local, 26, name.size(), 2);
    local += name + binary;
    std::string central(46, '\0');
    number(central, 0, 0x02014b50, 4);
    number(central, 16, crc32(0, reinterpret_cast<const Bytef*>(binary.data()), binary.size()), 4);
    number(central, 20, binary.size(), 4);
    number(central, 24, binary.size(), 4);
    number(central, 28, name.size(), 2);
    return local + central + name;
}

std::string gzip(const std::string& input)
{
    z_stream stream {};
    check(deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY) == Z_OK, "gzip init");
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    stream.avail_in = input.size();
    std::string output(compressBound(input.size()) + 64, '\0');
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = output.size();
    check(deflate(&stream, Z_FINISH) == Z_STREAM_END, "gzip finish");
    output.resize(stream.total_out);
    deflateEnd(&stream);
    return output;
}

std::string tarred(const std::string& name, const std::string& binary, char type = '0')
{
    std::string tar(512, '\0');
    tar.replace(0, name.size(), name);
    char size[12];
    std::snprintf(size, sizeof(size), "%011o", static_cast<unsigned>(binary.size()));
    tar.replace(124, 11, size);
    tar[156] = type;
    tar += binary;
    tar.resize(((tar.size() + 511) / 512) * 512 + 1024, '\0');
    return gzip(tar);
}

}

int main()
{
    using kestrel::newerRelease;
    using kestrel::updateBinary;
    check(newerRelease("v1.0.2+1.26.52", "1.0.1"), "patch upgrade");
    check(newerRelease("v1.10.0+1.26.52", "1.9.9"), "numeric comparison");
    check(!newerRelease("v1.0.0+1.26.51", "1.0.1"), "no downgrade");
    check(!newerRelease("v1.0.1+1.26.52", "1.0.1"), "no same version update");
    check(!newerRelease("v1.0.2-rc.1", "1.0.1"), "no prerelease");
    check(!newerRelease("v99999999999999999.0.0", "1.0.1"), "version overflow");
    check(!newerRelease("v1.0", "1.0.1"), "incomplete version");
    std::string path = "Kestrel-1.0.2+1.26.52-windows-x64/Kestrel.exe";
    std::string binary("MZ\0fake executable", 18);
    std::string zip = zipped(path, binary);
    check(updateBinary(zip, path, true) == binary, "zip extraction");
    check(updateBinary(zip, "other/Kestrel.exe", true).empty(), "exact package path");
    std::string damaged = zip;
    damaged[30 + path.size()] ^= 1;
    check(updateBinary(damaged, path, true).empty(), "zip checksum rejection");
    check(updateBinary(zip.substr(0, zip.size() - 10), path, true).empty(), "truncated zip");
    size_t central = 30 + path.size() + binary.size();
    damaged = zip;
    number(damaged, central + 42, 0xfffffff0, 4);
    check(updateBinary(damaged, path, true).empty(), "zip offset overflow");
    damaged = zip;
    number(damaged, central + 24, 0xffffffff, 4);
    check(updateBinary(damaged, path, true).empty(), "zip expansion bound");
    path = "Kestrel-1.0.2+1.26.52-linux-x64/Kestrel";
    std::string tar = tarred(path, binary);
    check(updateBinary(tar, path, false) == binary, "tar gzip extraction");
    check(updateBinary(tarred(path, binary, '2'), path, false).empty(), "tar symlink rejection");
    check(updateBinary(tar.substr(0, tar.size() - 4), path, false).empty(), "truncated gzip");
    check(updateBinary(tar, "../Kestrel", false).empty(), "no arbitrary path extraction");
    return 0;
}
