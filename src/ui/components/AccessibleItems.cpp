#include "ui/components/AccessibleItems.h"

#include <unordered_map>

#if JUCE_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace northlight::ui
{

namespace
{
bool& forced() noexcept
{
    static bool on = false;
    return on;
}
} // namespace

bool accessibilityClientsActive()
{
    if (forced())
        return true;
#if JUCE_WINDOWS
    // As JUCE decides whether to raise UI Automation events: a screen reader is
    // running, or a client is listening (UiaClientsAreListening, looked up in the
    // UI Automation library JUCE loads, so nothing more is linked).
    BOOL reader = FALSE;
    if (SystemParametersInfoW(SPI_GETSCREENREADER, 0, &reader, 0) != 0 && reader != FALSE)
        return true;
    using Listening = BOOL(WINAPI*)();
    static const auto listening = []() -> Listening
    {
        if (auto* module = LoadLibraryW(L"UIAutomationCore.dll"))
            return reinterpret_cast<Listening>(
                reinterpret_cast<void*>(GetProcAddress(module, "UiaClientsAreListening")));
        return nullptr;
    }();
    return listening != nullptr && listening() != FALSE;
#else
    return false;
#endif
}

void forceAccessibilityClients(bool on)
{
    forced() = on;
}

juce::String spokenPosition(const core::TempoMap& map, core::SamplePosition at)
{
    auto bb = map.barsBeatsAt(at);
    // An end one sample short of a beat (a clip's, a region's) reads as that beat.
    const auto soon = map.barsBeatsAt(at + static_cast<core::SamplePosition>(map.sampleRate() / 1000.0));
    if (bb.tick != 0 && (soon.bar != bb.bar || soon.beat != bb.beat))
        bb = {soon.bar, soon.beat, 0};
    auto text = "bar " + juce::String(bb.bar) + " beat " + juce::String(bb.beat);
    return bb.tick == 0 ? text : text + " tick " + juce::String(bb.tick);
}

// A stand-in: no paint, no mouse, no keyboard focus; only a handler that reads
// its item. A list or group holds its children's stand-ins.
class AccessibleItems::Proxy final : public juce::Component
{
public:
    Proxy()
    {
        setInterceptsMouseClicks(false, false);
        setPaintingIsUnclipped(true);
    }

    AccessibleItem item; // without its children
    juce::String path;   // the keys from the top, for the current item
    std::vector<std::unique_ptr<Proxy>> children;

    void update(const AccessibleItem& next)
    {
        const bool rebuilt = next.role != item.role ||
                             static_cast<bool>(next.press) != static_cast<bool>(item.press) ||
                             static_cast<bool>(next.showMenu) != static_cast<bool>(item.showMenu) ||
                             next.checkable != item.checkable;
        const bool renamed = next.title != item.title;
        item.key = next.key;
        item.role = next.role;
        item.title = next.title;
        item.bounds = next.bounds;
        item.selectable = next.selectable;
        item.selected = next.selected;
        item.checkable = next.checkable;
        item.checked = next.checked;
        item.current = next.current;
        item.press = next.press;
        item.showMenu = next.showMenu;
        const bool group =
            item.role == juce::AccessibilityRole::list || item.role == juce::AccessibilityRole::group;
        setFocusContainerType(group ? FocusContainerType::focusContainer : FocusContainerType::none);
        if (rebuilt)
            invalidateAccessibilityHandler();
        else if (renamed)
            if (auto* handler = getAccessibilityHandler())
                handler->notifyAccessibilityEvent(juce::AccessibilityEvent::titleChanged);
    }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        juce::AccessibilityActions actions;
        // Copies: what they do may rebuild the layer.
        if (item.press)
            actions.addAction(item.checkable ? juce::AccessibilityActionType::toggle
                                             : juce::AccessibilityActionType::press,
                              [this]
                              {
                                  if (auto press = item.press)
                                      press();
                              });
        if (item.showMenu)
            actions.addAction(juce::AccessibilityActionType::showMenu,
                              [this]
                              {
                                  if (auto show = item.showMenu)
                                      show();
                              });
        return std::make_unique<Handler>(*this, std::move(actions));
    }

private:
    class Handler final : public juce::AccessibilityHandler
    {
    public:
        Handler(Proxy& proxy, juce::AccessibilityActions actions)
            : juce::AccessibilityHandler(proxy, proxy.item.role, std::move(actions)), proxy_(proxy)
        {
        }
        juce::String getTitle() const override { return proxy_.item.title; }
        juce::AccessibleState getCurrentState() const override
        {
            auto state = juce::AccessibilityHandler::getCurrentState();
            if (proxy_.item.selectable)
                state = state.withSelectable();
            if (proxy_.item.selected)
                state = state.withSelected();
            if (proxy_.item.checkable)
                state = state.withCheckable();
            if (proxy_.item.checked)
                state = state.withChecked();
            return state;
        }

    private:
        Proxy& proxy_;
    };
};

AccessibleItems::AccessibleItems(juce::Component& owner, juce::String title, Build build)
    : owner_(owner), build_(std::move(build))
{
    setTitle(title);
    setInterceptsMouseClicks(false, false);
    setPaintingIsUnclipped(true);
    setFocusContainerType(FocusContainerType::focusContainer);
    startTimer(1000);
}

AccessibleItems::~AccessibleItems()
{
    stopTimer();
    proxies_.clear();
}

std::unique_ptr<juce::AccessibilityHandler> AccessibleItems::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::group);
}

bool AccessibleItems::wanted() const
{
    // Forced (--screen-reader, tests): views not on screen too, so tests need no window.
    return accessibilityClientsActive() && (forced() || owner_.isShowing());
}

void AccessibleItems::invalidate()
{
    dirty_ = true;
    if (active_ && getTimerInterval() != 100)
        startTimer(100);
}

void AccessibleItems::timerCallback()
{
    const bool active = wanted();
    if (active != active_ || (dirty_ && active))
        rebuild();
    if (getTimerInterval() != 1000)
        startTimer(1000);
}

void AccessibleItems::rebuild()
{
    dirty_ = false;
    active_ = wanted();
    if (!active_)
    {
        proxies_.clear();
        currentKey_.clear();
        return;
    }
    const auto items = build_();
    Proxy* current = nullptr;
    apply(*this, proxies_, items, {}, current);
    if (current == nullptr)
    {
        currentKey_.clear();
        return;
    }
    if (current->path == currentKey_)
        return;
    currentKey_ = current->path;
    // The keyboard moved to another item: the reader follows, so it says what is
    // now selected. The keyboard focus stays with the owner.
    if (owner_.hasKeyboardFocus(false))
        if (auto* handler = current->getAccessibilityHandler())
            handler->grabFocus();
}

void AccessibleItems::apply(juce::Component& parent,
                            std::vector<std::unique_ptr<Proxy>>& proxies,
                            const std::vector<AccessibleItem>& items,
                            juce::Point<int> origin,
                            Proxy*& current)
{
    const auto area = parent.getLocalBounds() + origin; // in the owner's coordinates
    const auto* parentProxy = dynamic_cast<const Proxy*>(&parent);
    std::unordered_map<juce::String, size_t> was;
    was.reserve(proxies.size());
    for (size_t i = 0; i < proxies.size(); ++i)
        was.emplace(proxies[i]->item.key, i);

    std::vector<std::unique_ptr<Proxy>> next;
    next.reserve(items.size());
    for (const auto& item : items)
    {
        const auto bounds = item.bounds.getIntersection(area);
        if (bounds.isEmpty())
            continue;
        std::unique_ptr<Proxy> proxy;
        if (const auto at = was.find(item.key); at != was.end() && proxies[at->second] != nullptr)
            proxy = std::move(proxies[at->second]);
        else
            proxy = std::make_unique<Proxy>();
        proxy->update(item);
        proxy->path = (parentProxy != nullptr ? parentProxy->path + "/" : juce::String()) + item.key;
        const auto local = bounds - origin;
        if (proxy->getBounds() != local)
            proxy->setBounds(local);
        if (proxy->getParentComponent() != &parent)
            parent.addAndMakeVisible(*proxy);
        // Read in the order given (JUCE would go by position: top to bottom).
        if (proxy->getExplicitFocusOrder() != static_cast<int>(next.size()) + 1)
            proxy->setExplicitFocusOrder(static_cast<int>(next.size()) + 1);
        if (item.current)
            current = proxy.get();
        apply(*proxy, proxy->children, item.children, bounds.getPosition(), current);
        next.push_back(std::move(proxy));
    }
    proxies = std::move(next); // the ones left over go, and leave their parent
}

juce::Component* AccessibleItems::find(const std::vector<juce::String>& path) const
{
    const std::vector<std::unique_ptr<Proxy>>* level = &proxies_;
    Proxy* found = nullptr;
    for (const auto& key : path)
    {
        found = nullptr;
        for (const auto& proxy : *level)
            if (proxy->item.key == key)
                found = proxy.get();
        if (found == nullptr)
            return nullptr;
        level = &found->children;
    }
    return found;
}

int AccessibleItems::count() const
{
    int n = 0;
    std::function<void(const std::vector<std::unique_ptr<Proxy>>&)> add =
        [&](const std::vector<std::unique_ptr<Proxy>>& level)
    {
        for (const auto& proxy : level)
        {
            ++n;
            add(proxy->children);
        }
    };
    add(proxies_);
    return n;
}

} // namespace northlight::ui
