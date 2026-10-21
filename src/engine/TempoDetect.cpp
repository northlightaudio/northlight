#include "engine/TempoDetect.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numeric>

namespace aural::engine
{

namespace
{
bool isDigit(char c) noexcept
{
    return c >= '0' && c <= '9';
}
} // namespace

double tempoFromName(const std::string& name)
{
    std::string lower;
    lower.reserve(name.size());
    for (char c : name)
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    // The extension is not part of the name.
    if (const auto dot = lower.rfind('.'); dot != std::string::npos && lower.size() - dot <= 5)
        lower.resize(dot);

    // Numbers in the name, with what surrounds them.
    struct Number
    {
        double value = 0.0;
        size_t begin = 0, end = 0;
    };
    std::vector<Number> numbers;
    for (size_t i = 0; i < lower.size();)
    {
        if (!isDigit(lower[i]))
        {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < lower.size() && (isDigit(lower[j]) || (lower[j] == '.' && j + 1 < lower.size() &&
                                                          isDigit(lower[j + 1]) && j > i)))
            ++j;
        numbers.push_back({std::atof(lower.substr(i, j - i).c_str()), i, j});
        i = j;
    }
    const auto plausible = [](double v) { return v >= 40.0 && v <= 300.0; };
    // "124bpm", "124 bpm", "124_bpm", "bpm124", "bpm 124".
    for (const auto& n : numbers)
    {
        size_t after = n.end;
        while (after < lower.size() && (lower[after] == ' ' || lower[after] == '_' || lower[after] == '-'))
            ++after;
        if (lower.compare(after, 3, "bpm") == 0 && plausible(n.value))
            return n.value;
        size_t before = n.begin;
        while (before > 0 &&
               (lower[before - 1] == ' ' || lower[before - 1] == '_' || lower[before - 1] == '-'))
            --before;
        if (before >= 3 && lower.compare(before - 3, 3, "bpm") == 0 && plausible(n.value))
            return n.value;
    }
    // A lone number between separators, in the usual tempo range.
    for (const auto& n : numbers)
    {
        const bool leftEdge = n.begin == 0 || !std::isalpha(static_cast<unsigned char>(lower[n.begin - 1]));
        const bool rightEdge =
            n.end == lower.size() || !std::isalpha(static_cast<unsigned char>(lower[n.end]));
        if (leftEdge && rightEdge && n.value >= 60.0 && n.value <= 200.0 && std::floor(n.value) == n.value)
            return n.value;
    }
    return 0.0;
}

TempoGuess tempoFromBeats(const std::vector<const float*>& channels,
                          std::int64_t length,
                          double sampleRate,
                          double minBpm,
                          double maxBpm)
{
    TempoGuess guess;
    if (channels.empty() || length <= 0 || sampleRate <= 0.0)
        return guess;
    // An onset envelope: the rise of log energy in short frames.
    const int hop = std::max(64, static_cast<int>(std::lround(sampleRate / 93.75))); // ~512 at 48 kHz
    const int window = hop * 2;
    const auto frames = static_cast<size_t>(std::max<std::int64_t>(0, (length - window) / hop));
    if (frames < 64)
        return guess;
    std::vector<double> energy(frames, 0.0);
    for (size_t f = 0; f < frames; ++f)
    {
        double sum = 0.0;
        const auto at = static_cast<std::int64_t>(f) * hop;
        for (const float* ch : channels)
            for (int i = 0; i < window; ++i)
            {
                const double x = ch[at + i];
                sum += x * x;
            }
        energy[f] = std::log(1.0e-9 + sum / (window * channels.size()));
    }
    std::vector<double> onset(frames, 0.0);
    for (size_t f = 1; f < frames; ++f)
        onset[f] = std::max(0.0, energy[f] - energy[f - 1]);
    const double mean = std::accumulate(onset.begin(), onset.end(), 0.0) / static_cast<double>(frames);
    for (auto& v : onset)
        v -= mean;

    // Autocorrelation over the lags of the tempo range, each lag scored with its
    // multiples too (a comb), so the beat period beats its subdivisions.
    const double framesPerSecond = sampleRate / hop;
    const auto lagFor = [&](double bpm) { return 60.0 / bpm * framesPerSecond; };
    const auto minLag = static_cast<size_t>(std::floor(lagFor(maxBpm * 2.0)));
    const auto maxLag = static_cast<size_t>(std::ceil(lagFor(minBpm / 2.0)));
    std::vector<double> ac(maxLag + 2, 0.0);
    for (size_t lag = std::max<size_t>(1, minLag); lag <= maxLag && lag < frames; ++lag)
    {
        double sum = 0.0;
        for (size_t f = lag; f < frames; ++f)
            sum += onset[f] * onset[f - lag];
        ac[lag] = sum / static_cast<double>(frames - lag);
    }
    double zero = 0.0;
    for (const double v : onset)
        zero += v * v;
    zero /= static_cast<double>(frames);
    if (zero <= 0.0)
        return guess;
    double best = -1.0e300;
    double bestLag = 0.0;
    const auto first = static_cast<size_t>(std::floor(lagFor(maxBpm)));
    const auto last = static_cast<size_t>(std::ceil(lagFor(minBpm)));
    for (size_t lag = std::max<size_t>(2, first); lag <= last && lag * 2 < ac.size(); ++lag)
    {
        const double score = ac[lag] + 0.5 * ac[std::min(ac.size() - 1, lag * 2)] +
                             0.25 * (lag * 4 < ac.size() ? ac[lag * 4] : 0.0) +
                             0.25 * ac[std::max<size_t>(1, lag / 2)];
        if (score > best)
        {
            best = score;
            bestLag = static_cast<double>(lag);
        }
    }
    if (bestLag <= 0.0)
        return guess;
    // Parabolic refinement around the peak.
    const auto l = static_cast<size_t>(bestLag);
    if (l > 1 && l + 1 < ac.size())
    {
        const double a = ac[l - 1], b = ac[l], c = ac[l + 1];
        const double d = a - 2.0 * b + c;
        if (d < 0.0)
            bestLag += std::clamp(0.5 * (a - c) / d, -0.5, 0.5);
    }
    double bpm = 60.0 * framesPerSecond / bestLag;
    while (bpm < minBpm)
        bpm *= 2.0;
    while (bpm > maxBpm)
        bpm *= 0.5;
    guess.bpm = bpm;
    guess.confidence = std::clamp(ac[l] / zero, 0.0, 1.0);
    guess.source = TempoGuess::Source::beats;
    return guess;
}

double tempoFromLength(
    std::int64_t length, double sampleRate, double beatsPerBar, double near, double minBpm, double maxBpm)
{
    if (length <= 0 || sampleRate <= 0.0)
        return 0.0;
    const double seconds = static_cast<double>(length) / sampleRate;
    const double target = near > 0.0 ? near : 120.0;
    double best = 0.0;
    for (const int bars : {1, 2, 4, 8, 16, 32})
    {
        const double bpm = bars * beatsPerBar * 60.0 / seconds;
        if (bpm < minBpm || bpm > maxBpm)
            continue;
        if (best == 0.0 || std::abs(std::log(bpm / target)) < std::abs(std::log(best / target)))
            best = bpm;
    }
    return best;
}

TempoGuess guessTempo(const std::string& name,
                      const std::vector<const float*>& channels,
                      std::int64_t length,
                      double sampleRate)
{
    if (const double named = tempoFromName(name); named > 0.0)
        return {named, 1.0, TempoGuess::Source::name};
    const auto beats = tempoFromBeats(channels, length, sampleRate);
    const double byLength = tempoFromLength(length, sampleRate, 4.0, beats.bpm);
    if (beats.bpm > 0.0 && byLength > 0.0 && std::abs(byLength / beats.bpm - 1.0) < 0.03)
        return {byLength, std::max(beats.confidence, 0.8), TempoGuess::Source::length};
    if (beats.bpm > 0.0)
        return beats;
    if (byLength > 0.0)
        return {byLength, 0.2, TempoGuess::Source::length};
    return {};
}

} // namespace aural::engine
