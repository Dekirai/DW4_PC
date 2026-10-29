#pragma once

// --export-presence-art FOLDER: suggested Discord art assets from the
// user's own game data (never shipped): one 512x512 PNG per area group key
// (presence_data.hpp), taken from a loading card of that area. The user
// uploads them in the Discord Developer Portal.
#include <filesystem>

namespace dw4::game {

// Returns the process exit code (0 when at least one picture was written).
int export_presence_art(const std::filesystem::path& data, const std::filesystem::path& folder);

} // namespace dw4::game
