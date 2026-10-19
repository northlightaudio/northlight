#include "engine/WavFile.h"

#include "engine/rt/RealtimeGuard.h"

#include <algorithm>
#include <cstring>

#if defined(_WIN32)
#define AURAL_FSEEK _fseeki64
#define AURAL_FTELL _ftelli64
#else
#define AURAL_FSEEK fseeko
#define AURAL_FTELL ftello
#endif

namespace aural::engine
{

namespace
{
constexpr std::int64_t headerBytes = 44;

void put32(unsigned char* p, std::uint32_t v)
{
    p[0] = static_cast<unsigned char>(v & 0xff);
    p[1] = static_cast<unsigned char>((v >> 8) & 0xff);
    p[2] = static_cast<unsigned char>((v >> 16) & 0xff);
    p[3] = static_cast<unsigned char>((v >> 24) & 0xff);
}

void put16(unsigned char* p, std::uint16_t v)
{
    p[0] = static_cast<unsigned char>(v & 0xff);
    p[1] = static_cast<unsigned char>((v >> 8) & 0xff);
}

std::uint32_t get32(const unsigned char* p)
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint16_t get16(const unsigned char* p)
{
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

// Builds the 44-byte canonical header for float32 data of `dataBytes`.
void buildHeader(unsigned char* h, int numChannels, double sampleRate, std::uint32_t dataBytes)
{
    const auto rate = static_cast<std::uint32_t>(sampleRate);
    const auto blockAlign = static_cast<std::uint16_t>(numChannels * 4);
    std::memcpy(h, "RIFF", 4);
    put32(h + 4, 36 + dataBytes);
    std::memcpy(h + 8, "WAVE", 4);
    std::memcpy(h + 12, "fmt ", 4);
    put32(h + 16, 16);
    put16(h + 20, 3); // IEEE float
    put16(h + 22, static_cast<std::uint16_t>(numChannels));
    put32(h + 24, rate);
    put32(h + 28, rate * blockAlign);
    put16(h + 32, blockAlign);
    put16(h + 34, 32);
    std::memcpy(h + 36, "data", 4);
    put32(h + 40, dataBytes);
}

std::uint32_t clampBytes(std::int64_t bytes)
{
    return static_cast<std::uint32_t>(std::min<std::int64_t>(bytes, 0xffffffffll - 44));
}
} // namespace

WavWriter::~WavWriter()
{
    if (file_ != nullptr)
        close();
}

bool WavWriter::open(const std::string& path, int numChannels, double sampleRate)
{
    AURAL_ASSERT_NOT_REALTIME("WavWriter::open");
    if (file_ != nullptr || numChannels <= 0 || sampleRate <= 0.0)
        return false;
    file_ = std::fopen(path.c_str(), "wb");
    if (file_ == nullptr)
        return false;
    path_ = path;
    numChannels_ = numChannels;
    sampleRate_ = sampleRate;
    samplesWritten_ = 0;
    if (!writeHeader())
    {
        std::fclose(file_);
        file_ = nullptr;
        return false;
    }
    return true;
}

bool WavWriter::writeHeader()
{
    unsigned char h[headerBytes];
    buildHeader(h, numChannels_, sampleRate_, clampBytes(samplesWritten_ * numChannels_ * 4));
    if (AURAL_FSEEK(file_, 0, SEEK_SET) != 0)
        return false;
    if (std::fwrite(h, 1, headerBytes, file_) != headerBytes)
        return false;
    return AURAL_FSEEK(file_, 0, SEEK_END) == 0;
}

bool WavWriter::write(const float* const* channels, int numSamples)
{
    if (file_ == nullptr || numSamples <= 0)
        return file_ != nullptr;
    interleaveBuffer_.resize(static_cast<size_t>(numSamples) * static_cast<size_t>(numChannels_));
    for (int ch = 0; ch < numChannels_; ++ch)
    {
        const float* src = channels[ch];
        for (int i = 0; i < numSamples; ++i)
            interleaveBuffer_[static_cast<size_t>(i) * static_cast<size_t>(numChannels_) +
                              static_cast<size_t>(ch)] = src != nullptr ? src[i] : 0.0f;
    }
    return writeInterleaved(interleaveBuffer_.data(), numSamples);
}

bool WavWriter::writeInterleaved(const float* frames, int numSamples)
{
    if (file_ == nullptr || numSamples <= 0)
        return file_ != nullptr;
    const auto count = static_cast<size_t>(numSamples) * static_cast<size_t>(numChannels_);
    if (std::fwrite(frames, sizeof(float), count, file_) != count)
        return false;
    samplesWritten_ += numSamples;
    return true;
}

bool WavWriter::updateHeader()
{
    if (file_ == nullptr)
        return false;
    if (std::fflush(file_) != 0)
        return false;
    return writeHeader() && std::fflush(file_) == 0;
}

bool WavWriter::close()
{
    if (file_ == nullptr)
        return false;
    const bool ok = writeHeader();
    const bool closed = std::fclose(file_) == 0;
    file_ = nullptr;
    return ok && closed;
}

bool WavWriter::repair(const std::string& path)
{
    AURAL_ASSERT_NOT_REALTIME("WavWriter::repair");
    std::FILE* f = std::fopen(path.c_str(), "r+b");
    if (f == nullptr)
        return false;
    unsigned char h[headerBytes];
    bool ok = std::fread(h, 1, headerBytes, f) == headerBytes && std::memcmp(h, "RIFF", 4) == 0 &&
              std::memcmp(h + 8, "WAVE", 4) == 0 && std::memcmp(h + 36, "data", 4) == 0;
    if (ok)
    {
        ok = AURAL_FSEEK(f, 0, SEEK_END) == 0;
        const std::int64_t size = AURAL_FTELL(f);
        const auto blockAlign = static_cast<std::int64_t>(get16(h + 32));
        if (ok && size >= headerBytes && blockAlign > 0)
        {
            // Only whole frames count; a crash can leave a partial one.
            const std::int64_t dataBytes = ((size - headerBytes) / blockAlign) * blockAlign;
            put32(h + 4, 36 + clampBytes(dataBytes));
            put32(h + 40, clampBytes(dataBytes));
            ok = AURAL_FSEEK(f, 0, SEEK_SET) == 0 && std::fwrite(h, 1, headerBytes, f) == headerBytes;
        }
    }
    const bool closed = std::fclose(f) == 0;
    return ok && closed;
}

std::optional<WavInfo> readWavInfo(const std::string& path)
{
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr)
        return std::nullopt;
    unsigned char h[headerBytes];
    const bool ok = std::fread(h, 1, headerBytes, f) == headerBytes;
    std::fclose(f);
    if (!ok || std::memcmp(h, "RIFF", 4) != 0 || std::memcmp(h + 8, "WAVE", 4) != 0 ||
        std::memcmp(h + 12, "fmt ", 4) != 0 || std::memcmp(h + 36, "data", 4) != 0)
        return std::nullopt;
    WavInfo info;
    const auto format = get16(h + 20);
    info.numChannels = get16(h + 22);
    info.sampleRate = get32(h + 24);
    info.bitsPerSample = get16(h + 34);
    info.isFloat = format == 3;
    const auto blockAlign = get16(h + 32);
    if (info.numChannels <= 0 || blockAlign == 0)
        return std::nullopt;
    info.lengthSamples = static_cast<std::int64_t>(get32(h + 40)) / blockAlign;
    return info;
}

bool readWavSamples(const std::string& path, std::vector<std::vector<float>>& channels, WavInfo* infoOut)
{
    const auto info = readWavInfo(path);
    if (!info.has_value())
        return false;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr)
        return false;
    bool ok = AURAL_FSEEK(f, headerBytes, SEEK_SET) == 0;
    const auto frames = static_cast<size_t>(info->lengthSamples);
    const auto bytesPerSample = static_cast<size_t>(info->bitsPerSample / 8);
    std::vector<unsigned char> raw(frames * static_cast<size_t>(info->numChannels) * bytesPerSample);
    ok = ok && (raw.empty() || std::fread(raw.data(), 1, raw.size(), f) == raw.size());
    std::fclose(f);
    if (!ok)
        return false;

    channels.assign(static_cast<size_t>(info->numChannels), std::vector<float>(frames, 0.0f));
    const unsigned char* p = raw.data();
    for (size_t i = 0; i < frames; ++i)
        for (int ch = 0; ch < info->numChannels; ++ch)
        {
            float v = 0.0f;
            if (info->isFloat && bytesPerSample == 4)
                std::memcpy(&v, p, 4);
            else if (bytesPerSample == 2)
                v = static_cast<float>(static_cast<std::int16_t>(get16(p))) / 32768.0f;
            else if (bytesPerSample == 3)
            {
                const std::int32_t s = static_cast<std::int32_t>((static_cast<std::uint32_t>(p[0]) << 8) |
                                                                 (static_cast<std::uint32_t>(p[1]) << 16) |
                                                                 (static_cast<std::uint32_t>(p[2]) << 24)) >>
                                       8;
                v = static_cast<float>(s) / 8388608.0f;
            }
            else if (bytesPerSample == 4)
                v = static_cast<float>(static_cast<std::int32_t>(get32(p))) / 2147483648.0f;
            channels[static_cast<size_t>(ch)][i] = v;
            p += bytesPerSample;
        }
    if (infoOut != nullptr)
        *infoOut = *info;
    return true;
}

} // namespace aural::engine
