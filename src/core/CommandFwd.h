#pragma once

#include <functional>
#include <memory>

// For headers that only pass commands along (a view's onCommand callback): the
// declaration, without every command's definition (core/Command.h). A .cpp that
// makes or runs commands includes core/Command.h itself.

namespace northlight::core
{

class Command;

} // namespace northlight::core
