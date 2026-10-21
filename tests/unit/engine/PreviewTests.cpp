#include "engine/Preview.h"
#include "engine/rt/RealtimeGuard.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

using namespace aural;
using Catch::Approx;

namespace
{
constexpr double rate = 48000.0;
constexpr int block = 128;

std::shared_ptr<engine::AudioSource> constant(float value, int samples, int channels = 1)
{
    return std::make_shared<engine::MemoryAudioSource>(
        std::vector<std::vector<float>>(static_cast<size_t>(channels),
                                        std::vector<float>(static_cast<size_t>(samples), value)),
        rate);
}

// Runs blocks over a bed of `bed` and returns the left output.
std::vector<float>
run(engine::PreviewPlayer& p, int blocks, float bed = 0.0f, std::vector<float>* right = nullptr)
{
    std::vector<float> out, outR;
    std::vector<float> l(block), r(block);
    for (int b = 0; b < blocks; ++b)
    {
        std::fill(l.begin(), l.end(), bed);
        std::fill(r.begin(), r.end(), bed);
        float* outputs[] = {l.data(), r.data()};
        p.process(outputs, 2, block);
        out.insert(out.end(), l.begin(), l.end());
        outR.insert(outR.end(), r.begin(), r.end());
    }
    if (right != nullptr)
        *right = outR;
    return out;
}

float maxStep(const std::vector<float>& x)
{
    float m = 0.0f;
    for (size_t i = 1; i < x.size(); ++i)
        m = std::max(m, std::abs(x[i] - x[i - 1]));
    return m;
}
} // namespace

TEST_CASE("Preview: a file plays over the mix at the preview gain, fading in, and ends", "[engine][preview]")
{
    engine::PreviewPlayer p;
    p.prepare(rate, block);
    p.setGain(0.5f);
    p.play(constant(0.8f, 4800)); // a tenth of a second, mono
    p.pump(true);
    CHECK(p.isPlaying());
    std::vector<float> right;
    const auto out = run(p, 60, 0.1f, &right);
    CHECK(out[0] == Approx(0.1f).margin(0.01f));           // the fade starts from nothing
    CHECK(out[1000] == Approx(0.1f + 0.4f).margin(1e-4f)); // the bed plus the preview
    CHECK(right[1000] == Approx(out[1000]));               // mono reaches both sides
    CHECK(out[5000] == Approx(0.1f));                      // ended
    CHECK(p.position() >= 4800);
    p.pump(true);
    CHECK_FALSE(p.isPlaying());
}

TEST_CASE("Preview: another file or a stop fades out what plays first, without a click", "[engine][preview]")
{
    engine::PreviewPlayer p;
    p.prepare(rate, block);
    p.setGain(1.0f);
    p.play(constant(0.5f, 48000));
    p.pump(true);
    run(p, 10);
    // Another file: the first fades out before the second is handed over.
    p.play(constant(-0.5f, 48000, 2));
    p.pump(true); // not yet: the first is still fading
    auto out = run(p, 4);
    CHECK(out.back() == 0.0f);
    p.pump(true);
    const auto second = run(p, 10);
    CHECK(second.back() == Approx(-0.5f));
    out.insert(out.end(), second.begin(), second.end());
    CHECK(maxStep(out) < 0.5f * static_cast<float>(1.0 / (rate * engine::PreviewPlayer::fadeSeconds)) * 1.5f);
    // A stop fades out too, and lets the source go.
    p.stop();
    CHECK_FALSE(p.isPlaying());
    const auto stopped = run(p, 4);
    CHECK(stopped.back() == 0.0f);
    CHECK(maxStep(stopped) <
          0.5f * static_cast<float>(1.0 / (rate * engine::PreviewPlayer::fadeSeconds)) * 1.5f);
    p.pump(true);
    CHECK(run(p, 2).back() == 0.0f);
}

TEST_CASE("Preview: with no audio thread a new file is handed over at once", "[engine][preview]")
{
    engine::PreviewPlayer p;
    p.prepare(rate, block);
    p.play(constant(0.5f, 48000));
    p.pump(false);
    p.play(constant(0.25f, 48000));
    p.pump(false);
    p.setGain(1.0f);
    CHECK(run(p, 4).back() == Approx(0.25f));
}

#ifdef AURAL_RT_CHECKS
namespace
{
std::atomic<int> previewViolations{0};
void countPreviewViolation(const rt::Violation&) noexcept
{
    previewViolations.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

TEST_CASE("Preview: playing, switching and stopping do not allocate on a real-time thread",
          "[engine][preview][rt]")
{
    engine::PreviewPlayer p;
    p.prepare(rate, block);
    const auto previous = rt::setViolationHandler(&countPreviewViolation);
    previewViolations = 0;
    std::vector<float> l(block), r(block);
    float* outputs[] = {l.data(), r.data()};
    for (int b = 0; b < 100; ++b)
    {
        if (b % 20 == 0)
            p.play(constant(0.3f, 4000 + b));
        if (b % 20 == 15)
            p.stop();
        p.pump(true);
        rt::ScopedRealtimeSection section;
        p.process(outputs, 2, block);
    }
    rt::setViolationHandler(previous);
    CHECK(previewViolations == 0);
}
#endif
