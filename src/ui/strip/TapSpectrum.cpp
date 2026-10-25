#include "ui/strip/TapSpectrum.h"

#include "engine/Gain.h"
#include "ui/strip/StripStyle.h"

#include <algorithm>
#include <cmath>

namespace northlight::ui::strip
{

std::vector<float> hannWindow(int size)
{
    std::vector<float> window(static_cast<size_t>(size));
    for (int i = 0; i < size; ++i)
        window[static_cast<size_t>(i)] =
            0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * static_cast<float>(i) /
                                   static_cast<float>(size - 1));
    return window;
}

TapSpectrum::TapSpectrum(int taps)
    : window_(hannWindow(fftSize)), fftData_(static_cast<size_t>(fftSize) * 2, 0.0f),
      samples_(static_cast<size_t>(fftSize), 0.0f), spectra_(static_cast<size_t>(taps)),
      lastTap_(static_cast<size_t>(taps), 0)
{
}

void TapSpectrum::clear()
{
    for (auto& s : spectra_)
        s.clear();
}

void TapSpectrum::analyse(const engine::ProcessorFeedback& fb,
                          int tap,
                          juce::Rectangle<float> p,
                          double elapsedMs)
{
    auto& smoothed = spectra_[static_cast<size_t>(tap)];
    const int columns = std::max(1, static_cast<int>(p.getWidth()));
    if (static_cast<int>(smoothed.size()) != columns)
        smoothed.assign(static_cast<size_t>(columns), bottomDb);
    const float fall = static_cast<float>(elapsedMs * 0.001 * 40.0); // dB per second
    const auto position = fb.tapPosition(tap);
    const bool fresh = position != lastTap_[static_cast<size_t>(tap)];
    lastTap_[static_cast<size_t>(tap)] = position;
    if (!fresh || !fb.readTap(tap, samples_.data(), fftSize))
    {
        // Nothing new (the device stopped): let it fall away.
        for (auto& v : smoothed)
            v = std::max(bottomDb, v - fall);
        return;
    }
    std::fill(fftData_.begin(), fftData_.end(), 0.0f);
    for (int i = 0; i < fftSize; ++i)
        fftData_[static_cast<size_t>(i)] = samples_[static_cast<size_t>(i)] * window_[static_cast<size_t>(i)];
    fft_.performFrequencyOnlyForwardTransform(fftData_.data(), true);
    const double binHz = fb.sampleRate() / fftSize;
    const double reference = fftSize / 4.0; // a full-scale sine through the Hann window
    const auto binDb = [&](int bin)
    {
        const double m = fftData_[static_cast<size_t>(juce::jlimit(0, fftSize / 2, bin))] / reference;
        return engine::gainToDb(m, engine::magnitudeFloorDb);
    };
    for (int col = 0; col < columns; ++col)
    {
        const double f0 = xToHz(p.getX() + static_cast<float>(col), p);
        const double f1 = xToHz(p.getX() + static_cast<float>(col) + 1.0f, p);
        const double b0 = f0 / binHz, b1 = f1 / binHz;
        double db;
        if (b1 - b0 < 1.0)
        {
            // Narrower than a bin: interpolate between the neighbours.
            const double centre = 0.5 * (b0 + b1);
            const int lo = static_cast<int>(std::floor(centre));
            const double t = centre - lo;
            db = binDb(lo) * (1.0 - t) + binDb(lo + 1) * t;
        }
        else
        {
            db = -200.0;
            for (int bin = static_cast<int>(std::ceil(b0)); bin <= static_cast<int>(b1); ++bin)
                db = std::max(db, binDb(bin));
        }
        db += tiltDbPerOctave * std::log2(0.5 * (f0 + f1) / 1000.0);
        const float v = static_cast<float>(juce::jlimit<double>(bottomDb, topDb + 12.0, db));
        auto& s = smoothed[static_cast<size_t>(col)];
        s = v > s ? s + (v - s) * 0.6f : std::max(v, s - fall);
    }
}

} // namespace northlight::ui::strip
