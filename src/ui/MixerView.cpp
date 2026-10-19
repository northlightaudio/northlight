#include "ui/MixerView.h"

#include "engine/Snapshot.h"
#include "hosting/HostedPlugin.h"
#include "plugins/BuiltIns.h"
#include "ui/InsertEditor.h"

#include <cmath>
#include <cstdint>
#include <iterator>
#include <string>

namespace aural::ui
{

namespace
{
constexpr double minFaderDb = -60.0;
constexpr double maxFaderDb = 6.0; // matches core::maxChannelGain

// Colours handed to new fader groups in turn.
constexpr std::uint32_t groupColours[] = {
    0xff4f8fe8u, 0xffe8a84fu, 0xff5fc96fu, 0xffd65f9eu, 0xff9b6fe8u, 0xff4fc9c9u};
constexpr int meterHz = 30;

float dbToGain(double db) noexcept
{
    return db <= minFaderDb + 0.05 ? 0.0f : static_cast<float>(std::pow(10.0, db / 20.0));
}

double gainToDb(float gain) noexcept
{
    return gain <= 0.0f ? minFaderDb : juce::jlimit(minFaderDb, maxFaderDb, 20.0 * std::log10(gain));
}

juce::String dbText(double db)
{
    return db <= minFaderDb + 0.05 ? juce::String("-inf") : juce::String(db, 1);
}

// A stereo bar meter with peak hold and a slow fall, fed from MeterBlock readings.
class StereoMeter final : public juce::Component
{
public:
    explicit StereoMeter(const Theme& theme) : theme_(theme) {}

    void push(const rt::MeterBlock::Reading& r)
    {
        const auto decay = [](float& level, float target)
        { level = target > level ? target : level * 0.85f; };
        decay(rmsL_, r.rmsLeft);
        decay(rmsR_, r.rmsRight);
        decay(peakL_, r.peakLeft);
        decay(peakR_, r.peakRight);
        if (r.peakLeft >= 1.0f || r.peakRight >= 1.0f)
            clipFrames_ = meterHz;
        else if (clipFrames_ > 0)
            --clipFrames_;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        auto bounds = getLocalBounds().toFloat();
        g.setColour(c.surfaceRaised);
        g.fillRoundedRectangle(bounds, 2.0f);
        const float gap = 2.0f;
        auto left = bounds.removeFromLeft(bounds.getWidth() * 0.5f - gap * 0.5f);
        bounds.removeFromLeft(gap);
        drawBar(g, left, rmsL_, peakL_);
        drawBar(g, bounds, rmsR_, peakR_);
        if (clipFrames_ > 0)
        {
            g.setColour(c.meterClip);
            g.fillRect(getLocalBounds().removeFromTop(4));
        }
    }

private:
    static float fraction(float level)
    {
        const double db = level > 0.0f ? 20.0 * std::log10(level) : -100.0;
        return static_cast<float>(juce::jlimit(0.0, 1.0, (db + 60.0) / 66.0));
    }

    void drawBar(juce::Graphics& g, juce::Rectangle<float> area, float rms, float peak)
    {
        const auto& c = theme_.colours;
        const float h = area.getHeight();
        const auto rmsFraction = fraction(rms);
        auto fill = area.withTop(area.getBottom() - h * rmsFraction);
        g.setColour(rms > 0.5f ? c.meterHot : c.meter);
        g.fillRect(fill);
        const auto peakY = area.getBottom() - h * fraction(peak);
        g.setColour(peak > 0.5f ? c.meterHot : c.meter);
        g.fillRect(area.getX(), peakY - 1.0f, area.getWidth(), 2.0f);
        // 0 dBFS line (at 60/66 of the height).
        g.setColour(c.borderStrong);
        const float zeroY = area.getBottom() - h * (60.0f / 66.0f);
        g.fillRect(area.getX(), zeroY, area.getWidth(), 1.0f);
    }

    const Theme& theme_;
    float rmsL_ = 0.0f, rmsR_ = 0.0f, peakL_ = 0.0f, peakR_ = 0.0f;
    int clipFrames_ = 0;
};
} // namespace

// ----- strip --------------------------------------------------------------------------------

class MixerView::Strip final : public juce::Component
{
public:
    Strip(MixerView& owner, const Theme& theme) : owner_(owner), theme_(theme), meter_(theme)
    {
        name_.setFont(juce::FontOptions{theme_.type.caption});
        name_.setJustificationType(juce::Justification::centred);
        name_.setMinimumHorizontalScale(0.7f);
        name_.setEditable(false, true, false);
        name_.onTextChange = [this]
        {
            if (!updating_)
                owner_.onCommand(std::make_unique<core::SetTrackName>(id_, name_.getText().toStdString()));
        };
        addAndMakeVisible(name_);

        pan_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        pan_.setRange(-1.0, 1.0, 0.01);
        pan_.setDoubleClickReturnValue(true, 0.0);
        pan_.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        pan_.setTooltip("Pan");
        pan_.onValueChange = [this]
        {
            if (!updating_)
                owner_.onCommand(
                    std::make_unique<core::SetTrackPan>(id_, static_cast<float>(pan_.getValue())));
        };
        addAndMakeVisible(pan_);

        fader_.setSliderStyle(juce::Slider::LinearVertical);
        fader_.setRange(minFaderDb, maxFaderDb, 0.1);
        fader_.setSkewFactorFromMidPoint(-12.0);
        fader_.setDoubleClickReturnValue(true, 0.0);
        fader_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 18);
        fader_.textFromValueFunction = [](double v) { return dbText(v); };
        fader_.valueFromTextFunction = [](const juce::String& t)
        { return t.containsIgnoreCase("inf") ? minFaderDb : t.getDoubleValue(); };
        fader_.onValueChange = [this]
        {
            if (updating_)
                return;
            const float gain = dbToGain(fader_.getValue());
            // In a group the move is relative: every member scales by the same ratio.
            // A member sitting at -inf has no ratio, so it moves alone.
            if (group_ != core::invalidId && currentGain_ > 1.0e-6f)
                owner_.onCommand(std::make_unique<core::SetGroupGain>(group_, gain / currentGain_));
            else
                owner_.onCommand(std::make_unique<core::SetTrackGain>(id_, gain));
        };
        addAndMakeVisible(fader_);

        const auto toggle = [this](juce::TextButton& b, const char* text, juce::Colour on, auto makeCommand)
        {
            b.setButtonText(text);
            b.setClickingTogglesState(true);
            b.setColour(juce::TextButton::buttonOnColourId, on);
            b.setColour(juce::TextButton::textColourOnId, theme_.colours.accentText);
            b.onClick = [this, &b, makeCommand]
            {
                if (!updating_)
                    owner_.onCommand(makeCommand(id_, group_, b.getToggleState()));
            };
            addAndMakeVisible(b);
        };
        toggle(mute_,
               "M",
               theme_.colours.warning,
               [](core::Id id, core::Id group, bool on) -> std::unique_ptr<core::Command>
               {
                   if (group != core::invalidId)
                       return std::make_unique<core::SetGroupMute>(group, on);
                   return std::make_unique<core::SetTrackMute>(id, on);
               });
        toggle(solo_,
               "S",
               theme_.colours.success,
               [](core::Id id, core::Id group, bool on) -> std::unique_ptr<core::Command>
               {
                   if (group != core::invalidId)
                       return std::make_unique<core::SetGroupSolo>(group, on);
                   return std::make_unique<core::SetTrackSolo>(id, on);
               });
        toggle(phase_,
               "\xC3\x98",
               theme_.colours.accent,
               [](core::Id id, core::Id, bool on) -> std::unique_ptr<core::Command>
               { return std::make_unique<core::SetPhaseInvert>(id, on); });
        phase_.setButtonText(juce::String::fromUTF8("\xC3\x98"));

        output_.setTooltip("Output");
        output_.onChange = [this]
        {
            if (updating_)
                return;
            const int index = output_.getSelectedId() - 2; // id 1 = master
            const core::Id bus = index >= 0 && static_cast<size_t>(index) < busIds_.size()
                                     ? busIds_[static_cast<size_t>(index)]
                                     : core::invalidId;
            owner_.onCommand(std::make_unique<core::SetOutputBus>(id_, bus));
        };
        addAndMakeVisible(output_);

        sends_.setButtonText("Sends");
        sends_.onClick = [this] { showSendsMenu(); };
        addAndMakeVisible(sends_);
        inserts_.setButtonText("Inserts");
        inserts_.onClick = [this] { showInsertsMenu(); };
        addAndMakeVisible(inserts_);
        groupButton_.setTooltip("Fader group");
        groupButton_.onClick = [this] { showGroupMenu(); };
        addAndMakeVisible(groupButton_);
        addAndMakeVisible(meter_);
    }

    core::Id id() const noexcept { return id_; }
    int meterSlot() const noexcept { return meterSlot_; }

    void setChannel(const core::Channel& channel, bool isBus, const core::Project& project, int slot)
    {
        const juce::ScopedValueSetter<bool> guard(updating_, true);
        id_ = channel.id;
        isBus_ = isBus;
        meterSlot_ = slot;
        colour_ = channel.colour != 0 ? juce::Colour(channel.colour)
                                      : (isBus ? theme_.colours.warning : theme_.colours.accent);
        name_.setText(channel.name.empty() ? (isBus ? "Bus" : "Track") : channel.name,
                      juce::dontSendNotification);
        pan_.setValue(channel.pan, juce::dontSendNotification);
        currentGain_ = channel.gain;
        fader_.setValue(gainToDb(channel.gain), juce::dontSendNotification);
        group_ = channel.group;
        const auto* group = project.findGroup(channel.group);
        groupButton_.setButtonText(group != nullptr ? juce::String(group->name) : juce::String("Group"));
        groupButton_.setColour(juce::TextButton::buttonColourId,
                               group != nullptr && group->colour != 0
                                   ? juce::Colour(group->colour).withAlpha(0.35f)
                                   : theme_.colours.surface);
        mute_.setToggleState(channel.mute, juce::dontSendNotification);
        solo_.setToggleState(channel.solo, juce::dontSendNotification);
        phase_.setToggleState(channel.phaseInvert, juce::dontSendNotification);

        busIds_.clear();
        output_.clear(juce::dontSendNotification);
        output_.addItem("Master", 1);
        int id = 2;
        for (const auto& b : project.buses)
        {
            if (b.id == channel.id)
                continue; // a bus cannot feed itself
            busIds_.push_back(b.id);
            output_.addItem(b.name.empty() ? "Bus" : b.name, id++);
        }
        int selected = 1;
        for (size_t i = 0; i < busIds_.size(); ++i)
            if (busIds_[i] == channel.outputBus)
                selected = static_cast<int>(i) + 2;
        output_.setSelectedId(selected, juce::dontSendNotification);

        insertCount_ = static_cast<int>(channel.inserts.size());
        inserts_.setButtonText(insertCount_ > 0 ? "Inserts (" + juce::String(insertCount_) + ")" : "Inserts");
        sendCount_ = static_cast<int>(channel.sends.size());
        sends_.setButtonText(sendCount_ > 0 ? "Sends (" + juce::String(sendCount_) + ")" : "Sends");
        sends_.setEnabled(!busIds_.empty());
        repaint();
    }

    void pushMeter(const rt::MeterBlock::Reading& r) { meter_.push(r); }

    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        g.fillAll(c.surface);
        g.setColour(c.border);
        g.fillRect(getLocalBounds().removeFromRight(1));
        g.setColour(colour_);
        g.fillRect(getLocalBounds().removeFromTop(4));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(theme_.spacing.xs);
        area.removeFromTop(4);
        name_.setBounds(area.removeFromTop(18));
        area.removeFromTop(theme_.spacing.xs);
        output_.setBounds(area.removeFromTop(22));
        area.removeFromTop(theme_.spacing.xs);
        sends_.setBounds(area.removeFromTop(22));
        area.removeFromTop(theme_.spacing.xs);
        inserts_.setBounds(area.removeFromTop(22));
        area.removeFromTop(theme_.spacing.xs);
        groupButton_.setBounds(area.removeFromTop(22));
        area.removeFromTop(theme_.spacing.xs);
        pan_.setBounds(area.removeFromTop(40).withSizeKeepingCentre(40, 40));
        area.removeFromTop(theme_.spacing.xs);
        auto buttons = area.removeFromTop(22);
        const int bw = (buttons.getWidth() - 2 * theme_.spacing.xs) / 3;
        mute_.setBounds(buttons.removeFromLeft(bw));
        buttons.removeFromLeft(theme_.spacing.xs);
        solo_.setBounds(buttons.removeFromLeft(bw));
        buttons.removeFromLeft(theme_.spacing.xs);
        phase_.setBounds(buttons);
        area.removeFromTop(theme_.spacing.sm);
        meter_.setBounds(area.removeFromRight(14).withTrimmedBottom(20));
        area.removeFromRight(theme_.spacing.xs);
        fader_.setBounds(area);
    }

private:
    void showGroupMenu()
    {
        if (owner_.project_ == nullptr)
            return;
        const auto& project = *owner_.project_;
        juce::PopupMenu menu;
        std::vector<std::function<void()>> actions;
        int item = 1;
        menu.addItem(item++, "None", true, group_ == core::invalidId);
        actions.push_back(
            [this] { owner_.onCommand(std::make_unique<core::SetChannelGroup>(id_, core::invalidId)); });
        for (const auto& g : project.groups)
        {
            menu.addItem(item++, g.name, true, g.id == group_);
            const core::Id groupId = g.id;
            actions.push_back([this, groupId]
                              { owner_.onCommand(std::make_unique<core::SetChannelGroup>(id_, groupId)); });
        }
        menu.addSeparator();
        menu.addItem(item++, "New group");
        actions.push_back(
            [this]
            {
                if (owner_.project_ == nullptr)
                    return;
                const auto n = owner_.project_->groups.size();
                core::Group g;
                g.name = "Group " + std::to_string(n + 1);
                g.colour = groupColours[n % std::size(groupColours)];
                owner_.onCommand(std::make_unique<core::AddGroup>(g, std::vector<core::Id>{id_}));
            });
        if (group_ != core::invalidId)
        {
            menu.addItem(item++, "Remove group");
            const core::Id groupId = group_;
            actions.push_back([this, groupId]
                              { owner_.onCommand(std::make_unique<core::RemoveGroup>(groupId)); });
        }
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(groupButton_),
                           [actions](int chosen)
                           {
                               if (chosen > 0 && static_cast<size_t>(chosen) <= actions.size())
                                   actions[static_cast<size_t>(chosen - 1)]();
                           });
    }

    void showInsertsMenu()
    {
        if (owner_.project_ == nullptr)
            return;
        const auto* channel = owner_.project_->findChannel(id_);
        if (channel == nullptr)
            return;
        juce::PopupMenu menu;
        std::vector<std::function<void()>> actions;
        int item = 1;
        for (const auto& insert : channel->inserts)
        {
            juce::PopupMenu sub;
            sub.addItem(item++, "Edit...");
            const core::Id insertId = insert.id;
            actions.push_back([this, insertId] { owner_.openInsertEditor(id_, insertId); });
            sub.addItem(item++, "Bypass", true, insert.bypass);
            const bool bypass = insert.bypass;
            actions.push_back(
                [this, insertId, bypass]
                { owner_.onCommand(std::make_unique<core::SetInsertBypass>(id_, insertId, !bypass)); });
            const int index = channel->insertIndex(insertId);
            sub.addItem(item++, "Move up", index > 0);
            actions.push_back(
                [this, insertId, index]
                { owner_.onCommand(std::make_unique<core::MoveInsert>(id_, insertId, index - 1)); });
            sub.addItem(item++, "Move down", index + 1 < static_cast<int>(channel->inserts.size()));
            actions.push_back(
                [this, insertId, index]
                { owner_.onCommand(std::make_unique<core::MoveInsert>(id_, insertId, index + 1)); });
            // Key (sidechain) source, for inserts that take one: tracks for anyone,
            // buses too when this channel is a bus.
            const auto live = owner_.liveInsert ? owner_.liveInsert(insertId) : nullptr;
            if (live != nullptr && live->acceptsSidechain())
            {
                juce::PopupMenu key;
                key.addItem(item++, "None", true, insert.sidechain == core::invalidId);
                actions.push_back(
                    [this, insertId]
                    {
                        owner_.onCommand(
                            std::make_unique<core::SetInsertSidechain>(id_, insertId, core::invalidId));
                    });
                const auto& project = *owner_.project_;
                const auto offer = [&](const core::Channel& source, const juce::String& prefix)
                {
                    if (source.id == id_)
                        return;
                    key.addItem(item++,
                                prefix + juce::String::fromUTF8(source.name.c_str()),
                                true,
                                insert.sidechain == source.id);
                    const core::Id sourceId = source.id;
                    actions.push_back(
                        [this, insertId, sourceId]
                        {
                            owner_.onCommand(
                                std::make_unique<core::SetInsertSidechain>(id_, insertId, sourceId));
                        });
                };
                for (const auto& t : project.tracks)
                    offer(t, "");
                if (isBus_)
                    for (const auto& b : project.buses)
                        offer(b, "Bus: ");
                sub.addSubMenu("Sidechain", key);
            }
            sub.addItem(item++, "Remove");
            actions.push_back([this, insertId]
                              { owner_.onCommand(std::make_unique<core::RemoveInsert>(id_, insertId)); });
            menu.addSubMenu(juce::String::fromUTF8(owner_.insertDisplayName(insert.type).c_str()) +
                                (insert.bypass ? "  (bypassed)" : ""),
                            sub);
        }
        if (!channel->inserts.empty())
            menu.addSeparator();
        juce::PopupMenu add;
        // Built-ins first, then every scanned plugin by name, then the scanner.
        for (const auto& type : plugins::BuiltInFactory().typeNames())
        {
            add.addItem(item++, plugins::BuiltInFactory::displayName(type));
            actions.push_back(
                [this, type]
                {
                    core::Insert insert;
                    insert.type = type;
                    insert.params = plugins::defaultParameters(type);
                    owner_.onCommand(std::make_unique<core::AddInsert>(id_, insert));
                });
        }
        if (owner_.host_ != nullptr && !owner_.host_->registry().plugins().empty())
        {
            add.addSeparator();
            for (const auto& plugin : owner_.host_->registry().plugins())
            {
                add.addItem(item++,
                            juce::String::fromUTF8(plugin.name.c_str()) +
                                (plugin.manufacturer.empty()
                                     ? juce::String()
                                     : "  (" + juce::String::fromUTF8(plugin.manufacturer.c_str()) + ")"));
                const auto type = hosting::PluginRegistry::typeFor(plugin);
                actions.push_back(
                    [this, type]
                    {
                        core::Insert insert;
                        insert.type = type;
                        owner_.onCommand(std::make_unique<core::AddInsert>(id_, insert));
                    });
            }
        }
        add.addSeparator();
        add.addItem(item++, "Scan for plugins...");
        actions.push_back(
            [this]
            {
                if (owner_.onScanPlugins)
                    owner_.onScanPlugins();
            });
        menu.addSubMenu("Add", add);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(inserts_),
                           [actions](int result)
                           {
                               if (result >= 1 && static_cast<size_t>(result) <= actions.size())
                                   actions[static_cast<size_t>(result) - 1]();
                           });
    }

    void showSendsMenu()
    {
        if (owner_.project_ == nullptr)
            return;
        const auto* channel = owner_.project_->findChannel(id_);
        if (channel == nullptr)
            return;
        juce::PopupMenu menu;
        int item = 1;
        std::vector<std::function<void()>> actions;
        for (const core::Id busId : busIds_)
        {
            const auto* bus = owner_.project_->findBus(busId);
            const auto* send = const_cast<core::Channel*>(channel)->findSend(busId);
            const juce::String busName =
                bus != nullptr && !bus->name.empty() ? juce::String(bus->name) : "Bus";
            juce::PopupMenu sub;
            const bool has = send != nullptr;
            const float level = has ? send->level : 0.0f;
            sub.addItem(item++, "Off", true, !has);
            actions.push_back([this, busId]
                              { owner_.onCommand(std::make_unique<core::RemoveSend>(id_, busId)); });
            for (const double db : {-12.0, -6.0, 0.0})
            {
                const float value = dbToGain(db);
                sub.addItem(
                    item++, juce::String(db, 0) + " dB", true, has && std::abs(level - value) < 0.01f);
                actions.push_back(
                    [this, busId, value, has, send]
                    {
                        owner_.onCommand(std::make_unique<core::SetSend>(
                            id_, busId, value, has && send != nullptr ? send->preFader : false));
                    });
            }
            sub.addSeparator();
            sub.addItem(item++, "Pre-fader", has, has && send->preFader);
            actions.push_back(
                [this, busId, has, level, send]
                {
                    if (has && send != nullptr)
                        owner_.onCommand(std::make_unique<core::SetSend>(id_, busId, level, !send->preFader));
                });
            menu.addSubMenu(busName + (has ? "  (" + dbText(gainToDb(level)) + " dB)" : ""), sub);
        }
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(sends_),
                           [actions](int result)
                           {
                               if (result >= 1 && static_cast<size_t>(result) <= actions.size())
                                   actions[static_cast<size_t>(result) - 1]();
                           });
    }

    MixerView& owner_;
    const Theme& theme_;
    core::Id id_ = core::invalidId;
    core::Id group_ = core::invalidId;
    float currentGain_ = 1.0f;
    bool isBus_ = false;
    int meterSlot_ = -1;
    int sendCount_ = 0;
    int insertCount_ = 0;
    bool updating_ = false;
    juce::Colour colour_;
    std::vector<core::Id> busIds_;
    juce::Label name_;
    juce::Slider pan_, fader_;
    juce::TextButton mute_, solo_, phase_, sends_, inserts_, groupButton_;
    juce::ComboBox output_;
    StereoMeter meter_;
};

// ----- master -------------------------------------------------------------------------------

class MixerView::MasterStrip final : public juce::Component
{
public:
    MasterStrip(MixerView& owner, const Theme& theme) : owner_(owner), theme_(theme), meter_(theme)
    {
        name_.setText("Master", juce::dontSendNotification);
        name_.setFont(juce::FontOptions{theme_.type.caption});
        name_.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(name_);
        lufs_.setFont(juce::FontOptions{theme_.type.caption});
        lufs_.setJustificationType(juce::Justification::centred);
        lufs_.setColour(juce::Label::textColourId, theme_.colours.textSecondary);
        lufs_.setTooltip("Loudness, LUFS: momentary (400 ms) and short-term (3 s)");
        addAndMakeVisible(lufs_);
        fader_.setSliderStyle(juce::Slider::LinearVertical);
        fader_.setRange(minFaderDb, maxFaderDb, 0.1);
        fader_.setSkewFactorFromMidPoint(-12.0);
        fader_.setDoubleClickReturnValue(true, 0.0);
        fader_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 18);
        fader_.textFromValueFunction = [](double v) { return dbText(v); };
        fader_.valueFromTextFunction = [](const juce::String& t)
        { return t.containsIgnoreCase("inf") ? minFaderDb : t.getDoubleValue(); };
        fader_.onValueChange = [this]
        {
            if (!updating_)
                owner_.onCommand(std::make_unique<core::SetMasterGain>(dbToGain(fader_.getValue())));
        };
        addAndMakeVisible(fader_);
        addAndMakeVisible(meter_);
    }

    void setMaster(const core::Master& master)
    {
        const juce::ScopedValueSetter<bool> guard(updating_, true);
        fader_.setValue(gainToDb(master.gain), juce::dontSendNotification);
    }

    void pushMeter(const rt::MeterBlock::Reading& r) { meter_.push(r); }

    void pushLoudness(const engine::LoudnessMeter::Reading& r)
    {
        const auto text = [](float lufs)
        { return lufs <= -99.0f ? juce::String("-inf") : juce::String(lufs, 1); };
        lufs_.setText("M " + text(r.momentaryLufs) + "\nS " + text(r.shortTermLufs),
                      juce::dontSendNotification);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(theme_.colours.surfaceRaised);
        g.setColour(theme_.colours.border);
        g.fillRect(getLocalBounds().removeFromLeft(1));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(theme_.spacing.xs);
        area.removeFromTop(4);
        name_.setBounds(area.removeFromTop(18));
        lufs_.setBounds(area.removeFromTop(30));
        area.removeFromTop(theme_.spacing.xs);
        meter_.setBounds(area.removeFromRight(18).withTrimmedBottom(20));
        area.removeFromRight(theme_.spacing.xs);
        fader_.setBounds(area);
    }

private:
    MixerView& owner_;
    const Theme& theme_;
    bool updating_ = false;
    juce::Label name_, lufs_;
    juce::Slider fader_;
    StereoMeter meter_;
};

// ----- insert editor windows ------------------------------------------------------------------

class MixerView::EditorWindow final : public juce::DocumentWindow
{
public:
    EditorWindow(MixerView& owner, std::unique_ptr<InsertEditor> editor, const juce::String& title)
        : juce::DocumentWindow(title, Theme::dark().colours.background, juce::DocumentWindow::closeButton),
          owner_(owner), editor_(editor.get())
    {
        setUsingNativeTitleBar(true);
        setContentOwned(editor.release(), true);
        setResizable(false, false);
        setVisible(true);
    }

    InsertEditor& editor() noexcept { return *editor_; }

    void closeButtonPressed() override
    {
        juce::Component::SafePointer<MixerView> owner(&owner_);
        const auto id = editor_->insertId();
        juce::MessageManager::callAsync(
            [owner, id]
            {
                if (owner != nullptr)
                    std::erase_if(owner->editors_,
                                  [id](const auto& w) { return w->editor().insertId() == id; });
            });
    }

private:
    MixerView& owner_;
    InsertEditor* editor_;
};

// ----- view ---------------------------------------------------------------------------------

std::string MixerView::insertDisplayName(const std::string& type) const
{
    return host_ != nullptr ? host_->displayName(type) : plugins::BuiltInFactory::displayName(type);
}

void MixerView::openInsertEditor(core::Id channelId, core::Id insertId)
{
    for (auto& w : editors_)
        if (w->editor().insertId() == insertId)
        {
            w->toFront(true);
            return;
        }
    for (auto& w : pluginWindows_)
        if (w->insertId() == insertId)
        {
            w->toFront(true);
            return;
        }
    if (project_ == nullptr)
        return;
    const auto* channel = project_->findChannel(channelId);
    const auto* insert =
        channel != nullptr ? const_cast<core::Channel*>(channel)->findInsert(insertId) : nullptr;
    if (insert == nullptr)
        return;

    // A hosted plugin shows its own editor on its live instance.
    if (hosting::PluginRegistry::isHostedType(insert->type))
    {
        const auto live = liveInsert ? liveInsert(insertId) : nullptr;
        auto* hosted = dynamic_cast<hosting::HostedPlugin*>(live.get());
        if (hosted == nullptr)
            return; // not loaded (missing plugin): nothing to show
        const juce::String title = juce::String::fromUTF8(channel->name.c_str()) + " - " +
                                   juce::String::fromUTF8(insertDisplayName(insert->type).c_str());
        const auto setting = [this](const std::string& key)
        { return loadSetting ? loadSetting(key) : std::string{}; };
        const bool pinned = setting("plugins.pin") == "1";
        auto window = std::make_unique<PluginWindow>(channelId, insertId, live, *hosted, title, pinned);
        juce::Component::SafePointer<MixerView> safe(this);
        window->onClose = [safe, insertId]
        {
            if (safe != nullptr)
                std::erase_if(safe->pluginWindows_,
                              [insertId](const auto& w) { return w->insertId() == insertId; });
        };
        window->onCommand = [safe](std::unique_ptr<core::Command> c)
        {
            if (safe != nullptr && safe->onCommand)
                safe->onCommand(std::move(c));
        };
        window->onPinChanged = [safe](bool pin)
        {
            if (safe != nullptr && safe->saveSetting)
                safe->saveSetting("plugins.pin", pin ? "1" : "0");
        };
        // Where this plugin's window was last left, per plugin type, if still on screen.
        const std::string positionKey = "plugins.window." + insert->type;
        const auto saved = juce::StringArray::fromTokens(juce::String(setting(positionKey)), ",", "");
        const auto screen = juce::Desktop::getInstance().getDisplays().getTotalBounds(true);
        if (saved.size() == 2 && screen.contains(saved[0].getIntValue() + 20, saved[1].getIntValue() + 20))
            window->setTopLeftPosition(saved[0].getIntValue(), saved[1].getIntValue());
        else
            window->centreAroundComponent(getTopLevelComponent(), window->getWidth(), window->getHeight());
        window->onMoved = [safe, positionKey](juce::Point<int> p)
        {
            if (safe != nullptr && safe->saveSetting)
                safe->saveSetting(positionKey, std::to_string(p.x) + "," + std::to_string(p.y));
        };
        window->setInsert(*insert);
        pluginWindows_.push_back(std::move(window));
        return;
    }

    const auto processor = plugins::BuiltInFactory().create(insert->type);
    if (processor == nullptr)
        return;
    auto editor = std::make_unique<InsertEditor>(channelId, insertId, *processor);
    editor->onCommand = [this](std::unique_ptr<core::Command> c)
    {
        if (onCommand)
            onCommand(std::move(c));
    };
    editor->setInsert(*insert);
    const juce::String title = juce::String(channel->name) + " - " +
                               juce::String(plugins::BuiltInFactory::displayName(insert->type));
    auto window = std::make_unique<EditorWindow>(*this, std::move(editor), title);
    window->centreAroundComponent(getTopLevelComponent(), window->getWidth(), window->getHeight());
    editors_.push_back(std::move(window));
}

MixerView::MixerView(rt::MeterBlock& meters, std::function<engine::LoudnessMeter::Reading()> loudness)
    : meters_(meters), loudness_(std::move(loudness))
{
    setOpaque(true);
    viewport_.setViewedComponent(&stripsHolder_, false);
    viewport_.setScrollBarsShown(false, true);
    addAndMakeVisible(viewport_);
    master_ = std::make_unique<MasterStrip>(*this, theme_);
    addAndMakeVisible(*master_);
    addBusButton_.onClick = [this] { addBus(); };
    addAndMakeVisible(addBusButton_);
    pluginsButton_.onClick = [this]
    {
        if (onOpenPluginBrowser)
            onOpenPluginBrowser();
    };
    addAndMakeVisible(pluginsButton_);
    startTimerHz(meterHz);
}

MixerView::~MixerView()
{
    stopTimer();
    closeEditors();
}

void MixerView::closeEditors()
{
    pluginWindows_.clear();
    editors_.clear();
}

void MixerView::setProject(const core::Project* project)
{
    project_ = project;
    rebuildStrips();

    // Open editors follow the model (undo, a removed insert closes its window).
    // A plugin window must go before its instance, which the engine drops once no
    // snapshot references it (after this call).
    std::erase_if(
        pluginWindows_,
        [this](const auto& w)
        {
            const auto* channel = project_ != nullptr ? project_->findChannel(w->channelId()) : nullptr;
            const auto* insert =
                channel != nullptr ? const_cast<core::Channel*>(channel)->findInsert(w->insertId()) : nullptr;
            if (insert == nullptr)
                return true;
            w->setInsert(*insert);
            return false;
        });
    std::erase_if(editors_,
                  [this](const auto& w)
                  {
                      const auto* channel =
                          project_ != nullptr ? project_->findChannel(w->editor().channelId()) : nullptr;
                      const auto* insert =
                          channel != nullptr
                              ? const_cast<core::Channel*>(channel)->findInsert(w->editor().insertId())
                              : nullptr;
                      if (insert == nullptr)
                          return true;
                      w->editor().setInsert(*insert);
                      return false;
                  });
}

void MixerView::addBus()
{
    if (!onCommand)
        return;
    core::Bus bus;
    bus.name = "Bus " + std::to_string(project_ != nullptr ? project_->buses.size() + 1 : 1);
    onCommand(std::make_unique<core::AddBus>(bus));
}

void MixerView::rebuildStrips()
{
    const size_t count = project_ != nullptr ? project_->tracks.size() + project_->buses.size() : 0;
    while (strips_.size() > count)
        strips_.pop_back();
    while (strips_.size() < count)
    {
        auto strip = std::make_unique<Strip>(*this, theme_);
        stripsHolder_.addAndMakeVisible(*strip);
        strips_.push_back(std::move(strip));
    }
    if (project_ != nullptr)
    {
        size_t i = 0;
        for (size_t t = 0; t < project_->tracks.size(); ++t, ++i)
            strips_[i]->setChannel(
                project_->tracks[t], false, *project_, engine::trackMeterSlot(static_cast<int>(t)));
        for (size_t b = 0; b < project_->buses.size(); ++b, ++i)
            strips_[i]->setChannel(
                project_->buses[b], true, *project_, engine::busMeterSlot(*project_, static_cast<int>(b)));
        master_->setMaster(project_->master);
    }
    layoutStrips();
}

void MixerView::layoutStrips()
{
    const int height = viewport_.getHeight();
    stripsHolder_.setSize(std::max(1, static_cast<int>(strips_.size()) * stripWidth), std::max(1, height));
    int x = 0;
    for (auto& strip : strips_)
    {
        strip->setBounds(x, 0, stripWidth, height);
        x += stripWidth;
    }
}

void MixerView::timerCallback()
{
    for (auto& strip : strips_)
        strip->pushMeter(meters_.take(strip->meterSlot()));
    if (project_ != nullptr)
        master_->pushMeter(meters_.take(engine::masterMeterSlot(*project_)));
    if (loudness_)
        master_->pushLoudness(loudness_());
}

void MixerView::paint(juce::Graphics& g)
{
    g.fillAll(theme_.colours.background);
    g.setColour(theme_.colours.border);
    g.fillRect(getLocalBounds().removeFromTop(1));
}

void MixerView::resized()
{
    auto area = getLocalBounds().withTrimmedTop(1);
    auto header = area.removeFromTop(theme_.sizes.controlHeight + theme_.spacing.sm);
    auto headerRow = header.reduced(theme_.spacing.sm, theme_.spacing.xs);
    addBusButton_.setBounds(headerRow.removeFromLeft(72));
    headerRow.removeFromLeft(theme_.spacing.sm);
    pluginsButton_.setBounds(headerRow.removeFromLeft(90));
    master_->setBounds(area.removeFromRight(stripWidth + 16));
    viewport_.setBounds(area);
    layoutStrips();
}

} // namespace aural::ui
