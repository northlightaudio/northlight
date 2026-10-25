#pragma once

#include "core/Model.h"

namespace northlight::core
{

// An insert parameter a command sets (or restores on undo), so the session can
// push exactly those to processors that own their parameters
// (Command::touchedParameters). Its own header so the engine need not see
// every command.
struct ParameterTouch
{
    Id insertId = invalidId;
    int parameter = 0;
};

} // namespace northlight::core
