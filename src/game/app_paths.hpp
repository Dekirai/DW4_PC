#pragma once

// The game's folders on each platform (docs/ANDROID.md, Storage): one place
// instead of SDL_GetBasePath at every caller (on Android it is "./").
#include <SDL3/SDL.h>

#include <filesystem>
#include <string>

namespace dw4::game {

namespace detail {
inline std::filesystem::path utf8_path(const char* text) {
    return text && *text ? std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text))) : std::filesystem::path();
}
} // namespace detail

// The game's own folder: settings.ini, saves/, mods/ and data/ (the disc
// image the first start copies or converts) live in it. The PC: the
// program's folder (SDL_GetBasePath). Android: the app's external files
// folder (/storage/emulated/0/Android/data/org.dw4pc.game/files), which a
// PC reaches over USB for imports and exports; the internal one when
// external storage is not available. Empty when unknown.
inline std::filesystem::path app_root() {
#ifdef SDL_PLATFORM_ANDROID
    if (SDL_GetAndroidExternalStorageState() & SDL_ANDROID_EXTERNAL_STORAGE_WRITE)
        if (auto path = detail::utf8_path(SDL_GetAndroidExternalStoragePath()); !path.empty()) return path;
    return detail::utf8_path(SDL_GetAndroidInternalStoragePath());
#else
    return detail::utf8_path(SDL_GetBasePath());
#endif
}

// Files no other app or USB host should read: the remembered online login
// (saves/online), online.log and controllers.log. Android: the app's
// internal files folder (/data/data/org.dw4pc.game/files). The PC: the same
// as app_root().
inline std::filesystem::path private_root() {
#ifdef SDL_PLATFORM_ANDROID
    return detail::utf8_path(SDL_GetAndroidInternalStoragePath());
#else
    return app_root();
#endif
}

#ifdef SDL_PLATFORM_ANDROID
// stdout and stderr line by line into logcat (tag "DW4"), from the start of
// main on (platform_android.cpp): the game's own lines, error texts before
// their message boxes, SDL's log.
void route_output_to_logcat();
#endif

} // namespace dw4::game
