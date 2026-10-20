#include "hosting/CrashGuard.h"

namespace aural::hosting
{

// Kept to a plain function with no objects that need unwinding: MSVC allows
// __try only where no C++ destructors run in the same frame.
bool callGuarded(void (*fn)(void* context), void* context) noexcept
{
#if defined(_MSC_VER)
    __try
    {
        fn(context);
        return true;
    }
    __except (1 /* EXCEPTION_EXECUTE_HANDLER */)
    {
        return false;
    }
#else
    fn(context);
    return true;
#endif
}

} // namespace aural::hosting
