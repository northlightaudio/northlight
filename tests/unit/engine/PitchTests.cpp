#include "core/Command.h"
#include "core/ProjectJson.h"
#include "engine/AudioEngine.h"
#include "engine/Snapshot.h"
#include "engine/pitch/Correction.h"
#include "engine/pitch/PitchAnalysis.h"
#include "engine/pitch/VocalOps.h"
#include "engine/pitch/VocalRender.h"
#include "engine/pitch/VocalService.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/BuiltIns.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <numeric>
#include <random>

using namespace northlight;
using namespace northlight::engine;
using namespace northlight::engine::pitch;

namespace
{
constexpr double rate = 48000.0;
constexpr double pi = 3.14159265358979323846;

// The test voice's formant envelope: three resonances over a floor.
double envelope(double hz)
{
    const auto peak = [hz](double f, double w) { return std::exp(-((hz - f) / w) * ((hz - f) / w)); };
    return 0.3 + peak(700.0, 130.0) + 0.6 * peak(1200.0, 150.0) + 0.3 * peak(2600.0, 200.0);
}

// A synthetic voice: harmonics up to 5 kHz under the formant envelope, following
// `hz(t)` while `voiced(t)`; optional noise where `noise(t)` > 0 (its level).
struct Voice
{
    std::function<double(double)> hz;
    std::function<bool(double)> voiced = [](double) { return true; };
    std::function<double(double)> noise = [](double) { return 0.0; };
};

std::vector<float> synthesise(const Voice& v, double seconds)
{
    const auto n = static_cast<size_t>(seconds * rate);
    std::vector<float> out(n, 0.0f);
    double phase = 0.0;
    std::mt19937 random(7);
    std::normal_distribution<double> gauss(0.0, 1.0);
    double gate = 0.0;   // smooths voicing on and off over ~5 ms
    double breath = 0.0; // the noise, low-passed around 2 kHz like a breath
    for (size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double>(i) / rate;
        const double f0 = v.hz(t);
        phase += 2.0 * pi * f0 / rate;
        if (phase > 2.0 * pi * 1000.0)
            phase -= 2.0 * pi * 1000.0;
        gate += ((v.voiced(t) ? 1.0 : 0.0) - gate) * 0.004;
        double s = 0.0;
        if (gate > 1.0e-4)
            for (int k = 1; k * f0 < 5000.0; ++k)
                s += envelope(k * f0) / k * std::sin(k * phase);
        breath += 0.25 * (gauss(random) - breath);
        out[i] = static_cast<float>(0.25 * gate * s + 2.0 * v.noise(t) * breath);
    }
    return out;
}

double centsOf(double hz)
{
    return 1200.0 * std::log2(hz / 440.0) + 6900.0;
}

// The amplitude of the component at `hz` in a Hann-windowed stretch.
double amplitudeAt(const std::vector<float>& x, size_t start, size_t length, double hz)
{
    double re = 0.0, im = 0.0;
    for (size_t i = 0; i < length; ++i)
    {
        const double w =
            0.5 - 0.5 * std::cos(2.0 * pi * static_cast<double>(i) / static_cast<double>(length - 1));
        const double p = 2.0 * pi * hz * static_cast<double>(i) / rate;
        re += w * x[start + i] * std::cos(p);
        im += w * x[start + i] * std::sin(p);
    }
    return std::sqrt(re * re + im * im);
}

double correlation(const std::vector<double>& a, const std::vector<double>& b)
{
    const double ma = std::accumulate(a.begin(), a.end(), 0.0) / static_cast<double>(a.size());
    const double mb = std::accumulate(b.begin(), b.end(), 0.0) / static_cast<double>(b.size());
    double sab = 0, saa = 0, sbb = 0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        sab += (a[i] - ma) * (b[i] - mb);
        saa += (a[i] - ma) * (a[i] - ma);
        sbb += (b[i] - mb) * (b[i] - mb);
    }
    return sab / std::sqrt(saa * sbb);
}

// The median pitch the analysis finds over [from, to) seconds.
double medianCents(const PitchAnalysis& a, double from, double to)
{
    std::vector<float> v;
    for (int f = 0; f < a.frames(); ++f)
    {
        const double t = static_cast<double>(f) * a.hop / rate;
        if (t >= from && t < to && a.cents[static_cast<size_t>(f)] > 0.0f)
            v.push_back(a.cents[static_cast<size_t>(f)]);
    }
    if (v.empty())
        return 0.0;
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2), v.end());
    return v[v.size() / 2];
}

double spreadCents(const PitchAnalysis& a, double from, double to)
{
    std::vector<double> v;
    for (int f = 0; f < a.frames(); ++f)
    {
        const double t = static_cast<double>(f) * a.hop / rate;
        if (t >= from && t < to && a.cents[static_cast<size_t>(f)] > 0.0f)
            v.push_back(a.cents[static_cast<size_t>(f)]);
    }
    const double m = std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
    double s = 0.0;
    for (const double x : v)
        s += (x - m) * (x - m);
    return std::sqrt(s / static_cast<double>(v.size()));
}

std::vector<float> applySpans(const std::vector<float>& original, const std::vector<RenderedSpan>& spans)
{
    auto out = original;
    for (const auto& s : spans)
        for (std::int64_t i = 0; i < s.length(); ++i)
            out[static_cast<size_t>(s.start + i)] = s.channels[0][static_cast<size_t>(i)];
    return out;
}

std::vector<float>
render(const std::vector<float>& audio, const PitchAnalysis& a, const core::VocalEdit& edit)
{
    const std::vector<std::vector<float>> media{audio};
    const auto shift = shiftCurve(a, edit);
    std::vector<RenderedSpan> spans;
    for (const auto& plan : planSpans(a, edit, 1))
        spans.push_back(renderSpan(media, a, edit, shift, plan));
    return applySpans(audio, spans);
}

// One steady note, the most common case.
const std::vector<float>& steadyA3()
{
    static const auto audio = synthesise(Voice{[](double) { return 220.0; }}, 1.0);
    return audio;
}

class ToneLoader final : public MediaLoader
{
public:
    std::map<std::string, std::vector<float>> files;
    int loads = 0;
    std::shared_ptr<AudioSource> load(const std::string& path, double sampleRate, std::string& error) override
    {
        ++loads;
        const auto it = files.find(path);
        if (it == files.end())
        {
            error = "no such file";
            return nullptr;
        }
        return std::make_shared<MemoryAudioSource>(std::vector<std::vector<float>>{it->second}, sampleRate);
    }
};
} // namespace

TEST_CASE("Pitch analysis tracks a steady note within a few cents", "[engine][pitch]")
{
    const auto a = analyse({steadyA3()}, rate);
    REQUIRE(a.frames() > 150);
    CHECK(std::abs(medianCents(a, 0.1, 0.9) - centsOf(220.0)) < 3.0);
    REQUIRE(a.notes.size() == 1);
    CHECK(std::abs(a.notes[0].centre - centsOf(220.0)) < 3.0);
    CHECK(a.notes[0].target == a.notes[0].centre);
    // Pitch marks one period apart.
    REQUIRE(a.marks.size() > 100);
    for (size_t i = 10; i + 10 < a.marks.size(); ++i)
        CHECK(std::abs(static_cast<double>(a.marks[i + 1] - a.marks[i]) - rate / 220.0) < 2.0);
}

TEST_CASE("Pitch analysis follows vibrato and glides", "[engine][pitch]")
{
    const auto hz = [](double t) { return 220.0 * std::exp2(50.0 * std::sin(2.0 * pi * 5.5 * t) / 1200.0); };
    const auto audio = synthesise(Voice{hz}, 1.0);
    const auto a = analyse({audio}, rate);
    std::vector<double> errors;
    for (int f = 0; f < a.frames(); ++f)
    {
        const double t = static_cast<double>(f) * a.hop / rate;
        if (t < 0.1 || t > 0.9)
            continue;
        REQUIRE(a.cents[static_cast<size_t>(f)] > 0.0f);
        errors.push_back(std::abs(a.cents[static_cast<size_t>(f)] - centsOf(hz(t))));
    }
    std::sort(errors.begin(), errors.end());
    CHECK(errors[errors.size() / 2] < 5.0);
    CHECK(errors[errors.size() * 95 / 100] < 15.0);
    // Vibrato stays inside one note.
    CHECK(a.notes.size() == 1);
}

TEST_CASE("Pitch analysis splits a legato melody into its notes", "[engine][pitch]")
{
    // C4, E4, G4 for 0.4 s each, gliding 50 ms between them.
    const auto hz = [](double t)
    {
        const double c = 261.63, e = 329.63, g = 392.0;
        const auto glide = [](double from, double to, double s)
        { return from * std::pow(to / from, std::clamp(s, 0.0, 1.0)); };
        if (t < 0.4)
            return c;
        if (t < 0.45)
            return glide(c, e, (t - 0.4) / 0.05);
        if (t < 0.8)
            return e;
        if (t < 0.85)
            return glide(e, g, (t - 0.8) / 0.05);
        return g;
    };
    const auto audio = synthesise(Voice{hz}, 1.25);
    const auto a = analyse({audio}, rate);
    for (const auto& n : a.notes)
        UNSCOPED_INFO("note " << n.start / rate << " .. " << n.end / rate << " at " << n.centre);
    REQUIRE(a.notes.size() == 3);
    CHECK(std::abs(a.notes[0].centre - 6000.0f) < 10.0f);
    CHECK(std::abs(a.notes[1].centre - 6400.0f) < 10.0f);
    CHECK(std::abs(a.notes[2].centre - 6700.0f) < 10.0f);
    CHECK(std::abs(static_cast<double>(a.notes[1].start) / rate - 0.425) < 0.04);
    CHECK(std::abs(static_cast<double>(a.notes[2].start) / rate - 0.825) < 0.04);
    // Joined, with transitions either side of each join.
    CHECK(a.notes[0].end == a.notes[1].start);
    CHECK(a.notes[0].transitionOut > 0);
    CHECK(a.notes[1].transitionIn > 0);
}

TEST_CASE("Pitch analysis finds a breath between phrases", "[engine][pitch]")
{
    Voice v;
    v.hz = [](double) { return 196.0; };
    v.voiced = [](double t) { return t < 0.5 || t >= 1.3; };
    v.noise = [](double t) { return t >= 0.85 && t < 1.2 ? 0.01 : 0.0; };
    const auto audio = synthesise(v, 1.8);
    const auto a = analyse({audio}, rate);
    REQUIRE(a.notes.size() == 2);
    const core::VocalSegment* breath = nullptr;
    for (const auto& s : a.segments)
        if (s.kind == core::VocalSegmentKind::breath)
            breath = &s;
    REQUIRE(breath != nullptr);
    CHECK(std::abs(static_cast<double>(breath->start) / rate - 0.85) < 0.05);
    CHECK(std::abs(static_cast<double>(breath->end) / rate - 1.2) < 0.05);
    // Notes and segments tile the media.
    std::vector<std::pair<std::int64_t, std::int64_t>> pieces;
    for (const auto& n : a.notes)
        pieces.emplace_back(n.start, n.end);
    for (const auto& s : a.segments)
        pieces.emplace_back(s.start, s.end);
    std::sort(pieces.begin(), pieces.end());
    CHECK(pieces.front().first == 0);
    CHECK(pieces.back().second == a.length);
    for (size_t i = 0; i + 1 < pieces.size(); ++i)
        CHECK(pieces[i].second == pieces[i + 1].first);
}

TEST_CASE("An analysis survives its cache file", "[engine][pitch]")
{
    const auto a = analyse({steadyA3()}, rate);
    PitchAnalysis b;
    REQUIRE(PitchAnalysis::deserialise(a.serialise(), b));
    CHECK(b.cents == a.cents);
    CHECK(b.marks == a.marks);
    CHECK(b.notes == a.notes);
    CHECK(b.segments == a.segments);
    CHECK_FALSE(PitchAnalysis::deserialise("not an analysis", b));
}

TEST_CASE("An unedited vocal renders nothing and changes no sample", "[engine][pitch]")
{
    const auto a = analyse({steadyA3()}, rate);
    auto edit = a.initialEdit();
    CHECK_FALSE(edit.changesSound());
    CHECK(planSpans(a, edit, 1).empty());
    CHECK(render(steadyA3(), a, edit) == steadyA3());
}

TEST_CASE("A gain-only edit leaves the voiced samples bit-identical", "[engine][pitch]")
{
    Voice v;
    v.hz = [](double) { return 196.0; };
    v.voiced = [](double t) { return t < 0.5 || t >= 1.3; };
    v.noise = [](double t) { return t >= 0.85 && t < 1.2 ? 0.01 : 0.0; };
    const auto audio = synthesise(v, 1.8);
    const auto a = analyse({audio}, rate);
    auto edit = a.initialEdit();
    for (auto& s : edit.segments)
        if (s.kind == core::VocalSegmentKind::breath)
            s.muted = true;
    const auto out = render(audio, a, edit);
    for (const auto& n : a.notes)
        for (auto i = n.start; i < n.end; ++i)
            REQUIRE(out[static_cast<size_t>(i)] == audio[static_cast<size_t>(i)]);
    // The breath is gone apart from its fades.
    double before = 0.0, after = 0.0;
    for (size_t i = static_cast<size_t>(0.9 * rate); i < static_cast<size_t>(1.15 * rate); ++i)
    {
        before += audio[i] * audio[i];
        after += out[i] * out[i];
    }
    CHECK(after < before * 1.0e-6);
}

TEST_CASE("Moving a note renders its new pitch and keeps the formants", "[engine][pitch]")
{
    const auto& audio = steadyA3();
    const auto a = analyse({audio}, rate);
    auto edit = a.initialEdit();
    REQUIRE(edit.notes.size() == 1);
    edit.notes[0].target = edit.notes[0].centre + 400.0f; // up a major third
    const auto out = render(audio, a, edit);
    const auto b = analyse({out}, rate);
    CHECK(std::abs(medianCents(b, 0.15, 0.85) - (centsOf(220.0) + 400.0)) < 3.0);

    // The harmonics of the new pitch sit under the same envelope (the formants
    // stayed), not under the envelope moved up with them (a resample).
    const double f0 = 220.0 * std::exp2(400.0 / 1200.0);
    std::vector<double> measured, kept, moved;
    for (int k = 1; k * f0 < 3500.0; ++k)
    {
        measured.push_back(std::log(amplitudeAt(out, 20000, 8192, k * f0) + 1.0e-9));
        kept.push_back(std::log(envelope(k * f0) / k));
        moved.push_back(std::log(envelope(k * 220.0) / k));
    }
    CHECK(correlation(measured, kept) > 0.9);
    CHECK(correlation(measured, kept) > correlation(measured, moved) + 0.05);

    // No clicks: no step much larger than the voice's own.
    double ownStep = 0.0, step = 0.0;
    for (size_t i = 1; i < audio.size(); ++i)
    {
        ownStep = std::max(ownStep, static_cast<double>(std::abs(audio[i] - audio[i - 1])));
        step = std::max(step, static_cast<double>(std::abs(out[i] - out[i - 1])));
    }
    CHECK(step < 2.0 * ownStep);
}

TEST_CASE("Vibrato and drift amounts scale what was sung", "[engine][pitch]")
{
    const auto hz = [](double t) { return 220.0 * std::exp2(40.0 * std::sin(2.0 * pi * 5.5 * t) / 1200.0); };
    const auto audio = synthesise(Voice{hz}, 1.2);
    const auto a = analyse({audio}, rate);
    REQUIRE(a.notes.size() == 1);
    const double sung = spreadCents(a, 0.2, 1.0);
    CHECK(sung > 20.0);

    auto flat = a.initialEdit();
    flat.notes[0].vibrato = 0.0f;
    flat.notes[0].drift = 0.0f;
    const auto b = analyse({render(audio, a, flat)}, rate);
    CHECK(spreadCents(b, 0.2, 1.0) < 0.3 * sung);
    CHECK(std::abs(medianCents(b, 0.2, 1.0) - a.notes[0].centre) < 5.0);

    auto kept = a.initialEdit();
    kept.notes[0].target += 100.0f;
    const auto c = analyse({render(audio, a, kept)}, rate);
    CHECK(std::abs(spreadCents(c, 0.2, 1.0) - sung) < 0.25 * sung);
}

TEST_CASE("A transition carries the slide between two moved notes", "[engine][pitch]")
{
    const auto hz = [](double t)
    {
        if (t < 0.5)
            return 220.0;
        if (t < 0.56)
            return 220.0 * std::pow(246.94 / 220.0, (t - 0.5) / 0.06);
        return 246.94;
    };
    const auto audio = synthesise(Voice{hz}, 1.0);
    const auto a = analyse({audio}, rate);
    REQUIRE(a.notes.size() == 2);
    auto edit = a.initialEdit();
    edit.notes[0].target += 100.0f;
    edit.notes[1].target -= 100.0f; // the two notes now sit a unison apart... nearly
    REQUIRE(edit.notes[0].end == edit.notes[1].start);
    const auto shift = shiftCurve(a, edit);
    // The shift glides from one note's to the other's: never a step.
    for (size_t f = 1; f < shift.size(); ++f)
        if (a.cents[f] > 0.0f && a.cents[f - 1] > 0.0f)
        {
            INFO("frame " << f << " of " << shift.size());
            CHECK(std::abs(shift[f] - shift[f - 1]) < 40.0f);
        }
}

TEST_CASE("Correct to key moves notes part way and leaves close ones alone", "[engine][pitch]")
{
    const auto note = [](float centre, double seconds, core::Id id)
    {
        core::VocalNote n;
        n.id = id;
        n.start = static_cast<std::int64_t>(id) * 48000;
        n.end = n.start + static_cast<std::int64_t>(seconds * rate);
        n.centre = centre;
        n.target = centre;
        return n;
    };
    const core::MusicalKey cMajor{0, core::ScaleKind::major};
    std::vector<core::VocalNote> notes{
        note(6005.0f, 0.5, 1),  // C4 + 5: inside the tolerance
        note(6240.0f, 0.5, 2),  // D4 + 40: moves 85% of the way
        note(6435.0f, 0.06, 3), // E4 + 35, an ornament: moves much less
        note(6604.0f, 0.5, 4),  // F#4 sung in tune: chromatic, and it stays
    };
    const auto r = correctToKey(notes, cMajor, rate);
    CHECK(r.notes[0].target == 6005.0f);
    CHECK(r.notes[1].target == Catch::Approx(6240.0f - 40.0f * 0.85f));
    CHECK(r.notes[1].drift == Catch::Approx(0.6f));
    CHECK(r.notes[1].vibrato == 1.0f);
    CHECK(r.notes[2].target > 6420.0f);
    CHECK(r.notes[2].target < 6435.0f);
    CHECK(r.notes[2].drift == 1.0f);
    CHECK(r.notes[3].target == 6604.0f);
    CHECK(r.flagged.empty());

    // D4 + 60: the key wins over D#, but that is farther than allowed: flagged.
    CorrectionOptions strict;
    strict.maxShiftCents = 50.0f;
    const auto far = correctToKey({note(6260.0f, 0.5, 5)}, cMajor, rate, strict);
    CHECK(far.notes[0].target == 6260.0f);
    REQUIRE(far.flagged.size() == 1);
    CHECK(far.flagged[0] == 5);

    // F4 sung 30 cents flat in E minor (no F there): still an F, not pulled to E.
    const core::MusicalKey eMinor{4, core::ScaleKind::minor};
    CHECK(correctToKey({note(6470.0f, 0.5, 6)}, eMinor, rate).notes[0].target > 6490.0f);
}

TEST_CASE("Correct to key keeps the melody's direction", "[engine][pitch]")
{
    const auto note = [](float centre, core::Id id)
    {
        core::VocalNote n;
        n.id = id;
        n.start = static_cast<std::int64_t>(id) * 24000;
        n.end = n.start + 24000;
        n.centre = n.target = centre;
        return n;
    };
    // A minor pentatonic (A C D E G). C4 + 30, then a rise of 70 cents to C4 + 100
    // (C#, not in the key): nearer to C than to D, but the singer was going up.
    const core::MusicalKey aMinorPentatonic{9, core::ScaleKind::minorPentatonic};
    CorrectionOptions options;
    options.strength = 1.0f;
    options.maxShiftCents = 200.0f;
    const auto r = correctToKey({note(6030.0f, 1), note(6098.0f, 2)}, aMinorPentatonic, rate, options);
    CHECK(r.notes[0].target == 6000.0f);
    CHECK(r.notes[1].target > r.notes[0].target);
    const core::MusicalKey cMajor{0, core::ScaleKind::major};
    // A pitch sung twice lands on one target.
    const auto same = correctToKey({note(6452.0f, 1), note(6448.0f, 2)}, cMajor, rate, options);
    CHECK(same.notes[0].target == same.notes[1].target);
}

TEST_CASE("Correct to key can prefer chord tones", "[engine][pitch]")
{
    core::VocalNote n;
    n.id = 1;
    n.end = 24000;
    n.centre = n.target = 6450.0f; // between E4 and F4
    const core::MusicalKey cMajor{0, core::ScaleKind::major};
    CorrectionOptions options;
    options.chordAware = true;
    options.strength = 1.0f;
    const std::uint16_t fMajorChord = (1u << 5) | (1u << 9) | (1u << 0);
    const std::uint16_t cMajorChord = (1u << 0) | (1u << 4) | (1u << 7);
    CHECK(correctToKey({n}, cMajor, rate, options, {fMajorChord}).notes[0].target == 6500.0f);
    CHECK(correctToKey({n}, cMajor, rate, options, {cMajorChord}).notes[0].target == 6400.0f);
}

TEST_CASE("Key detection finds a minor key from the notes", "[engine][pitch]")
{
    // A minor: A and E long, C and the rest shorter.
    std::vector<core::VocalNote> notes;
    const std::pair<int, double> melody[] = {{57, 1.0},
                                             {60, 0.5},
                                             {64, 1.0},
                                             {62, 0.4},
                                             {60, 0.5},
                                             {59, 0.3},
                                             {57, 1.2},
                                             {64, 0.8},
                                             {65, 0.3},
                                             {64, 0.5},
                                             {57, 1.0},
                                             {69, 0.6}};
    std::int64_t at = 0;
    for (const auto& [midi, seconds] : melody)
    {
        core::VocalNote n;
        n.start = at;
        n.end = at + static_cast<std::int64_t>(seconds * rate);
        n.centre = static_cast<float>(midi * 100 + 8);
        notes.push_back(n);
        at = n.end;
    }
    const auto key = detectKey(notes);
    CHECK(key.root == 9);
    CHECK(key.scale == core::ScaleKind::minor);
}

TEST_CASE("Keys know their scales", "[core][pitch]")
{
    const core::MusicalKey d{2, core::ScaleKind::major};
    CHECK(d.contains(2));
    CHECK(d.contains(6)); // F#
    CHECK_FALSE(d.contains(5));
    CHECK(d.contains(1)); // C#
    const core::MusicalKey none;
    CHECK(none.contains(3));
    CHECK(core::MusicalKey{9, core::ScaleKind::minor}.mask() ==
          core::MusicalKey{0, core::ScaleKind::major}.mask());
}

TEST_CASE("Vocal edits and the key survive a save and an undo", "[core][pitch]")
{
    core::Project p;
    core::Track t;
    t.id = p.allocateId();
    core::AudioClip c;
    c.id = p.allocateId();
    c.mediaPath = "voice.wav";
    c.length = 48000;
    t.clips.push_back(c);
    p.tracks.push_back(t);

    core::VocalEdit edit;
    edit.analysisVersion = 1;
    core::VocalNote n;
    n.start = 100;
    n.end = 20000;
    n.centre = 6012.0f;
    n.target = 6000.0f;
    n.drift = 0.6f;
    n.transitionOut = 480;
    edit.notes.push_back(n);
    core::VocalSegment s;
    s.start = 20000;
    s.end = 30000;
    s.kind = core::VocalSegmentKind::breath;
    s.muted = true;
    edit.segments.push_back(s);

    core::UndoStack undo;
    undo.execute(p, std::make_unique<core::SetVocalEdit>(t.id, c.id, edit, "Correct pitch"));
    undo.execute(p, std::make_unique<core::SetProjectKey>(core::MusicalKey{7, core::ScaleKind::dorian}));
    const auto& stored = p.tracks[0].clips[0].vocal;
    REQUIRE(stored.notes.size() == 1);
    CHECK(stored.notes[0].id != core::invalidId);
    CHECK(stored.segments[0].id != core::invalidId);

    const auto back = core::projectFromJson(core::projectToJson(p));
    REQUIRE(back.has_value());
    CHECK(back->tracks[0].clips[0].vocal == stored);
    CHECK(back->key == p.key);

    undo.undo(p);
    undo.undo(p);
    CHECK_FALSE(p.tracks[0].clips[0].vocal.active());
    CHECK_FALSE(p.key.isSet());
    undo.redo(p);
    CHECK(p.tracks[0].clips[0].vocal == back->tracks[0].clips[0].vocal);
}

TEST_CASE("Splitting a vocal clip keeps the edit on both halves", "[core][pitch]")
{
    core::Project p;
    core::Track t;
    t.id = p.allocateId();
    core::AudioClip c;
    c.id = p.allocateId();
    c.length = 48000;
    c.vocal.analysisVersion = 1;
    c.vocal.notes.push_back(core::VocalNote{});
    t.clips.push_back(c);
    p.tracks.push_back(t);
    core::SplitClip split(t.id, c.id, 24000);
    split.apply(p);
    REQUIRE(p.tracks[0].clips.size() == 2);
    CHECK(p.tracks[0].clips[1].vocal == c.vocal);
    CHECK(core::JoinClips::canJoin(p.tracks[0].clips[0], p.tracks[0].clips[1]));
}

TEST_CASE("The vocal service analyses, renders and serves edited clips", "[engine][pitch]")
{
    ToneLoader loader;
    loader.files["voice"] = steadyA3();
    VocalService service(loader);
    std::atomic<int> changes{0};
    service.onChanged = [&changes] { ++changes; };

    core::AudioClip clip;
    clip.id = 5;
    clip.mediaPath = "voice";
    clip.length = static_cast<std::int64_t>(steadyA3().size());
    const auto base = std::make_shared<MemoryAudioSource>(std::vector<std::vector<float>>{steadyA3()}, rate);

    service.analysis("voice", rate); // starts it
    service.waitUntilIdle();
    const auto a = service.analysis("voice", rate);
    REQUIRE(a != nullptr);
    CHECK(changes.load() >= 1);

    clip.vocal = a->initialEdit();
    CHECK(service.sourceFor(clip, rate, base) == base); // nothing changes the sound
    clip.vocal.notes[0].target += 200.0f;
    const auto pending = service.sourceFor(clip, rate, base);
    CHECK(pending != nullptr);
    service.waitUntilIdle();
    const auto ready =
        std::dynamic_pointer_cast<const PitchEditedSource>(service.sourceFor(clip, rate, base));
    REQUIRE(ready != nullptr);
    REQUIRE(ready->spans().size() == 1);

    // Reading through the source gives the rendered samples, on a real-time thread.
    std::vector<float> through(steadyA3().size());
    {
        rt::ScopedRealtimeSection section;
        for (size_t at = 0; at < through.size(); at += 512)
            ready->read(0,
                        static_cast<std::int64_t>(at),
                        through.data() + at,
                        static_cast<int>(std::min<size_t>(512, through.size() - at)));
    }
    CHECK(through == render(steadyA3(), *a, clip.vocal));
    // The media was loaded once.
    CHECK(loader.loads == 1);
}

TEST_CASE("A synchronous vocal service renders inside the call", "[engine][pitch]")
{
    ToneLoader loader;
    loader.files["voice"] = steadyA3();
    VocalService service(loader, true);
    const auto a = service.analysis("voice", rate);
    REQUIRE(a != nullptr);
    core::AudioClip clip;
    clip.id = 1;
    clip.mediaPath = "voice";
    clip.vocal = a->initialEdit();
    clip.vocal.notes[0].muted = true;
    const auto base = std::make_shared<MemoryAudioSource>(std::vector<std::vector<float>>{steadyA3()}, rate);
    const auto source =
        std::dynamic_pointer_cast<const PitchEditedSource>(service.sourceFor(clip, rate, base));
    REQUIRE(source != nullptr);
    CHECK(source->spans().size() == 1);
}

TEST_CASE("The media cache passes vocal clips through its hook", "[engine][pitch]")
{
    ToneLoader loader;
    loader.files["voice"] = steadyA3();
    MediaCache cache(loader);
    int calls = 0;
    cache.setClipSourceHook(
        [&calls](const core::AudioClip&, double, std::shared_ptr<const AudioSource> base)
        {
            ++calls;
            return base;
        });
    core::AudioClip plain;
    plain.id = 1;
    plain.mediaPath = "voice";
    CHECK(cache.sourceForClip(plain, rate) != nullptr);
    CHECK(calls == 0);
    auto vocal = plain;
    vocal.id = 2;
    vocal.vocal.analysisVersion = 1;
    CHECK(cache.sourceForClip(vocal, rate) != nullptr);
    CHECK(calls == 1);
}

TEST_CASE("Splitting and merging notes keeps their shift", "[engine][pitch]")
{
    const auto a = analyse({steadyA3()}, rate);
    auto edit = a.initialEdit();
    edit.notes[0].id = 1;
    edit.notes[0].target += 50.0f;
    const auto end = edit.notes[0].end;
    CHECK_FALSE(splitNote(edit, a, 1, edit.notes[0].start + 100)); // too close to the start
    REQUIRE(splitNote(edit, a, 1, 24000));
    REQUIRE(edit.notes.size() == 2);
    CHECK(edit.notes[0].end == 24000);
    CHECK(edit.notes[1].start == 24000);
    CHECK(edit.notes[1].end == end);
    CHECK(edit.notes[1].id == 0);
    CHECK(edit.notes[1].shift() == Catch::Approx(50.0f));
    CHECK(std::abs(edit.notes[1].centre - centsOf(220.0)) < 3.0);
    CHECK(edit.notes[0].transitionOut > 0);
    // Splitting a note that was moved never makes a step in the shift.
    const auto shift = shiftCurve(a, edit);
    CHECK(shift[100] == Catch::Approx(50.0f));
    REQUIRE(mergeWithNext(edit, a, 1));
    REQUIRE(edit.notes.size() == 1);
    CHECK(edit.notes[0].end == end);
    CHECK(edit.notes[0].shift() == Catch::Approx(50.0f));
}

TEST_CASE("Breath and pitch helpers change what they say", "[engine][pitch]")
{
    core::VocalEdit edit;
    edit.analysisVersion = 1;
    core::VocalNote n;
    n.id = 3;
    n.centre = 6012.0f;
    n.target = 6040.0f;
    n.drift = 0.5f;
    n.gainDb = -3.0f;
    edit.notes.push_back(n);
    core::VocalSegment b;
    b.kind = core::VocalSegmentKind::breath;
    core::VocalSegment c;
    c.kind = core::VocalSegmentKind::consonant;
    edit.segments = {b, c};
    setSegments(edit, core::VocalSegmentKind::breath, -12.0f, false);
    CHECK(edit.segments[0].gainDb == -12.0f);
    CHECK(edit.segments[1].gainDb == 0.0f);
    snapToSemitones(edit, {3});
    CHECK(edit.notes[0].target == 6000.0f);
    resetPitch(edit, {3});
    CHECK(edit.notes[0].target == 6012.0f);
    CHECK(edit.notes[0].drift == 1.0f);
    CHECK(edit.notes[0].gainDb == -3.0f);
}

TEST_CASE("The engine plays a vocal edit at its new pitch, offset into the media", "[engine][pitch]")
{
    const plugins::BuiltInFactory builtIns;
    ToneLoader loader;
    loader.files["voice"] = steadyA3();
    VocalService vocal(loader, true);
    AudioEngine engine(loader, builtIns, 0);
    engine.setClipSourceHook(
        [&vocal](const core::AudioClip& clip, double sampleRate, std::shared_ptr<const AudioSource> base)
        { return vocal.sourceFor(clip, sampleRate, std::move(base)); });
    engine.prepare(rate, 256);

    const auto a = vocal.analysis("voice", rate);
    REQUIRE(a != nullptr);
    core::Project p;
    core::Track t;
    t.id = 1;
    core::AudioClip clip{2, "voice", "voice", 4800, 38400, 4800, 1.0f};
    clip.vocal = a->initialEdit();
    clip.vocal.notes[0].target -= 300.0f;
    t.clips.push_back(clip);
    p.tracks.push_back(t);
    p.nextId = 3;
    engine.setProject(p);
    engine.play();

    std::vector<float> left(256), right(256), out;
    float* outputs[2] = {left.data(), right.data()};
    while (engine.position() < 48000)
    {
        engine.process(outputs, 2, 256);
        out.insert(out.end(), left.begin(), left.end());
    }
    const auto b = analyse({out}, rate);
    CHECK(std::abs(medianCents(b, 0.25, 0.8) - (centsOf(220.0) - 300.0)) < 3.0);
}
