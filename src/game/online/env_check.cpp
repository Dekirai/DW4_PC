#include "env_check.hpp"


namespace dw4::game::online {
namespace {

// What the engine is built from or what it simulates: cheats and their
// switch, the fixed random and clock replays, injected players and records,
// card slots, test starts and counters, load timing, the randomizer, mods,
// state tests and the direct netplay test hooks.
constexpr std::string_view kForbidden[] = {
    "DW4_AX_PHASE",       "DW4_BITS",           "DW4_CARD_A",         "DW4_CARD_B",         "DW4_CARD_RECORD",
    "DW4_CHEATS_FROM_ENV", "DW4_CLOCK_FILE",    "DW4_COUNTERS",       "DW4_DEVICES",        "DW4_DIFFICULTY",
    "DW4_DISC_CLOCK",     "DW4_DISKS",          "DW4_DISKS_ON_LOAD",  "DW4_ENTRY",          "DW4_FAST_LOADING",
    "DW4_FIXED_RANDOM",   "DW4_FLAG_FOLDER",    "DW4_FORMS",          "DW4_GAMEPLAY",       "DW4_GOD",
    "DW4_INFINITE_MP",    "DW4_LEVEL",          "DW4_LEVEL_ON_LOAD",  "DW4_MENU_HOST",      "DW4_MENU_IMPORT",
    "DW4_MENU_JOIN",      "DW4_MODS",           "DW4_ONE_HIT",        "DW4_PLAYERS",        "DW4_RANDOMIZER",
    "DW4_RANDOMIZER_BIND", "DW4_RANDOMIZER_SWEEP", "DW4_STATE_FILE",  "DW4_STATE_TEST",   "DW4_XDATA",
};

// Presentation (pictures, windows, audio output and dumps, name tags, menus),
// diagnostics (every DW4_TRACE_*, scene and draw dumps, profiles), the host
// around the engine (the local save store, Discord, achievements), the test
// bots and harness switches (their pads travel as input like a player's) and
// the online client's own switches.
constexpr std::string_view kAllowed[] = {
    "DW4_ACHIEVEMENTS", "DW4_AUDIO_DUMP", "DW4_AUDIO_DUMP_STREAMS", "DW4_AUDIO_STREAMS_CHANNELS", "DW4_AUDIO_TOPUP",
    "DW4_AVOID_MATS", "DW4_AX_COMPRESSOR", "DW4_CAMPAIGN_CHECKPOINTS", "DW4_CAMPAIGN_CHECKPOINT_LIST", "DW4_CAMPAIGN_IDLE",
    "DW4_CAMPAIGN_LOAD", "DW4_CAMPAIGN_REACH", "DW4_CAMPAIGN_REACH_CLOSE", "DW4_CAMPAIGN_REACH_EACH", "DW4_CAMPAIGN_REACH_FROM",
    "DW4_CAMPAIGN_REGIONS", "DW4_CAMPAIGN_STALL_DUMP", "DW4_CAMPAIGN_STOP", "DW4_CARD_DIR", "DW4_DEV_DUMP",
    "DW4_DEV_DUMP_DIR", "DW4_DEV_HIDE", "DW4_DEV_IDS", "DW4_DEV_MSAA", "DW4_DEV_PICTURES",
    "DW4_DEV_PRESENT", "DW4_DEV_SETTINGS", "DW4_DEV_SMOOTH", "DW4_DEV_WIDESCREEN", "DW4_DEV_WINDOW",
    "DW4_DISCORD_PIPE", "DW4_DRAW_DUMP", "DW4_DUMP_SEQ", "DW4_DUMP_TEXTURES", "DW4_FARM_FIELD",
    "DW4_FARM_TEST", "DW4_LABELS", "DW4_MAT_SEQUENCE", "DW4_MAT_SWITCH", "DW4_MENU_TAB",
    "DW4_MIGRATE_FROM", "DW4_MSAA", "DW4_NO_ERROR_BOX", "DW4_ONLINE_CALIBRATION", "DW4_ONLINE_PASSWORD",
    "DW4_ONLINE_PIN", "DW4_ONLINE_RATE", "DW4_ONLINE_SERVER", "DW4_ONLINE_TRACE", "DW4_OPEN_MENU",
    "DW4_PRESENCE_ART_ALL", "DW4_PROFILE", "DW4_RENDER", "DW4_SCENE_REPORT", "DW4_SEEK_DIRECT",
    "DW4_SMOKE_STAGES", "DW4_STAGE_STATES", "DW4_TEXTURE_PACK", "DW4_TRACE_ACTION", "DW4_TRACE_BONES",
    "DW4_TRACE_BOSSBOX", "DW4_TRACE_CAMERA", "DW4_TRACE_CAMERA_COMMANDS", "DW4_TRACE_CAMPAIGN", "DW4_TRACE_CRI_RING",
    "DW4_TRACE_DAMAGE", "DW4_TRACE_DB", "DW4_TRACE_EFFECT", "DW4_TRACE_EVENTS", "DW4_TRACE_EXPLORE",
    "DW4_TRACE_FINGERPRINT", "DW4_TRACE_FOLLOW", "DW4_TRACE_GIMMICK", "DW4_TRACE_GRID", "DW4_TRACE_GUARD",
    "DW4_TRACE_HIT", "DW4_TRACE_HITBY", "DW4_TRACE_HITLAND", "DW4_TRACE_HOMING", "DW4_TRACE_INFOS",
    "DW4_TRACE_ITEM", "DW4_TRACE_LABELS", "DW4_TRACE_LOAD", "DW4_TRACE_LOADER", "DW4_TRACE_LOADER_CHUNKS",
    "DW4_TRACE_LOADTIME", "DW4_TRACE_LOCK", "DW4_TRACE_LUACALL", "DW4_TRACE_LUA_STACK", "DW4_TRACE_MESSAGE_OBJECTS",
    "DW4_TRACE_MODELS", "DW4_TRACE_MOTION", "DW4_TRACE_MOTION_ALL", "DW4_TRACE_NEAR", "DW4_TRACE_NO_RECIPIENT",
    "DW4_TRACE_PANEL", "DW4_TRACE_PARTICLES", "DW4_TRACE_PLAYER", "DW4_TRACE_POSTS", "DW4_TRACE_PRESENCE",
    "DW4_TRACE_RECORDER", "DW4_TRACE_RIDE", "DW4_TRACE_ROUTE", "DW4_TRACE_SAVE", "DW4_TRACE_SCHEDULE",
    "DW4_TRACE_SCRIPT", "DW4_TRACE_SELECT", "DW4_TRACE_SENDS", "DW4_TRACE_SEQ", "DW4_TRACE_SEQINT",
    "DW4_TRACE_SHADOW", "DW4_TRACE_SKILL", "DW4_TRACE_SOUND", "DW4_TRACE_STATS", "DW4_TRACE_STATUS",
    "DW4_TRACE_STICK", "DW4_TRACE_TEXT", "DW4_TRACE_TURN", "DW4_TRACE_VIEW", "DW4_TRACE_VIEW_PATH",
    "DW4_TRACE_WALLBITS", "DW4_TRACE_WATER", "DW4_TRACE_XBOX", "DW4_WIDESCREEN", "DW4_WINDOW",
};

} // namespace

std::span<const std::string_view> online_forbidden_env() { return kForbidden; }

std::span<const std::string_view> online_allowed_env() { return kAllowed; }

std::optional<std::string> first_forbidden_env(const char* (*lookup)(const char*)) {
    for (const auto name : kForbidden) {
        const std::string key(name);
        if (const char* value = lookup(key.c_str()); value && *value) return key;
    }
    return std::nullopt;
}

std::string env_refusal(std::string_view variable) {
    return "Online play is locked while the test variable " + std::string(variable) +
           " is set (it changes the game and would break the session). Unset it and start the game again.";
}

} // namespace dw4::game::online
