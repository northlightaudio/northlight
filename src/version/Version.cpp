#include "version/Version.h"

#include "NorthlightVersion.h" // generated into the build tree on every build

#include <cstring>

namespace northlight::version
{

const char* number() noexcept
{
    return NORTHLIGHT_VERSION_NUMBER;
}

const char* label() noexcept
{
    return NORTHLIGHT_VERSION_LABEL;
}

const char* commit() noexcept
{
    return NORTHLIGHT_GIT_COMMIT;
}

bool dirty() noexcept
{
    return NORTHLIGHT_GIT_DIRTY != 0;
}

namespace
{
std::string withLabel()
{
    std::string text = number();
    if (std::strlen(label()) > 0)
        text += std::string("-") + label();
    return text;
}
} // namespace

std::string full()
{
    std::string text = withLabel();
    if (std::strlen(commit()) > 0)
        text += std::string("+") + commit() + (dirty() ? ".dirty" : "");
    return text;
}

std::string display()
{
    std::string text = withLabel();
    if (std::strcmp(label(), "dev") == 0 && std::strlen(commit()) > 0)
        text += std::string(" \xc2\xb7 ") + commit() + (dirty() ? "*" : "");
    return text;
}

} // namespace northlight::version
