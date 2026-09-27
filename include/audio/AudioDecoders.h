#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kestrel::audio {

/**
 * Decoded audio: interleaved float samples at their own rate.
 */
struct PcmBuffer {
    uint32_t channels = 1;
    uint32_t sampleRate = 44100;
    std::vector<float> samples;
};

/**
 * Decodes the first sample of an FMOD sound bank (FSB5): FMOD ADPCM, 8 or
 * 16 bit PCM.
 */
bool decodeFsb(const std::string& data, PcmBuffer& out);

/**
 * Decodes a RIFF wave file holding 8, 16 or 32 bit PCM or 32 bit float
 * samples.
 */
bool decodeWav(const std::string& data, PcmBuffer& out);

bool decodeOgg(const std::string& data, PcmBuffer& out);

/**
 * Decodes by file extension (.fsb, .wav, anything else as Ogg Vorbis).
 */
bool decodeAudio(const std::string& data, const std::string& extension, PcmBuffer& out);

}
