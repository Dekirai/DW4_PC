#pragma once

// --export-presence-art FOLDER: suggested Discord art assets from the
// user's own game data (never shipped): one 512x512 PNG per art key
// (presence_data.hpp). "logo" is the title logo (stagIS10's logo.png) on a
// transparent square; an area key gets the stage picture of one of its
// loading cards, exactly the part of the card's texture the loading screen
// shows in its picture quad (M_imageShape), upright and centre-cropped to a
// square at the shape it has on the 4:3 screen. The user uploads them in the
// Discord Developer Portal.
#include <filesystem>

namespace dw4::game {

// Returns the process exit code (0 when at least one picture was written).
int export_presence_art(const std::filesystem::path& data, const std::filesystem::path& folder);

} // namespace dw4::game
