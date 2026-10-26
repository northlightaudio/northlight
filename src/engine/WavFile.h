#pragma once

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

// Minimal 32-bit float WAV writing for the recorder, with the crash-safety trick
// every DAW uses: the header is rewritten periodically while recording so a file
// left behind by a crash is valid up to the last update, and repair() fixes the
// header from the file size to recover everything after it. JUCE-free so the engine
// tests can exercise it.

namespace northlight::engine
{

struct WavInfo
{
    int numChannels = 0;
    double sampleRate = 0.0;
    std::int64_t lengthSamples = 0;
    bool isFloat = false;
    int bitsPerSample = 0;
};

class WavWriter
{
public:
    WavWriter() = default;
    ~WavWriter();
    WavWriter(const WavWriter&) = delete;
    WavWriter& operator=(const WavWriter&) = delete;

    // `bitsPerSample` 32 writes IEEE float, 24 writes PCM (clipped at full scale,
    // with triangular dither below the last bit).
    bool open(const std::string& path, int numChannels, double sampleRate, int bitsPerSample = 32);
    bool isOpen() const noexcept { return file_ != nullptr; }

    // Interleaves and appends numSamples from each channel. Not real-time.
    bool write(const float* const* channels, int numSamples);
    // Appends already interleaved frames.
    bool writeInterleaved(const float* frames, int numSamples);

    // Rewrites the RIFF and data sizes for what has been written so far.
    bool updateHeader();
    // Final header update and close.
    bool close();

    std::int64_t samplesWritten() const noexcept { return samplesWritten_; }
    const std::string& path() const noexcept { return path_; }

    // Fixes the header sizes of a file that was never closed, from its size on disk.
    static bool repair(const std::string& path);

private:
    bool writeHeader();

    std::FILE* file_ = nullptr;
    std::string path_;
    int numChannels_ = 0;
    double sampleRate_ = 0.0;
    int bitsPerSample_ = 32;
    std::int64_t samplesWritten_ = 0;
    std::vector<float> interleaveBuffer_;
    std::vector<unsigned char> pcmBuffer_;
    std::uint32_t ditherState_ = 0x9e3779b9u;
};

// Header parse (PCM 16/24/32 and float 32 are recognised).
std::optional<WavInfo> readWavInfo(const std::string& path);

// Reads the whole file as floats, de-interleaved; for tests and small files only.
bool readWavSamples(const std::string& path,
                    std::vector<std::vector<float>>& channels,
                    WavInfo* info = nullptr);

} // namespace northlight::engine
