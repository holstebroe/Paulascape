#pragma once

#include <string>

namespace paulascape {

// Blocking native file dialogs. Return an empty string when cancelled or unavailable.
// filterName/filterExt describe one filter, e.g. "MOD files", "mod".
std::string openFileDialog(const std::string& title, const std::string& filterName, const std::string& filterExt);
std::string saveFileDialog(const std::string& title, const std::string& defaultName, const std::string& filterName, const std::string& filterExt);

} // namespace paulascape
