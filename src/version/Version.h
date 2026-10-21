#pragma once

#include <string>

// The application's version (CONTRIBUTING.md, "Versions and releases"). The number
// comes from project() in the top-level CMakeLists.txt, the only place it is set;
// the label and the git commit are added at build time.
namespace northlight::version
{

// "0.2.0": MAJOR.MINOR.PATCH.
const char* number() noexcept;
// "dev" for development builds, "beta.1" and the like for pre-releases, empty for
// a final release (the NORTHLIGHT_VERSION_LABEL CMake option).
const char* label() noexcept;
// The short git commit the build was made from; empty when built outside git.
const char* commit() noexcept;
// True when the build had uncommitted changes to tracked files.
bool dirty() noexcept;

// The full version, for crash reports and bug reports:
// "0.2.0-dev+1cb6f30", "0.2.0-dev+1cb6f30.dirty", "0.2.0-beta.1+9a8b7c6",
// "0.2.0+9a8b7c6".
std::string full();
// The version as people read it, for the About box and splash: "0.2.0-dev · 1cb6f30"
// for a development build (the commit tells builds apart), else "0.2.0-beta.1" or
// "0.2.0".
std::string display();

} // namespace northlight::version
