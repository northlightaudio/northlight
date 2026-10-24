#pragma once

// Support for the UI performance tests (UiWorkBudgetTests.cpp, UiPaintTimeTests.cpp):
// a recorder of what a component repaints, the cards to measure, and the
// interactions a user makes with them, driven through the components' own
// handlers. No window is needed: a component reports its repaints to its cached
// image before its parent, which is where the recorder listens.

#include "core/Command.h"
#include "core/Model.h"
#include "plugins/BuiltIns.h"
#include "ui/PaintStats.h"
#include "ui/Theme.h"
#include "ui/components/Knob.h"
#include "ui/strip/BuiltInCards.h"
#include "ui/strip/ModuleCard.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <functional>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

namespace northlight::uiperf
{

// Records every area a component (or anything inside it) asks to repaint, in
// its own pixels. Attached as the component's cached image, which the component
// owns.
class RepaintRecorder final : public juce::CachedComponentImage
{
public:
    juce::RectangleList<int> area;
    bool everything = false;

    void paint(juce::Graphics&) override {}
    bool invalidateAll() override
    {
        everything = true;
        area.add(bounds_);
        return false;
    }
    bool invalidate(const juce::Rectangle<int>& r) override
    {
        area.add(r.getIntersection(bounds_));
        return false;
    }
    void releaseResources() override {}

    void clear()
    {
        area.clear();
        everything = false;
    }
    void setBounds(juce::Rectangle<int> b) { bounds_ = b; }

private:
    juce::Rectangle<int> bounds_;
};

// The share of `bounds` a list of rectangles covers (overlaps counted once).
inline double coverage(juce::RectangleList<int> list, juce::Rectangle<int> bounds)
{
    list.consolidate();
    list.clipTo(bounds);
    double covered = 0.0;
    for (const auto& r : list)
        covered += static_cast<double>(r.getWidth()) * r.getHeight();
    const double total = static_cast<double>(bounds.getWidth()) * bounds.getHeight();
    return total > 0.0 ? covered / total : 0.0;
}

// Every built-in with a card or an editor: the effects, then the instruments.
inline std::vector<std::string> builtInTypes()
{
    return {
        plugins::types::gain,   plugins::types::eq,    plugins::types::compressor, plugins::types::opto,
        "northlight.glue",      "northlight.widener",  "northlight.patina",        "northlight.exciter",
        "northlight.bloom",     "northlight.amp",      "northlight.gravity",       "northlight.flare",
        "northlight.comet",     "northlight.tide",     "northlight.polaris",       plugins::types::limiter,
        plugins::types::reverb, plugins::types::delay, "northlight.tapeecho",      "northlight.aurora",
        "northlight.pulsar",    "northlight.nebula",   "northlight.starbit",       "northlight.sampler"};
}

// A card with its processor live, playing a tone into it, at its preferred
// size and visible (so its repaints are reported).
class CardUnderTest
{
public:
    explicit CardUnderTest(const std::string& type)
    {
        live_ = plugins::BuiltInFactory().create(type);
        if (live_ != nullptr)
        {
            live_->prepare(48000.0, block);
            live_->setBypassed(false);
        }
        insert_.id = 7;
        insert_.type = type;
        insert_.params = plugins::defaultParameters(type);
        if (live_ != nullptr)
            for (size_t i = 0; i < insert_.params.size(); ++i)
                live_->setParameter(static_cast<int>(i), insert_.params[i]);
        host_.liveInsert = [this](core::Id) { return live_; };
        // As the engine does: a parameter set reaches the processor.
        host_.onCommand = [this](std::unique_ptr<core::Command> c)
        {
            if (auto* set = dynamic_cast<core::SetInsertParameter*>(c.get());
                set != nullptr && live_ != nullptr)
                live_->setParameter(set->parameter(), set->value());
        };
        host_.onGestureBegin = [](const std::string&) {};
        host_.onGestureEnd = [] {};
        card_ = ui::strip::createBuiltInCard(ui::Theme::dark(), host_, core::masterId, insert_);
        if (card_ == nullptr)
            return;
        card_->setBounds(0,
                         0,
                         card_->preferredWidth(),
                         card_->preferredBodyHeight() + ui::strip::ModuleCard::headerHeight);
        card_->setVisible(true);
        card_->tick(16.0); // starts watching its processor
        for (int i = 0; i < 20; ++i)
            frame();
    }

    ~CardUnderTest() { detach(); }

    ui::strip::ModuleCard* card() const noexcept { return card_.get(); }

    // One display frame: a block of the tone through the processor, then a tick.
    void frame()
    {
        if (live_ != nullptr)
        {
            std::vector<float> l(block), r(block);
            for (int i = 0; i < block; ++i)
            {
                l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] =
                    0.3f * static_cast<float>(std::sin(phase_));
                phase_ += 2.0 * std::numbers::pi * 220.0 / 48000.0;
            }
            live_->process(l.data(), r.data(), block);
        }
        card_->tick(1000.0 / 60.0);
    }

    // Starts recording what repaints.
    RepaintRecorder& record()
    {
        if (recorder_ == nullptr)
        {
            recorder_ = new RepaintRecorder();
            recorder_->setBounds(card_->getLocalBounds());
            card_->setCachedComponentImage(recorder_); // the card owns it
        }
        recorder_->clear();
        return *recorder_;
    }

    // Paints the card at `scale` (into a reused image), clipped to `region`, and
    // returns the words its faces set while doing so. The recorder stays: a
    // direct paintEntireComponent never asks the cached image to paint.
    std::uint64_t paint(const juce::RectangleList<int>& region, float scale = 2.0f)
    {
        const int w = juce::roundToInt(static_cast<float>(card_->getWidth()) * scale);
        const int h = juce::roundToInt(static_cast<float>(card_->getHeight()) * scale);
        if (image_.getWidth() != w || image_.getHeight() != h)
            image_ = juce::Image(juce::Image::ARGB, w, h, true);
        if (region.isEmpty())
            return 0;
        const auto before = ui::paintstats::counters().wordsSet;
        {
            juce::Graphics g(image_);
            g.addTransform(juce::AffineTransform::scale(scale));
            g.reduceClipRegion(region);
            card_->paintEntireComponent(g, false);
        }
        return ui::paintstats::counters().wordsSet - before;
    }
    std::uint64_t paintAll(float scale = 2.0f)
    {
        return paint(juce::RectangleList<int>(card_->getLocalBounds()), scale);
    }

    // The knobs a user can reach: every visible one.
    std::vector<ui::Knob*> knobs() const
    {
        std::vector<ui::Knob*> out;
        std::function<void(juce::Component&)> walk = [&](juce::Component& c)
        {
            for (auto* child : c.getChildren())
            {
                if (!child->isVisible())
                    continue;
                if (auto* k = dynamic_cast<ui::Knob*>(child))
                    out.push_back(k);
                walk(*child);
            }
        };
        walk(*card_);
        return out;
    }

    // A knob's bounds in the card's pixels.
    juce::Rectangle<int> boundsOf(const juce::Component& c) const
    {
        return card_->getLocalArea(&c, c.getLocalBounds());
    }

private:
    void detach()
    {
        if (recorder_ != nullptr && card_ != nullptr)
        {
            card_->setCachedComponentImage(nullptr); // deletes it
            recorder_ = nullptr;
        }
    }

    static constexpr int block = 800; // a 60 Hz frame of audio at 48 kHz
    std::shared_ptr<engine::InsertProcessor> live_;
    core::Insert insert_;
    ui::strip::CardHost host_;
    std::unique_ptr<ui::strip::ModuleCard> card_;
    RepaintRecorder* recorder_ = nullptr;
    juce::Image image_;
    double phase_ = 0.0;
};

// A mouse event over `c` (the mouse entering, leaving or hovering it).
inline juce::MouseEvent mouseOver(juce::Component& c)
{
    const auto centre = c.getLocalBounds().getCentre().toFloat();
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),
                            centre,
                            juce::ModifierKeys(),
                            0.0f,
                            0.0f,
                            0.0f,
                            0.0f,
                            0.0f,
                            &c,
                            &c,
                            now,
                            centre,
                            now,
                            0,
                            false);
}

} // namespace northlight::uiperf
