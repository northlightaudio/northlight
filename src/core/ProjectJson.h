#pragma once

#include "core/Model.h"

#include <optional>
#include <string>

// Project file format: JSON, versioned by Project::schemaVersion. Media is referenced
// by path, never embedded.

namespace northlight::core
{

// Names processors' parameters stably (a built-in's parameter id, a plugin's
// own parameter id), so a saved project survives a processor adding, removing
// or reordering parameters. The app supplies it; core knows no processor.
struct ParameterLayout
{
    std::vector<std::string> keys; // in the processor's current order
    std::vector<float> defaults;   // the same length
};

class ParameterKeys
{
public:
    virtual ~ParameterKeys() = default;
    // The insert's layout now; empty when unknown (a plugin not installed or not
    // yet instantiated).
    virtual ParameterLayout layout(const Insert& insert) const = 0;
};

// With `keys`, each insert's parameters and each parameter lane are saved with
// their keys (as well as their positions, which files without keys use).
std::string projectToJson(const Project& project, bool pretty = true, const ParameterKeys* keys = nullptr);

// Returns nullopt and fills `error` when the text is not a valid project of a
// supported schema version.
std::optional<Project> projectFromJson(const std::string& text, std::string* error = nullptr);

// Puts loaded parameters where each processor wants them now: values by key
// (defaults for new parameters, dropped for removed ones), lanes by key (a lane
// whose parameter is gone is removed). Inserts with no layout yet are left as
// they are. Returns whether anything changed.
bool reconcileParameters(Project& project, const ParameterKeys& keys);

} // namespace northlight::core
