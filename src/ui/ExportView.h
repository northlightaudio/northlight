#pragma once

#include "engine/ExportSettings.h"
#include "ui/Theme.h"
#include "ui/components/SegmentedControl.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace aural::ui
{

// The body of the export sheet: what to render (the mix, every track's stem or
// the selected tracks'), the range (project, loop, a marker region), the format
// and bit depth (or the Vorbis quality), the sample rate, dither, and loudness
// normalisation with its peak ceiling. Choices that do not apply are disabled
// (dither on a float or lossy file, the ceiling with normalisation off), and a
// line under the rows says what the file will be. The owner reads settings()
// when the sheet's Export button is pressed.
class ExportView final : public juce::Component
{
public:
    struct Context
    {
        bool hasLoop = false;
        std::vector<juce::String> regions; // marker region names, in timeline order
        int selectedTracks = 0;
        double projectRate = 48000.0;
        juce::StringArray oggQualities; // the Vorbis encoder's quality names
    };

    ExportView(const Theme& theme, const engine::ExportSettings& initial, Context context);

    engine::ExportSettings settings() const;
    // With the region range: the index into Context::regions.
    int region() const noexcept { return region_; }

    static int preferredHeight(const Theme& theme);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void fillRows();
    void update(); // enables what applies, rewrites the summary
    juce::String summary() const;

    const Theme& theme_;
    Context context_;
    engine::ExportSettings settings_;
    int region_ = 0;

    SegmentedControl source_, format_, peakMode_;
    juce::ComboBox range_, depth_, rate_, dither_, loudness_, ceiling_;
    std::vector<engine::ExportDepth> depths_; // what depth_'s items are
};

} // namespace aural::ui
