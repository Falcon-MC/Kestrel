#include "audio/AudioDecoders.h"

#include "util/Bytes.h"

#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iterator>

namespace kestrel::audio {

namespace {

using util::readLe32;
using util::readLe64;
using util::readLittle;

constexpr uint32_t FsbHeaderSize = 0x3C;
constexpr uint32_t FsbLegacyHeaderSize = 0x40;
constexpr uint32_t FsbModePcm8 = 1;
constexpr uint32_t FsbModePcm16 = 2;
constexpr uint32_t FsbModeFadpcm = 16;
constexpr uint32_t FadpcmFrameSize = 0x8C;
constexpr uint32_t FadpcmFrameSamples = 256;

constexpr int16_t FadpcmCoefficients[8][2] = {
    { 0, 0 },
    { 60, 0 },
    { 122, 60 },
    { 115, 52 },
    { 98, 55 },
    { 0, 0 },
    { 0, 0 },
    { 0, 0 },
};

void decodeFadpcm(const uint8_t* frame, float* out, uint32_t stride)
{
    uint32_t coefficients = readLittle<uint32_t>(frame);
    uint32_t shifts = readLittle<uint32_t>(frame + 4);
    int32_t history1 = readLittle<int16_t>(frame + 8);
    int32_t history2 = readLittle<int16_t>(frame + 10);
    uint32_t written = 0;
    for (int group = 0; group < 8; ++group) {
        int index = static_cast<int>((coefficients >> (group * 4)) & 0x0F) % 0x07;
        int shift = 0x16 - static_cast<int>((shifts >> (group * 4)) & 0x0F);
        int32_t coefficient1 = FadpcmCoefficients[index][0];
        int32_t coefficient2 = FadpcmCoefficients[index][1];
        for (int word = 0; word < 4; ++word) {
            uint32_t nibbles = readLittle<uint32_t>(frame + 0x0C + 0x10 * group + 0x04 * word);
            for (int nibble = 0; nibble < 8; ++nibble) {
                int32_t sample = static_cast<int32_t>(((nibbles >> (nibble * 4)) & 0x0F) << 28) >> shift;
                sample = (sample - history2 * coefficient2 + history1 * coefficient1) >> 6;
                sample = std::clamp(sample, -32768, 32767);
                out[written * stride] = static_cast<float>(sample) / 32768.0f;
                ++written;
                history2 = history1;
                history1 = sample;
            }
        }
    }
}

}

bool decodeFsb(const std::string& data, PcmBuffer& out)
{
    if (data.size() < FsbLegacyHeaderSize || data.compare(0, 4, "FSB5") != 0) {
        return false;
    }
    uint32_t version = readLe32(data, 4);
    uint32_t sampleCount = readLe32(data, 8);
    uint32_t sampleHeaders = readLe32(data, 12);
    uint32_t nameTable = readLe32(data, 16);
    uint32_t dataSize = readLe32(data, 20);
    uint32_t mode = readLe32(data, 24);
    uint32_t headerSize = version == 0 ? FsbLegacyHeaderSize : FsbHeaderSize;
    if (sampleCount == 0 || data.size() < size_t(headerSize) + sampleHeaders) {
        return false;
    }

    size_t cursor = headerSize;
    uint64_t raw = readLe64(data, cursor);
    cursor += 8;
    static constexpr uint32_t Frequencies[] = { 4000, 8000, 11000, 11025, 16000, 22050, 24000, 32000, 44100, 48000, 96000 };
    static constexpr uint32_t ChannelCounts[] = { 1, 2, 6, 8 };
    uint32_t frequencyIndex = static_cast<uint32_t>((raw >> 1) & 0x0F);
    out.sampleRate = frequencyIndex < std::size(Frequencies) ? Frequencies[frequencyIndex] : 44100;
    out.channels = ChannelCounts[(raw >> 5) & 0x03];
    uint64_t sampleOffset = ((raw >> 7) & 0x07FFFFFF) * 32;
    uint64_t frames = (raw >> 34) & 0x3FFFFFFF;
    bool more = (raw & 1) != 0;
    while (more && cursor + 4 <= data.size()) {
        uint32_t chunk = readLe32(data, cursor);
        cursor += 4;
        more = (chunk & 1) != 0;
        uint32_t size = (chunk >> 1) & 0x00FFFFFF;
        uint32_t type = (chunk >> 25) & 0x7F;
        if (cursor + size > data.size()) {
            return false;
        }
        if (type == 1 && size >= 1) {
            out.channels = static_cast<uint8_t>(data[cursor]);
        } else if (type == 2 && size >= 4) {
            out.sampleRate = readLe32(data, cursor);
        }
        cursor += size;
    }

    size_t start = size_t(headerSize) + sampleHeaders + nameTable + sampleOffset;
    size_t end = std::min(data.size(), size_t(headerSize) + sampleHeaders + nameTable + dataSize);
    if (start >= end || out.channels == 0) {
        return false;
    }
    if (sampleCount > 1) {
        uint64_t nextRaw = readLe64(data, cursor);
        size_t nextOffset = size_t(headerSize) + sampleHeaders + nameTable + ((nextRaw >> 7) & 0x07FFFFFF) * 32;
        if (nextOffset > start && nextOffset < end) {
            end = nextOffset;
        }
    }
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data()) + start;
    size_t length = end - start;

    if (mode == FsbModeFadpcm) {
        size_t frameCount = length / (size_t(FadpcmFrameSize) * out.channels);
        out.samples.assign(frameCount * FadpcmFrameSamples * out.channels, 0.0f);
        for (size_t frame = 0; frame < frameCount; ++frame) {
            for (uint32_t channel = 0; channel < out.channels; ++channel) {
                const uint8_t* source = bytes + (frame * out.channels + channel) * FadpcmFrameSize;
                decodeFadpcm(source, out.samples.data() + frame * FadpcmFrameSamples * out.channels + channel, out.channels);
            }
        }
    } else if (mode == FsbModePcm16) {
        size_t count = length / 2;
        out.samples.resize(count);
        for (size_t i = 0; i < count; ++i) {
            out.samples[i] = static_cast<float>(readLittle<int16_t>(bytes + i * 2)) / 32768.0f;
        }
    } else if (mode == FsbModePcm8) {
        out.samples.resize(length);
        for (size_t i = 0; i < length; ++i) {
            out.samples[i] = (static_cast<float>(bytes[i]) - 128.0f) / 128.0f;
        }
    } else {
        return false;
    }
    size_t wanted = size_t(frames) * out.channels;
    if (frames > 0 && wanted < out.samples.size()) {
        out.samples.resize(wanted);
    }
    return !out.samples.empty();
}

bool decodeWav(const std::string& data, PcmBuffer& out)
{
    if (data.size() < 12 || data.compare(0, 4, "RIFF") != 0 || data.compare(8, 4, "WAVE") != 0) {
        return false;
    }
    uint16_t format = 0;
    uint16_t bits = 0;
    size_t cursor = 12;
    while (cursor + 8 <= data.size()) {
        std::string id = data.substr(cursor, 4);
        uint32_t size = readLe32(data, cursor + 4);
        size_t body = cursor + 8;
        if (body + size > data.size()) {
            size = static_cast<uint32_t>(data.size() - body);
        }
        if (id == "fmt " && size >= 16) {
            format = util::readLe16(data, body);
            out.channels = util::readLe16(data, body + 2);
            out.sampleRate = readLe32(data, body + 4);
            bits = util::readLe16(data, body + 14);
        } else if (id == "data" && out.channels > 0) {
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data()) + body;
            if (format == 3 && bits == 32) {
                out.samples.resize(size / 4);
                std::memcpy(out.samples.data(), bytes, out.samples.size() * 4);
            } else if (bits == 16) {
                out.samples.resize(size / 2);
                for (size_t i = 0; i < out.samples.size(); ++i) {
                    out.samples[i] = static_cast<float>(readLittle<int16_t>(bytes + i * 2)) / 32768.0f;
                }
            } else if (bits == 8) {
                out.samples.resize(size);
                for (size_t i = 0; i < size; ++i) {
                    out.samples[i] = (static_cast<float>(bytes[i]) - 128.0f) / 128.0f;
                }
            } else if (bits == 32) {
                out.samples.resize(size / 4);
                for (size_t i = 0; i < out.samples.size(); ++i) {
                    out.samples[i] = static_cast<float>(readLittle<int32_t>(bytes + i * 4)) / 2147483648.0f;
                }
            }
            return !out.samples.empty();
        }
        cursor = body + size + (size & 1);
    }
    return false;
}

bool decodeOgg(const std::string& data, PcmBuffer& out)
{
    int channels = 0;
    int rate = 0;
    short* decoded = nullptr;
    int frames = stb_vorbis_decode_memory(reinterpret_cast<const unsigned char*>(data.data()), static_cast<int>(data.size()), &channels, &rate, &decoded);
    if (frames <= 0 || !decoded) {
        return false;
    }
    out.channels = static_cast<uint32_t>(channels);
    out.sampleRate = static_cast<uint32_t>(rate);
    out.samples.resize(size_t(frames) * size_t(channels));
    for (size_t i = 0; i < out.samples.size(); ++i) {
        out.samples[i] = static_cast<float>(decoded[i]) / 32768.0f;
    }
    free(decoded);
    return true;
}

bool decodeAudio(const std::string& data, const std::string& extension, PcmBuffer& out)
{
    if (extension == ".fsb") {
        return decodeFsb(data, out);
    }
    if (extension == ".wav") {
        return decodeWav(data, out);
    }
    return decodeOgg(data, out);
}

}
