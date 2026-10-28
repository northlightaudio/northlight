#include "app/SystemSettingsWatcher.h"

#include "ui/Theme.h"

#if JUCE_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace northlight
{

#if JUCE_WINDOWS
// A hidden top-level window (a message-only window would miss broadcasts).
struct SystemSettingsWatcher::Window
{
    explicit Window(juce::AsyncUpdater& owner) : owner_(owner)
    {
        static constexpr wchar_t className[] = L"NorthlightSystemSettings";
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = &Window::proc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = className;
        RegisterClassExW(&windowClass); // fails harmlessly when already registered
        hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW,
                                className,
                                L"",
                                WS_POPUP,
                                0,
                                0,
                                0,
                                0,
                                nullptr,
                                nullptr,
                                windowClass.hInstance,
                                nullptr);
        if (hwnd_ != nullptr)
            SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    }

    ~Window()
    {
        if (hwnd_ != nullptr)
            DestroyWindow(hwnd_);
    }

    static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_SETTINGCHANGE || message == WM_SYSCOLORCHANGE)
            if (auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
                self->owner_.triggerAsyncUpdate();
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    juce::AsyncUpdater& owner_;
    HWND hwnd_ = nullptr;
};
#else
struct SystemSettingsWatcher::Window
{
    explicit Window(juce::AsyncUpdater&) {}
};
#endif

SystemSettingsWatcher::SystemSettingsWatcher(std::function<void()> onChanged)
    : onChanged_(std::move(onChanged)), highContrast_(ui::Theme::systemHighContrast()),
      reduceMotion_(ui::Theme::systemReduceMotion()),
      window_(std::make_unique<Window>(static_cast<juce::AsyncUpdater&>(*this)))
{
}

SystemSettingsWatcher::~SystemSettingsWatcher()
{
    cancelPendingUpdate();
}

void SystemSettingsWatcher::handleAsyncUpdate()
{
    const bool highContrast = ui::Theme::systemHighContrast();
    const bool reduceMotion = ui::Theme::systemReduceMotion();
    if (highContrast == highContrast_ && reduceMotion == reduceMotion_)
        return;
    highContrast_ = highContrast;
    reduceMotion_ = reduceMotion;
    if (onChanged_)
        onChanged_();
}

} // namespace northlight
