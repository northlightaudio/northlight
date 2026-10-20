#include "ui/shell/Panels.h"

#include "ui/AuralLookAndFeel.h"

#include <cmath>

namespace aural::ui
{

const char* panelKey(PanelId id)
{
    switch (id)
    {
        case PanelId::mixer:
            return "mixer";
        case PanelId::editor:
            return "editor";
        case PanelId::pianoRoll:
            return "pianoRoll";
        case PanelId::diagnostics:
            return "diagnostics";
        case PanelId::browser:
            return "browser";
        case PanelId::inspector:
            return "inspector";
        case PanelId::instruments:
            return "instruments";
    }
    return "panel";
}

namespace
{
const char* hostKey(HostKind kind)
{
    switch (kind)
    {
        case HostKind::dock:
            return "dock";
        case HostKind::side:
            return "side";
        case HostKind::floating:
            return "floating";
    }
    return "dock";
}

HostKind hostFromKey(const juce::String& key, HostKind fallback)
{
    if (key == "dock")
        return HostKind::dock;
    if (key == "side")
        return HostKind::side;
    if (key == "floating")
        return HostKind::floating;
    return fallback;
}
} // namespace

// ----- host ----------------------------------------------------------------------------------

PanelHost::PanelHost(const Theme& theme, HostKind kind)
    : theme_(theme), kind_(kind), detach_(theme, Icon::detach, "Open in window"),
      close_(theme, Icon::close, "Close")
{
    setOpaque(true);
    detach_.setSize(theme_.sizes.controlCompact, theme_.sizes.controlCompact);
    close_.setSize(theme_.sizes.controlCompact, theme_.sizes.controlCompact);
    detach_.onClick = [this]
    {
        if (hasCurrent_ && onDetach)
            onDetach(current_);
    };
    close_.onClick = [this]
    {
        if (hasCurrent_ && onClose)
            onClose(current_);
    };
    addChildComponent(detach_);
    addChildComponent(close_);
}

void PanelHost::addPanel(const PanelInfo& info, juce::Component& panel, int index)
{
    removePanel(info.id);
    Tab tab;
    tab.info = info;
    tab.panel = &panel;
    if (index < 0 || index >= static_cast<int>(tabs_.size()))
        tabs_.push_back(tab);
    else
        tabs_.insert(tabs_.begin() + index, tab);
    addChildComponent(panel);
    showPanel(info.id);
}

void PanelHost::removePanel(PanelId id)
{
    for (auto it = tabs_.begin(); it != tabs_.end(); ++it)
        if (it->info.id == id)
        {
            removeChildComponent(it->panel);
            it->panel->setVisible(false);
            tabs_.erase(it);
            break;
        }
    if (hasCurrent_ && current_ == id)
    {
        hasCurrent_ = false;
        if (!tabs_.empty())
            showPanel(tabs_.front().info.id);
    }
    layoutTabs();
    repaint();
}

bool PanelHost::hasPanel(PanelId id) const noexcept
{
    for (const auto& tab : tabs_)
        if (tab.info.id == id)
            return true;
    return false;
}

std::vector<PanelId> PanelHost::panels() const
{
    std::vector<PanelId> ids;
    for (const auto& tab : tabs_)
        ids.push_back(tab.info.id);
    return ids;
}

void PanelHost::showPanel(PanelId id)
{
    for (auto& tab : tabs_)
        tab.panel->setVisible(tab.info.id == id);
    current_ = id;
    hasCurrent_ = hasPanel(id);
    detach_.setVisible(hasCurrent_);
    close_.setVisible(hasCurrent_);
    resized();
    repaint();
    if (hasCurrent_ && onShown)
        onShown(id);
}

void PanelHost::setDropHighlight(bool on)
{
    if (dropHighlight_ != on)
    {
        dropHighlight_ = on;
        repaint();
    }
}

int PanelHost::insertIndexForX(int x) const noexcept
{
    for (size_t i = 0; i < tabs_.size(); ++i)
        if (x < tabs_[i].bounds.getCentreX())
            return static_cast<int>(i);
    return static_cast<int>(tabs_.size());
}

void PanelHost::layoutTabs()
{
    int x = theme_.spacing.sm;
    for (auto& tab : tabs_)
    {
        const int w = juce::GlyphArrangement::getStringWidthInt(theme_.label(), tab.info.title) +
                      theme_.sizes.icon + theme_.spacing.sm + 2 * theme_.spacing.md;
        tab.bounds = {x, 0, w, headerHeight()};
        x += w;
    }
}

void PanelHost::resized()
{
    layoutTabs();
    auto header = getLocalBounds().removeFromTop(headerHeight());
    const int side = theme_.sizes.controlCompact;
    auto right = header.reduced(theme_.spacing.xs, (headerHeight() - side) / 2);
    close_.setBounds(right.removeFromRight(side));
    right.removeFromRight(theme_.spacing.xs);
    detach_.setBounds(right.removeFromRight(side));
    const auto content = getLocalBounds().withTrimmedTop(headerHeight());
    for (auto& tab : tabs_)
        tab.panel->setBounds(content);
}

void PanelHost::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    g.fillAll(c.surface0);
    const auto header = getLocalBounds().removeFromTop(headerHeight());
    g.setColour(c.lineSubtle);
    g.fillRect(header.withTrimmedTop(headerHeight() - 1));
    for (size_t i = 0; i < tabs_.size(); ++i)
    {
        const auto& tab = tabs_[i];
        const bool front = hasCurrent_ && tab.info.id == current_;
        const bool hover = static_cast<int>(i) == hoverTab_;
        auto area = tab.bounds.reduced(theme_.spacing.md, 0);
        const auto textColour = front || hover ? c.textPrimary : c.textSecondary;
        Icons::draw(g,
                    tab.info.icon,
                    area.removeFromLeft(theme_.sizes.icon)
                        .toFloat()
                        .withSizeKeepingCentre(static_cast<float>(theme_.sizes.icon),
                                               static_cast<float>(theme_.sizes.icon)),
                    textColour);
        area.removeFromLeft(theme_.spacing.sm);
        g.setColour(textColour);
        g.setFont(theme_.label());
        g.drawText(tab.info.title, area, juce::Justification::centredLeft);
        if (front)
        {
            g.setColour(c.accent);
            g.fillRect(
                tab.bounds.withTrimmedTop(headerHeight() - theme_.spacing.xxs).reduced(theme_.spacing.sm, 0));
        }
    }
    if (dropHighlight_)
    {
        g.setColour(c.dropTarget);
        g.fillRect(getLocalBounds());
        g.setColour(c.accent);
        g.drawRect(getLocalBounds(), 1);
    }
}

int PanelHost::tabAt(juce::Point<int> p) const noexcept
{
    for (size_t i = 0; i < tabs_.size(); ++i)
        if (tabs_[i].bounds.contains(p))
            return static_cast<int>(i);
    return -1;
}

void PanelHost::mouseMove(const juce::MouseEvent& e)
{
    const int tab = tabAt(e.getPosition());
    if (tab != hoverTab_)
    {
        hoverTab_ = tab;
        repaint(getLocalBounds().removeFromTop(headerHeight()));
    }
}

void PanelHost::mouseDown(const juce::MouseEvent& e)
{
    pressedTab_ = tabAt(e.getPosition());
    pressPosition_ = e.getPosition();
    dragStarted_ = false;
    if (pressedTab_ >= 0)
        showPanel(tabs_[static_cast<size_t>(pressedTab_)].info.id);
}

void PanelHost::mouseDrag(const juce::MouseEvent& e)
{
    if (pressedTab_ < 0 || dragStarted_)
        return;
    // Dragging a tab out of the row past 24 px lifts the panel into a window.
    const int outside = kind_ == HostKind::dock ? pressPosition_.y - e.y : e.x - pressPosition_.x;
    if (std::abs(e.y - pressPosition_.y) > theme_.spacing.xl || std::abs(outside) > theme_.spacing.xl)
    {
        dragStarted_ = true;
        const auto id = tabs_[static_cast<size_t>(pressedTab_)].info.id;
        pressedTab_ = -1;
        if (onTabDraggedOut)
            onTabDraggedOut(id, e.getScreenPosition());
    }
}

void PanelHost::mouseUp(const juce::MouseEvent&)
{
    pressedTab_ = -1;
    dragStarted_ = false;
}

// ----- floating window -----------------------------------------------------------------------

class FloatingPanelWindow::Header final : public juce::Component
{
public:
    Header(const Theme& theme, const PanelInfo& info, FloatingPanelWindow& window)
        : theme_(theme), info_(info), window_(window), attach_(theme, Icon::attach, "Return to dock"),
          close_(theme, Icon::close, "Close")
    {
        attach_.setSize(theme_.sizes.controlCompact, theme_.sizes.controlCompact);
        close_.setSize(theme_.sizes.controlCompact, theme_.sizes.controlCompact);
        attach_.setTooltip(info_.defaultHost == HostKind::side ? "Return to side panel" : "Return to dock");
        attach_.onClick = [this]
        {
            if (info_.canDock && info_.canSide)
            {
                juce::PopupMenu menu;
                menu.addItem(1, "Return to dock");
                menu.addItem(2, "Return to side panel");
                juce::Component::SafePointer<Header> safe(this);
                menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(attach_),
                                   [safe](int choice)
                                   {
                                       if (safe != nullptr && choice > 0 && safe->window_.onAttach)
                                           safe->window_.onAttach(
                                               safe->info_.id, choice == 1 ? HostKind::dock : HostKind::side);
                                   });
                return;
            }
            if (window_.onAttach)
                window_.onAttach(info_.id, info_.canSide && !info_.canDock ? HostKind::side : HostKind::dock);
        };
        close_.onClick = [this] { window_.closeButtonPressed(); };
        addAndMakeVisible(attach_);
        addAndMakeVisible(window_.pin_);
        addAndMakeVisible(close_);
    }

    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        g.fillAll(c.surface0);
        g.setColour(c.lineSubtle);
        g.fillRect(getLocalBounds().removeFromBottom(1));
        auto area = getLocalBounds().reduced(theme_.spacing.md, 0);
        Icons::draw(g,
                    info_.icon,
                    area.removeFromLeft(theme_.sizes.icon)
                        .toFloat()
                        .withSizeKeepingCentre(static_cast<float>(theme_.sizes.icon),
                                               static_cast<float>(theme_.sizes.icon)),
                    c.textSecondary);
        area.removeFromLeft(theme_.spacing.sm);
        g.setColour(c.textPrimary);
        g.setFont(theme_.label());
        g.drawText(info_.title, area, juce::Justification::centredLeft);
    }

    void resized() override
    {
        const int side = theme_.sizes.controlCompact;
        auto area = getLocalBounds().reduced(theme_.spacing.xs, (getHeight() - side) / 2);
        close_.setBounds(area.removeFromRight(side));
        area.removeFromRight(theme_.spacing.xs);
        window_.pin_.setBounds(area.removeFromRight(side));
        area.removeFromRight(theme_.spacing.xs);
        attach_.setBounds(area.removeFromRight(side));
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (window_.onHeaderDrag)
            window_.onHeaderDrag(info_.id, e.getScreenPosition(), false);
    }
    void mouseUp(const juce::MouseEvent& e) override
    {
        if (window_.onHeaderDrag)
            window_.onHeaderDrag(info_.id, e.getScreenPosition(), true);
    }

private:
    const Theme& theme_;
    PanelInfo info_;
    FloatingPanelWindow& window_;
    IconButton attach_, close_;
};

FloatingPanelWindow::FloatingPanelWindow(const Theme& theme, const PanelInfo& info, juce::Component& panel)
    : juce::DocumentWindow(info.title,
                           theme.colours.surface0,
                           juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton |
                               juce::DocumentWindow::maximiseButton),
      theme_(theme), info_(info), panel_(&panel), pin_(theme, Icon::pin, "Keep on top")
{
    setUsingNativeTitleBar(true);
    pin_.setClickingTogglesState(true);
    pin_.setSize(theme_.sizes.controlCompact, theme_.sizes.controlCompact);
    pin_.onClick = [this] { setAlwaysOnTop(pin_.getToggleState()); };
    auto* holder = new juce::Component();
    holder->setOpaque(true);
    header_ = new Header(theme_, info_, *this);
    holder->addAndMakeVisible(header_);
    holder->addAndMakeVisible(panel_);
    struct Layout final : public juce::ComponentListener
    {
        Layout(const Theme& t, Header* h, juce::Component* p) : theme(t), header(h), panel(p) {}
        void componentMovedOrResized(juce::Component& holderComponent, bool, bool) override
        {
            auto area = holderComponent.getLocalBounds();
            header->setBounds(area.removeFromTop(theme.sizes.controlLarge));
            panel->setBounds(area);
        }
        const Theme& theme;
        Header* header;
        juce::Component* panel;
    };
    auto* layout = new Layout(theme_, header_, panel_);
    holder->addComponentListener(layout);
    holder->getProperties().set("layoutListener", juce::var(reinterpret_cast<juce::int64>(layout)));
    layoutListener_ = layout;
    setContentOwned(holder, false);
    setResizable(true, false);
    setResizeLimits(info_.minWidth, info_.minHeight + theme_.sizes.controlLarge, 10000, 10000);
}

FloatingPanelWindow::~FloatingPanelWindow()
{
    if (auto* holder = getContentComponent())
    {
        holder->removeChildComponent(panel_);
        if (layoutListener_ != nullptr)
            holder->removeComponentListener(static_cast<juce::ComponentListener*>(layoutListener_));
    }
    delete static_cast<juce::ComponentListener*>(layoutListener_);
    panel_->setVisible(false);
}

void FloatingPanelWindow::setPinned(bool pinned)
{
    pin_.setToggleState(pinned, juce::dontSendNotification);
    setAlwaysOnTop(pinned);
}

void FloatingPanelWindow::closeButtonPressed()
{
    if (onCloseRequested)
        onCloseRequested(info_.id);
}

bool FloatingPanelWindow::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        closeButtonPressed();
        return true;
    }
    return juce::DocumentWindow::keyPressed(key);
}

// ----- manager -------------------------------------------------------------------------------

PanelManager::PanelManager(const Theme& theme, PanelHost& dock, PanelHost& side)
    : theme_(theme), dock_(dock), side_(side)
{
    for (auto* host : {&dock_, &side_})
    {
        host->onDetach = [this](PanelId id) { moveTo(id, HostKind::floating); };
        host->onClose = [this](PanelId id) { hide(id); };
        host->onTabDraggedOut = [this](PanelId id, juce::Point<int>) { moveTo(id, HostKind::floating); };
        host->onShown = [this](PanelId id)
        {
            for (auto& entry : entries_)
                if (entry->host == hostOf(id) && entry->host != HostKind::floating)
                {
                    const bool visible = entry->info.id == id;
                    if (entry->visible != visible)
                    {
                        entry->visible = visible;
                        if (onPanelVisibilityChanged)
                            onPanelVisibilityChanged(entry->info.id, visible);
                    }
                }
        };
    }
}

PanelManager::~PanelManager()
{
    stopTimer();
    for (auto& entry : entries_)
        entry->window.reset();
}

void PanelManager::registerPanel(const PanelInfo& info, juce::Component& panel)
{
    auto entry = std::make_unique<Entry>();
    entry->info = info;
    entry->panel = &panel;
    entry->host = info.defaultHost;
    entry->visible = false;
    entries_.push_back(std::move(entry));
}

PanelManager::Entry* PanelManager::find(PanelId id)
{
    for (auto& entry : entries_)
        if (entry->info.id == id)
            return entry.get();
    return nullptr;
}

const PanelManager::Entry* PanelManager::find(PanelId id) const
{
    for (const auto& entry : entries_)
        if (entry->info.id == id)
            return entry.get();
    return nullptr;
}

const PanelInfo* PanelManager::info(PanelId id) const
{
    const auto* entry = find(id);
    return entry != nullptr ? &entry->info : nullptr;
}

PanelHost& PanelManager::hostComponent(HostKind kind)
{
    return kind == HostKind::side ? side_ : dock_;
}

bool PanelManager::isVisible(PanelId id) const
{
    const auto* entry = find(id);
    return entry != nullptr && entry->visible;
}

HostKind PanelManager::hostOf(PanelId id) const
{
    const auto* entry = find(id);
    return entry != nullptr ? entry->host : HostKind::dock;
}

void PanelManager::placeInHost(Entry& entry, int index)
{
    auto& host = hostComponent(entry.host);
    host.addPanel(entry.info, *entry.panel, index);
    entry.visible = true;
    // Only one panel shows per host: the others there are hidden.
    for (auto& other : entries_)
        if (other.get() != &entry && other->host == entry.host && other->visible)
        {
            other->visible = false;
            if (onPanelVisibilityChanged)
                onPanelVisibilityChanged(other->info.id, false);
        }
}

void PanelManager::removeFromHost(Entry& entry)
{
    auto& host = hostComponent(entry.host);
    const bool wasCurrent = host.currentPanel() == entry.info.id;
    host.removePanel(entry.info.id);
    if (wasCurrent && !host.empty())
        if (auto* shown = find(host.currentPanel()))
            shown->visible = true;
}

void PanelManager::openFloating(Entry& entry)
{
    if (entry.window != nullptr)
    {
        entry.window->toFront(true);
        return;
    }
    entry.window = std::make_unique<FloatingPanelWindow>(theme_, entry.info, *entry.panel);
    entry.window->onAttach = [this](PanelId id, HostKind host) { moveTo(id, host); };
    entry.window->onCloseRequested = [this](PanelId id)
    {
        // Closing a floating panel returns it to its default host, hidden.
        juce::MessageManager::callAsync(
            [this, id]
            {
                if (auto* e = find(id))
                {
                    closeFloating(*e, false);
                    e->host = e->info.defaultHost;
                    e->visible = false;
                    refreshHosts();
                    if (onPanelVisibilityChanged)
                        onPanelVisibilityChanged(id, false);
                }
            });
    };
    entry.window->onHeaderDrag = [this](PanelId id, juce::Point<int> screen, bool released)
    { headerDragged(id, screen, released); };
    juce::Rectangle<int> bounds = entry.floatingBounds;
    if (bounds.isEmpty())
    {
        const juce::Rectangle<int> main =
            mainWindow_ != nullptr
                ? mainWindow_->getScreenBounds()
                : juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userBounds.toNearestInt();
        bounds = juce::Rectangle<int>(main.getWidth() * 3 / 5, main.getHeight() * 3 / 5)
                     .withCentre(main.getCentre());
    }
    // Back onto a screen that exists.
    const auto displays = juce::Desktop::getInstance().getDisplays();
    if (!displays.getTotalBounds(true).intersects(bounds))
        bounds = bounds.withCentre(displays.getPrimaryDisplay()->userBounds.toNearestInt().getCentre());
    entry.window->setBounds(bounds);
    entry.window->setPinned(entry.pinned);
    entry.window->setVisible(true);
    entry.panel->setVisible(true);
    entry.visible = true;
}

void PanelManager::closeFloating(Entry& entry, bool keepVisible)
{
    if (entry.window == nullptr)
        return;
    entry.floatingBounds = entry.window->getBounds();
    entry.pinned = entry.window->pinned();
    entry.window.reset();
    entry.visible = keepVisible;
}

void PanelManager::show(PanelId id)
{
    auto* entry = find(id);
    if (entry == nullptr)
        return;
    if (entry->host == HostKind::floating)
    {
        openFloating(*entry);
        return;
    }
    auto& host = hostComponent(entry->host);
    if (!host.hasPanel(id))
        placeInHost(*entry, -1);
    else
    {
        host.showPanel(id);
        entry->visible = true;
    }
    entry->panel->grabKeyboardFocus();
    refreshHosts();
    if (onPanelVisibilityChanged)
        onPanelVisibilityChanged(id, true);
}

void PanelManager::hide(PanelId id)
{
    auto* entry = find(id);
    if (entry == nullptr || !entry->visible)
        return;
    if (entry->host == HostKind::floating)
        closeFloating(*entry, false);
    else
        removeFromHost(*entry);
    entry->visible = false;
    refreshHosts();
    if (onPanelVisibilityChanged)
        onPanelVisibilityChanged(id, false);
}

void PanelManager::toggle(PanelId id)
{
    if (isVisible(id))
        hide(id);
    else
        show(id);
}

void PanelManager::moveTo(PanelId id, HostKind host, int index)
{
    auto* entry = find(id);
    if (entry == nullptr)
        return;
    if (host == HostKind::side && !entry->info.canSide)
        host = HostKind::dock;
    if (host == HostKind::dock && !entry->info.canDock)
        host = entry->info.canSide ? HostKind::side : HostKind::floating;
    if (entry->host == HostKind::floating)
        closeFloating(*entry, true);
    else
        removeFromHost(*entry);
    entry->host = host;
    if (host == HostKind::floating)
        openFloating(*entry);
    else
        placeInHost(*entry, index);
    refreshHosts();
    if (onPanelVisibilityChanged)
        onPanelVisibilityChanged(id, true);
}

void PanelManager::detachOrReattach(PanelId id)
{
    const auto* entry = find(id);
    if (entry == nullptr)
        return;
    moveTo(id, entry->host == HostKind::floating ? entry->info.defaultHost : HostKind::floating);
}

bool PanelManager::handleKey(const juce::KeyPress& key)
{
    for (const auto& entry : entries_)
    {
        const auto& shortcut = entry->info.shortcut;
        if (!shortcut.isValid())
            continue;
        if (key.getKeyCode() != shortcut.getKeyCode())
            continue;
        if (key.getModifiers().isShiftDown())
        {
            detachOrReattach(entry->info.id);
            return true;
        }
        if (key.getModifiers().withoutMouseButtons() == shortcut.getModifiers())
        {
            if (entry->visible)
            {
                // Already showing: focus it (a floating window comes to the front).
                if (entry->window != nullptr)
                    entry->window->toFront(true);
                entry->panel->grabKeyboardFocus();
            }
            else
                show(entry->info.id);
            return true;
        }
    }
    return false;
}

void PanelManager::headerDragged(PanelId id, juce::Point<int> screenPosition, bool released)
{
    auto* target = hostAtScreenPosition(screenPosition);
    const auto* entry = find(id);
    if (entry != nullptr && target != nullptr)
    {
        const bool allowed = target->kind() == HostKind::dock ? entry->info.canDock : entry->info.canSide;
        if (!allowed)
            target = nullptr;
    }
    if (highlighted_ != nullptr && highlighted_ != target)
        highlighted_->setDropHighlight(false);
    highlighted_ = target;
    if (target != nullptr)
        target->setDropHighlight(!released);
    if (released)
    {
        highlighted_ = nullptr;
        if (target != nullptr)
        {
            const int index = target->insertIndexForX(target->getLocalPoint(nullptr, screenPosition).x);
            moveTo(id, target->kind(), index);
        }
    }
}

PanelHost* PanelManager::hostAtScreenPosition(juce::Point<int> screenPosition)
{
    for (auto* host : {&dock_, &side_})
        if (host->isShowing() && host->getScreenBounds().contains(screenPosition))
            return host;
    // An empty, closed host still accepts a drop over the strip where it would open.
    return nullptr;
}

void PanelManager::setDockHeight(int height)
{
    dockSize_ = std::max(theme_.sizes.dockMin, height);
    dockRequested_ = dockSize_;
    dockHeightShown_ = dockSize_;
    if (onLayoutChanged)
        onLayoutChanged();
}

void PanelManager::setSideWidth(int width)
{
    sideSize_ = std::max(theme_.sizes.panelMin, width);
    sideRequested_ = sideSize_;
    sideWidthShown_ = sideSize_;
    if (onLayoutChanged)
        onLayoutChanged();
}

void PanelManager::refreshHosts()
{
    dockRequested_ = dock_.empty() ? 0 : dockSize_;
    sideRequested_ = side_.empty() ? 0 : sideSize_;
    animate();
}

void PanelManager::animate()
{
    const int duration = theme_.motion.ms(theme_.motion.panelMs);
    if (duration <= 0)
    {
        dockHeightShown_ = dockRequested_;
        sideWidthShown_ = sideRequested_;
        if (onLayoutChanged)
            onLayoutChanged();
        return;
    }
    dockFrom_ = dockHeightShown_;
    sideFrom_ = sideWidthShown_;
    animationStart_ = juce::Time::getMillisecondCounterHiRes();
    startTimerHz(60);
}

void PanelManager::timerCallback()
{
    const int duration = std::max(1, theme_.motion.ms(theme_.motion.panelMs));
    const double t =
        juce::jlimit(0.0, 1.0, (juce::Time::getMillisecondCounterHiRes() - animationStart_) / duration);
    const double eased = 1.0 - std::pow(1.0 - t, 3.0);
    dockHeightShown_ = dockFrom_ + (dockRequested_ - dockFrom_) * eased;
    sideWidthShown_ = sideFrom_ + (sideRequested_ - sideFrom_) * eased;
    if (t >= 1.0)
        stopTimer();
    if (onLayoutChanged)
        onLayoutChanged();
}

// ----- persistence ---------------------------------------------------------------------------

juce::var PanelManager::toJson() const
{
    auto* root = new juce::DynamicObject();
    root->setProperty("version", 1);
    root->setProperty("dockHeight", dockSize_);
    root->setProperty("sideWidth", sideSize_);
    juce::Array<juce::var> panels;
    for (const auto& entry : entries_)
    {
        auto* p = new juce::DynamicObject();
        p->setProperty("id", panelKey(entry->info.id));
        p->setProperty("host", hostKey(entry->host));
        p->setProperty("visible", entry->visible);
        p->setProperty("pinned", entry->window != nullptr ? entry->window->pinned() : entry->pinned);
        const auto bounds = entry->window != nullptr ? entry->window->getBounds() : entry->floatingBounds;
        if (!bounds.isEmpty())
        {
            juce::Array<juce::var> b{bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight()};
            p->setProperty("bounds", b);
        }
        // The order within the host.
        int order = 0;
        if (entry->host != HostKind::floating)
        {
            const auto ids = (entry->host == HostKind::dock ? dock_ : side_).panels();
            for (size_t i = 0; i < ids.size(); ++i)
                if (ids[i] == entry->info.id)
                    order = static_cast<int>(i);
        }
        p->setProperty("order", order);
        panels.add(juce::var(p));
    }
    root->setProperty("panels", panels);
    return juce::var(root);
}

void PanelManager::fromJson(const juce::var& layout)
{
    if (!layout.isObject())
        return;
    // Start from nothing shown, then place as recorded.
    for (auto& entry : entries_)
    {
        if (entry->host == HostKind::floating)
            closeFloating(*entry, false);
        else if (hostComponent(entry->host).hasPanel(entry->info.id))
            hostComponent(entry->host).removePanel(entry->info.id);
        entry->visible = false;
    }
    dockSize_ =
        std::max(theme_.sizes.dockMin, static_cast<int>(layout.getProperty("dockHeight", defaultDockHeight)));
    sideSize_ =
        std::max(theme_.sizes.panelMin, static_cast<int>(layout.getProperty("sideWidth", defaultSideWidth)));
    struct Placement
    {
        Entry* entry;
        int order;
        bool visible;
    };
    std::vector<Placement> placements;
    if (const auto* panels = layout["panels"].getArray())
        for (const auto& p : *panels)
        {
            const auto key = p.getProperty("id", "").toString();
            Entry* entry = nullptr;
            for (auto& e : entries_)
                if (key == panelKey(e->info.id))
                    entry = e.get();
            if (entry == nullptr)
                continue;
            entry->host = hostFromKey(p.getProperty("host", "").toString(), entry->info.defaultHost);
            entry->pinned = static_cast<bool>(p.getProperty("pinned", false));
            if (const auto* b = p["bounds"].getArray(); b != nullptr && b->size() == 4)
                entry->floatingBounds = {static_cast<int>((*b)[0]),
                                         static_cast<int>((*b)[1]),
                                         static_cast<int>((*b)[2]),
                                         static_cast<int>((*b)[3])};
            placements.push_back({entry,
                                  static_cast<int>(p.getProperty("order", 0)),
                                  static_cast<bool>(p.getProperty("visible", false))});
        }
    std::sort(placements.begin(),
              placements.end(),
              [](const Placement& a, const Placement& b) { return a.order < b.order; });
    // Hidden docked panels are added first (so tabs keep their order), the
    // visible one last so it shows.
    for (const auto& placement : placements)
        if (placement.entry->host != HostKind::floating && !placement.visible)
        {
            placeInHost(*placement.entry, -1);
            placement.entry->visible = false;
        }
    for (const auto& placement : placements)
        if (placement.visible)
        {
            if (placement.entry->host == HostKind::floating)
                openFloating(*placement.entry);
            else
            {
                if (!hostComponent(placement.entry->host).hasPanel(placement.entry->info.id))
                    placeInHost(*placement.entry, -1);
                hostComponent(placement.entry->host).showPanel(placement.entry->info.id);
                placement.entry->visible = true;
            }
        }
    // A host with tabs but no visible panel shows its first.
    for (auto* host : {&dock_, &side_})
        if (!host->empty())
        {
            bool any = false;
            for (const auto& entry : entries_)
                if (entry->host == host->kind() && entry->visible)
                    any = true;
            if (!any)
            {
                host->showPanel(host->panels().front());
                if (auto* e = find(host->panels().front()))
                    e->visible = true;
            }
        }
    dockHeightShown_ = dock_.empty() ? 0 : dockSize_;
    sideWidthShown_ = side_.empty() ? 0 : sideSize_;
    refreshHosts();
    for (const auto& entry : entries_)
        if (onPanelVisibilityChanged)
            onPanelVisibilityChanged(entry->info.id, entry->visible);
}

void PanelManager::saveTo(const juce::File& file) const
{
    file.getParentDirectory().createDirectory();
    file.replaceWithText(juce::JSON::toString(toJson(), false));
}

bool PanelManager::loadFrom(const juce::File& file)
{
    if (!file.existsAsFile())
        return false;
    juce::var layout;
    if (juce::JSON::parse(file.loadFileAsString(), layout).failed())
        return false;
    fromJson(layout);
    return true;
}

void PanelManager::setWindowDefaults(int mainWidth, int mainHeight)
{
    // Below 1440 wide the side panel defaults closed; below 900 tall the dock.
    defaultsSideOpen_ = mainWidth >= 1440;
    defaultsDockOpen_ = mainHeight >= 900;
}

void PanelManager::resetLayout()
{
    auto* root = new juce::DynamicObject();
    root->setProperty("dockHeight", defaultDockHeight);
    root->setProperty("sideWidth", defaultSideWidth);
    juce::Array<juce::var> panels;
    int dockOrder = 0, sideOrder = 0;
    for (const auto& entry : entries_)
    {
        auto* p = new juce::DynamicObject();
        p->setProperty("id", panelKey(entry->info.id));
        p->setProperty("host", hostKey(entry->info.defaultHost));
        const bool first = entry->info.defaultHost == HostKind::dock ? dockOrder == 0 : sideOrder == 0;
        const bool open = entry->info.defaultHost == HostKind::dock ? defaultsDockOpen_ : defaultsSideOpen_;
        p->setProperty("visible", first && open);
        p->setProperty("order", entry->info.defaultHost == HostKind::dock ? dockOrder++ : sideOrder++);
        panels.add(juce::var(p));
    }
    root->setProperty("panels", panels);
    fromJson(juce::var(root));
}

void PanelManager::applyPreset(const juce::String& name)
{
    if (name == "Mix")
    {
        resetLayout();
        for (const auto& entry : entries_)
            if (entry->visible && entry->info.id != PanelId::mixer)
                hide(entry->info.id);
        moveTo(PanelId::mixer, HostKind::floating);
        return;
    }
    resetLayout(); // "Edit": the defaults
}

} // namespace aural::ui
