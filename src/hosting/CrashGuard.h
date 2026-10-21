#pragma once

namespace northlight::hosting
{

// Runs fn(context) and catches a hardware exception a plugin raises inside it
// (an access violation, a divide by zero, or a C++ exception thrown across the
// plugin boundary). Returns false when it crashed. Windows only (structured
// exception handling); elsewhere it calls fn and returns true.
//
// This contains a crash, it does not undo one: the plugin's memory (and maybe
// the process heap) may be damaged, so the caller stops calling the plugin and
// the user is told to save. Real-time safe: no allocation, no locks.
bool callGuarded(void (*fn)(void* context), void* context) noexcept;

// A lambda (or other callable) through callGuarded, without allocating.
template <typename F> bool callGuarded(F& f) noexcept
{
    return callGuarded([](void* context) { (*static_cast<F*>(context))(); }, &f);
}

} // namespace northlight::hosting
