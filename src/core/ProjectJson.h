#pragma once

#include "core/Model.h"

#include <optional>
#include <string>

// Project file format: JSON, versioned by Project::schemaVersion. Media is referenced
// by path, never embedded.

namespace aural::core
{

std::string projectToJson(const Project& project, bool pretty = true);

// Returns nullopt and fills `error` when the text is not a valid project of a
// supported schema version.
std::optional<Project> projectFromJson(const std::string& text, std::string* error = nullptr);

} // namespace aural::core
