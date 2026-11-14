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
// PC reaches over USB for imports and exports. Never the internal one
// instead: that would be a new first start without the player's settings,
// saves and disc copy (main stops with a message when the external folder
// cannot be written, app_root_problem). Empty when unknown.
inline std::filesystem::path app_root() {
#ifdef SDL_PLATFORM_ANDROID
    return detail::utf8_path(SDL_GetAndroidExternalStoragePath());
#else
    return detail::utf8_path(SDL_GetBasePath());
#endif
}

#ifdef SDL_PLATFORM_ANDROID
// Why the game cannot use app_root() (external storage not mounted or not
// writable), or empty.
inline std::string app_root_problem() {
    if ((SDL_GetAndroidExternalStorageState() & SDL_ANDROID_EXTERNAL_STORAGE_WRITE) && !app_root().empty()) return {};
    return "The game's folder (Android/data/org.dw4pc.game/files) cannot be written: the device's shared storage is not "
           "available. The settings, saves and the disc copy live there; start the game again when the storage is back.";
}
#endif

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
// The language packs the APK carries (assets lang/<code>/: the repository's
// data/lang, language.ini and textures.tsv; never a game.tsv) into
// lang_root (app_root()/lang, where the PC build puts them next to the
// program), each file written only when it differs. Other files there (a
// game.tsv the player copied in) are left alone (platform_android.cpp).
void install_bundled_language_packs(const std::filesystem::path& lang_root);
// A system property (getprop), empty when it is unset or the app may not
// read it: the AYN Thor's controller style, persist.sys.gamepad.type.
std::string android_property(const char* name);
// The rate the game presents at, asked of the display for the game's
// surface (DW4Activity.setFrameRate: Surface.setFrameRate, fixed source):
// the Thor's top screen runs 60 or 120 Hz.
void android_set_frame_rate(float hz);
#endif

} // namespace dw4::game
