#include "ui/strip/ChannelStripView.h"

#include "core/Command.h"
#include "ui/AuralLookAndFeel.h"

#include <map>

namespace aural::ui::strip
{

namespace
{
std::map<std::string, bool>& expandedTypes()
{
    static std::map<std::string, bool> chosen;
    return chosen;
}
} // namespace

bool ChannelStripView::expanded(const std::string& type, bool on)
{
    const auto& chosen = expandedTypes();
    const auto it = chosen.find(type);
    return it != chosen.end() ? it->second : on;
}

void ChannelStripView::collapse(core::Id moduleId, bool collapsed)
{
    for (const auto& card : cards_)
        if (card->moduleId() == moduleId)
        {
            expandedTypes()[card->type()] = !collapsed;
            card->setCollapsed(collapsed);
        }
    resized();
    if (onWidthChanged)
        onWidthChanged();
}

ChannelStripView::ChannelStripView(const Theme& theme) : theme_(theme)
{
    setOpaque(false);
    cardHost_.onCommand = [this](std::unique_ptr<core::Command> c)
    {
        if (host.onCommand)
            host.onCommand(std::move(c));
    };
    cardHost_.onGestureBegin = [this](const std::string& name)
    {
        if (host.onGestureBegin)
            host.onGestureBegin(name);
    };
    cardHost_.onGestureEnd = [this]
    {
        if (host.onGestureEnd)
            host.onGestureEnd();
    };
    cardHost_.liveInsert = [this](core::Id id)
    { return host.liveInsert ? host.liveInsert(id) : std::shared_ptr<engine::InsertProcessor>{}; };
    cardHost_.onReorder = [this](core::Id id, juce::Point<int> screen, bool released)
    { reorder(id, screen, released); };
    cardHost_.onCollapse = [this](core::Id id, bool collapsed) { collapse(id, collapsed); };

    position_.getProperties().set(AuralLookAndFeel::propertyVariant, "ghost");
    position_.setTooltip("Where the strip sits in the channel: click to move it before or after the inserts");
    position_.onClick = [this]
    {
        if (host.onCommand && channelId_ != core::invalidId)
            host.onCommand(std::make_unique<core::SetStripPosition>(channelId_, !preInserts_));
    };
    addAndMakeVisible(position_);
    allOff_.setButtonText("All off");
    allOff_.getProperties().set(AuralLookAndFeel::propertyVariant, "ghost");
    allOff_.setTooltip("Switch every module of the strip off");
    allOff_.onClick = [this]
    {
        if (!host.onCommand)
            return;
        if (host.onGestureBegin)
            host.onGestureBegin("Strip off");
        for (const auto& card : cards_)
            if (card->isOn())
                host.onCommand(std::make_unique<core::SetInsertBypass>(channelId_, card->moduleId(), true));
        if (host.onGestureEnd)
            host.onGestureEnd();
    };
    addAndMakeVisible(allOff_);
    startTimerHz(60);
}

ChannelStripView::~ChannelStripView()
{
    stopTimer();
    cards_.clear();
}

void ChannelStripView::setChannel(const core::Channel* channel)
{
    std::vector<core::Id> order;
    if (channel != nullptr)
        for (const auto& m : channel->strip)
            order.push_back(m.id);
    const core::Id id = channel != nullptr ? channel->id : core::invalidId;
    if (id != channelId_ || order != order_)
    {
        cards_.clear();
        channelId_ = id;
        order_ = order;
        if (channel != nullptr)
            for (const auto& m : channel->strip)
                if (auto card = createModuleCard(theme_, cardHost_, channelId_, m))
                {
                    card->setCollapsed(!expanded(m.type, !m.bypass));
                    addAndMakeVisible(*card);
                    cards_.push_back(std::move(card));
                }
    }
    else if (channel != nullptr)
        for (const auto& card : cards_)
            if (const auto* m = channel->findInsert(card->moduleId()))
            {
                // Switching a module on opens its card.
                if (card->isCollapsed() && !card->isOn() && !m->bypass)
                {
                    expandedTypes()[card->type()] = true;
                    card->setCollapsed(false);
                }
                card->refresh(*m);
            }
    const bool pre = channel != nullptr && channel->stripPreInserts;
    if (pre != preInserts_ || position_.getButtonText().isEmpty())
    {
        preInserts_ = pre;
        position_.setButtonText(
            juce::String::fromUTF8(pre ? "Before inserts \xe2\x87\x84" : "After inserts \xe2\x87\x84"));
    }
    allOff_.setEnabled(std::any_of(cards_.begin(), cards_.end(), [](const auto& c) { return c->isOn(); }));
    resized();
    repaint();
}

int ChannelStripView::preferredWidth() const
{
    int w = 0;
    for (const auto& card : cards_)
        w += card->layoutWidth() + theme_.spacing.sm;
    return std::max(0, w - theme_.spacing.sm);
}

void ChannelStripView::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    auto caption = getLocalBounds().removeFromTop(captionHeight);
    g.setFont(theme_.captionCaps());
    g.setColour(c.textTertiary);
    g.drawText(
        "CHANNEL STRIP", caption.withTrimmedLeft(theme_.spacing.xxs), juce::Justification::centredLeft);
    if (dropIndex_ >= 0)
    {
        int x = 0;
        if (dropIndex_ > 0 && static_cast<size_t>(dropIndex_) <= cards_.size())
            x = cards_[static_cast<size_t>(dropIndex_ - 1)]->getRight() + theme_.spacing.sm / 2;
        g.setColour(c.accent);
        g.fillRect(x - 1, captionHeight, 2, getHeight() - captionHeight);
    }
}

void ChannelStripView::resized()
{
    auto caption = getLocalBounds().removeFromTop(captionHeight);
    caption.removeFromLeft(110);
    position_.setBounds(caption.removeFromLeft(120).reduced(0, 1));
    allOff_.setBounds(caption.removeFromLeft(64).reduced(0, 1));
    int x = 0;
    const int h = getHeight() - captionHeight;
    for (const auto& card : cards_)
    {
        card->setBounds(x, captionHeight, card->layoutWidth(), h);
        x += card->layoutWidth() + theme_.spacing.sm;
    }
}

void ChannelStripView::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double elapsed = lastTickMs_ > 0.0 ? std::min(100.0, now - lastTickMs_) : 16.0;
    lastTickMs_ = now;
    const bool showing = isShowing();
    if (!showing)
    {
        if (wasShowing_)
            for (const auto& card : cards_)
                card->stopWatching();
        wasShowing_ = false;
        return;
    }
    wasShowing_ = true;
    for (const auto& card : cards_)
        card->tick(elapsed);
}

int ChannelStripView::indexForX(int x) const
{
    int index = 0;
    for (const auto& card : cards_)
    {
        if (x < card->getX() + card->getWidth() / 2)
            break;
        ++index;
    }
    return index;
}

void ChannelStripView::reorder(core::Id moduleId, juce::Point<int> screen, bool released)
{
    const int index = indexForX(getLocalPoint(nullptr, screen).x);
    if (!released)
    {
        dropIndex_ = index;
        repaint();
        return;
    }
    dropIndex_ = -1;
    repaint();
    int from = -1;
    for (size_t i = 0; i < cards_.size(); ++i)
        if (cards_[i]->moduleId() == moduleId)
            from = static_cast<int>(i);
    const int to = index > from ? index - 1 : index;
    if (from >= 0 && to != from && host.onCommand)
        host.onCommand(std::make_unique<core::MoveStripModule>(channelId_, moduleId, to));
}

} // namespace aural::ui::strip
