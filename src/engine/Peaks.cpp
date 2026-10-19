#include "engine/Peaks.h"

#include "engine/rt/RealtimeGuard.h"

#include <algorithm>
#include <cstring>

namespace aural::engine
{

namespace
{
constexpr char magic[8] = {'A', 'U', 'R', 'P', 'E', 'A', 'K', '1'};

template <typename T> void put(std::string& out, const T& value)
{
    out.append(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T> bool get(const std::string& in, size_t& pos, T& value)
{
    if (pos + sizeof(T) > in.size())
        return false;
    std::memcpy(&value, in.data() + pos, sizeof(T));
    pos += sizeof(T);
    return true;
}
} // namespace

PeakRange PeakData::lookup(int channel, std::int64_t start, std::int64_t end) const noexcept
{
    PeakRange range;
    if (channels_.empty() || end <= start)
        return range;
    const auto& levels = channels_[static_cast<size_t>(channel % numChannels())];

    // Choose the coarsest level whose bin is no longer than a quarter of the span,
    // so at most a handful of bins are scanned per lookup.
    const std::int64_t span = end - start;
    size_t chosen = 0;
    for (size_t i = 0; i < levels.size(); ++i)
        if (static_cast<std::int64_t>(levels[i].samplesPerBin) * 4 <= span)
            chosen = i;
    const auto& level = levels[chosen];
    const auto spb = static_cast<std::int64_t>(level.samplesPerBin);

    const std::int64_t clampedStart = std::max<std::int64_t>(0, start);
    const std::int64_t clampedEnd = std::min<std::int64_t>(end, length_);
    if (clampedEnd <= clampedStart)
        return range;

    const auto firstBin = static_cast<size_t>(clampedStart / spb);
    const auto lastBin = static_cast<size_t>((clampedEnd - 1) / spb);
    range.minimum = 1.0f;
    range.maximum = -1.0f;
    for (size_t b = firstBin; b <= lastBin && b < level.minimum.size(); ++b)
    {
        range.minimum = std::min(range.minimum, level.minimum[b]);
        range.maximum = std::max(range.maximum, level.maximum[b]);
    }
    if (range.minimum > range.maximum)
        range = {};
    return range;
}

PeakData PeakData::build(StreamReader& reader,
                         int baseSamplesPerBin,
                         int levelCount,
                         const std::function<bool(double)>& progress)
{
    AURAL_ASSERT_NOT_REALTIME("PeakData::build");
    PeakData data;
    data.length_ = reader.lengthSamples();
    data.sampleRate_ = reader.sampleRate();
    const int numChannels = std::max(1, reader.numChannels());
    baseSamplesPerBin = std::max(1, baseSamplesPerBin);
    levelCount = std::max(1, levelCount);

    const auto baseBins = static_cast<size_t>((data.length_ + baseSamplesPerBin - 1) / baseSamplesPerBin);
    data.channels_.resize(static_cast<size_t>(numChannels));
    for (auto& levels : data.channels_)
    {
        levels.resize(static_cast<size_t>(levelCount));
        int spb = baseSamplesPerBin;
        for (auto& level : levels)
        {
            level.samplesPerBin = spb;
            const auto bins = static_cast<size_t>((data.length_ + spb - 1) / spb);
            level.minimum.assign(bins, 0.0f);
            level.maximum.assign(bins, 0.0f);
            spb *= levelRatio;
        }
    }

    // Base level straight from the samples, in chunks of whole bins.
    const int binsPerChunk = std::max(1, 65536 / baseSamplesPerBin);
    const int chunkSamples = binsPerChunk * baseSamplesPerBin;
    std::vector<std::vector<float>> chunk(static_cast<size_t>(numChannels),
                                          std::vector<float>(static_cast<size_t>(chunkSamples)));
    std::vector<float*> pointers;
    for (auto& c : chunk)
        pointers.push_back(c.data());

    for (std::int64_t position = 0; position < data.length_; position += chunkSamples)
    {
        const auto count = static_cast<int>(std::min<std::int64_t>(chunkSamples, data.length_ - position));
        if (!reader.read(position, count, pointers.data()))
            break;
        const auto firstBin = static_cast<size_t>(position / baseSamplesPerBin);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& level = data.channels_[static_cast<size_t>(ch)][0];
            const float* samples = chunk[static_cast<size_t>(ch)].data();
            for (int offset = 0; offset < count; offset += baseSamplesPerBin)
            {
                const int n = std::min(baseSamplesPerBin, count - offset);
                float lo = samples[offset], hi = samples[offset];
                for (int i = 1; i < n; ++i)
                {
                    lo = std::min(lo, samples[offset + i]);
                    hi = std::max(hi, samples[offset + i]);
                }
                const size_t bin = firstBin + static_cast<size_t>(offset / baseSamplesPerBin);
                if (bin < level.minimum.size())
                {
                    level.minimum[bin] = lo;
                    level.maximum[bin] = hi;
                }
            }
        }
        if (progress && !progress(static_cast<double>(position + count) / static_cast<double>(data.length_)))
            return PeakData{};
    }

    // Coarser levels fold the finer ones.
    for (auto& levels : data.channels_)
    {
        for (size_t l = 1; l < levels.size(); ++l)
        {
            const auto& fine = levels[l - 1];
            auto& coarse = levels[l];
            for (size_t b = 0; b < coarse.minimum.size(); ++b)
            {
                const size_t from = b * levelRatio;
                const size_t to = std::min(fine.minimum.size(), from + levelRatio);
                float lo = 1.0f, hi = -1.0f;
                for (size_t f = from; f < to; ++f)
                {
                    lo = std::min(lo, fine.minimum[f]);
                    hi = std::max(hi, fine.maximum[f]);
                }
                coarse.minimum[b] = lo <= hi ? lo : 0.0f;
                coarse.maximum[b] = lo <= hi ? hi : 0.0f;
            }
        }
    }
    (void)baseBins;
    return data;
}

std::string PeakData::serialise() const
{
    std::string out;
    out.append(magic, sizeof(magic));
    put(out, length_);
    put(out, sampleRate_);
    put<std::int32_t>(out, numChannels());
    put<std::int32_t>(out, levelCount());
    for (const auto& levels : channels_)
        for (const auto& level : levels)
        {
            put<std::int32_t>(out, level.samplesPerBin);
            put<std::int64_t>(out, static_cast<std::int64_t>(level.minimum.size()));
            out.append(reinterpret_cast<const char*>(level.minimum.data()),
                       level.minimum.size() * sizeof(float));
            out.append(reinterpret_cast<const char*>(level.maximum.data()),
                       level.maximum.size() * sizeof(float));
        }
    return out;
}

bool PeakData::deserialise(const std::string& in, PeakData& out)
{
    if (in.size() < sizeof(magic) || std::memcmp(in.data(), magic, sizeof(magic)) != 0)
        return false;
    size_t pos = sizeof(magic);
    PeakData data;
    std::int32_t channels = 0, levels = 0;
    if (!get(in, pos, data.length_) || !get(in, pos, data.sampleRate_) || !get(in, pos, channels) ||
        !get(in, pos, levels))
        return false;
    if (channels <= 0 || channels > 64 || levels <= 0 || levels > 16 || data.length_ < 0)
        return false;

    data.channels_.resize(static_cast<size_t>(channels));
    for (auto& levelList : data.channels_)
    {
        levelList.resize(static_cast<size_t>(levels));
        for (auto& level : levelList)
        {
            std::int64_t bins = 0;
            if (!get(in, pos, level.samplesPerBin) || !get(in, pos, bins) || bins < 0)
                return false;
            const size_t bytes = static_cast<size_t>(bins) * sizeof(float);
            if (pos + 2 * bytes > in.size())
                return false;
            level.minimum.resize(static_cast<size_t>(bins));
            level.maximum.resize(static_cast<size_t>(bins));
            std::memcpy(level.minimum.data(), in.data() + pos, bytes);
            pos += bytes;
            std::memcpy(level.maximum.data(), in.data() + pos, bytes);
            pos += bytes;
        }
    }
    out = std::move(data);
    return true;
}

} // namespace aural::engine
