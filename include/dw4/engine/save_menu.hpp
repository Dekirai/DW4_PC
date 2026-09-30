#pragma once

// The save room (stagIS12, docs/SAVES.md): SAVECTRL (beNDMWSaveCtrl) and one
// SAVEINTF_ALL panel (beNDMWSaveIntf) per controller, with the helpers they
// share with the character select: the memory card worker over SAVEUTILITY,
// the slot window (SELSLOT), the four save boxes (DATASELINTF) and the
// Digimon status windows (DEGISTATECTRL). Only the GameCube paths exist.
#include "dw4/engine/managers.hpp"
#include "dw4/save_data.hpp"

#include <array>
#include <memory>
#include <vector>

namespace dw4::engine {

// Memory card worker (McUtilCtrl, 803675D8 / 80367900..80367B18): one
// SAVEUTILITY request at a time, the deliver record mirrored per slot.
struct CardIo {
    // Request kinds of 80367B18 (the SAVEUTILITY REQUEST_* order).
    enum Request : int { kFileMake, kFileFind, kFormat, kUseCheck, kReadMedia, kWriteMedia, kGetFreeSize };
    struct Slot {
        bool inserted{};             // +8
        std::uint32_t free_bytes{};  // +12
        std::uint32_t need_bytes{};  // +16
        bool enough{};               // +20
        int state{};                 // +28: deliver card state (1 usable)
    };
    bool available{};    // +12: SAVEUTILITY answered GENERATE
    bool ready{};        // +13
    bool pending{};      // +14
    bool all_ready{};    // +15
    bool formatting{};   // +16: the last request was a format
    int slot{};          // +20
    int request{};       // +28
    int result{};        // +32: beSv*Api code (2 done)
    bool done{};         // +36
    int step{};          // +48
    std::vector<std::uint8_t> buffer = std::vector<std::uint8_t>(save::kCopySize - 8); // +40
    std::array<Slot, 2> slots{};

    void generate(Engine& engine);   // 80367900
    bool is_ready(Engine& engine);   // 80367980
    void remove(Engine& engine);     // 80367A18
    bool gone(Engine& engine);       // 80367A60
    void start(int kind, int card);  // 80367AE4
    void update(Engine& engine);     // 80367B18
};

// COMMONINTF of SAVECTRL (8033EC68): the panels' shared state.
struct SaveCommon {
    // A card file as read (80363A64): its four boxes and the players using it.
    struct CardData {
        int slot{};                             // +8
        std::array<save::Record, save::kRecordsPerCopy> records{};
        std::vector<int> users;                 // +16
    };
    struct Player {
        int counter{};                  // +8: frames left in the wait state
        bool entered{}, joined{}, closed{};
        bool active = true;             // +15: panel takes input
        bool card_missing{};            // +16: the card under this player's file left
        int result{};                   // +20: outcome of the card flows
        std::shared_ptr<CardData> card; // +32
        int state{}, sub{}, detail{}, detail2{}; // +36..+48
    };
    bool active{};
    int state{};       // +48: 3 once every panel finished
    int platform = 1;  // +56: GameCube
    int owner{};       // +68: player allowed to touch the card
    std::array<Player, 4> players{};
    std::array<save::Record, 4> records{}; // +36: each player's own character
    std::vector<std::shared_ptr<CardData>> cards; // +32
    CardIo io;                             // +52
    std::uint64_t timer_end{};             // MakeDataTime / SaveDataTime

    bool all_confirmed() const;           // 80360CF8
    void deactivate_all();                // 80360CBC
    void pause_others(int player);        // 803644AC
    void lock(int player, int state);     // 803645EC
    void detach(int player);              // 80364704
    void update(Engine& engine);          // 8036322C

    // Card steps; `level` picks the player counter that holds the step
    // (0 state .. 3 detail2). 0 while running, 1 done, 2 failed (io.result).
    int check(Engine& engine, int player, int card, int level);                        // 8036356C
    int ask(Engine& engine, int player, MessageWindow& message, int text, int prompt, int cursor,
            int level);                                                                 // 80363358
    int create(Engine& engine, int player, MessageWindow& message, int card, int level); // 80363788
    int format(Engine& engine, int player, int card, int level);                        // 803642DC
    int read(Engine& engine, int player, int card, int level);                          // 80363A64
    int write(Engine& engine, int player, int card, int box, int level);                // 80364038
};

// DEGISTATECTRL (80352968..80353394): a Digimon's face and status window for
// the player's own character (mode 1) or a card box (mode 0); the check form
// uses the small j_savecheckwin instead.
class RecordWindow {
public:
    explicit RecordWindow(bool check = false) : check_(check) {}
    void show(int mode, int index) { mode_ = mode; index_ = index; phase_ = 1; step_ = 0; done_ = false; }   // 8035299C
    void change(int mode, int index) { mode_ = mode; index_ = index; phase_ = 3; step_ = 0; done_ = false; } // 803529C4
    void hide(int style) { style_ = style; phase_ = 2; step_ = 0; done_ = false; }                           // 803529EC
    bool update(Engine& engine, CommonModelControl& models, int player, const Vec3f& position,
                const SaveCommon& common); // 80353394
    bool done() const { return done_; }

private:
    const save::Record* record(const SaveCommon& common, int player) const;
    bool check_{};
    int mode_{}, index_{}, style_{}, phase_{}, step_{};
    bool done_ = true;
};

// SELSLOT on the GameCube (80358AF8..8035B448): memory card slot A or B.
class SlotSelect {
public:
    void reset();                                          // 80358B24
    void clear_flags();                                    // 80358AF8
    void set_phase(int phase) { phase_ = phase; step_ = 0; } // 80361060
    void reselect(Engine& engine, CommonModelControl& models, int player); // 8035ADB0
    bool open(Engine& engine, CommonModelControl& models, int player, const Vec3f& position, const CardIo& io,
              int slot = -1);                              // 80358BC0
    bool select(Engine& engine, CommonModelControl& models, int player, const Vec3f& position, bool input,
                SaveCommon& common);                        // 803599AC
    bool close(Engine& engine, CommonModelControl& models, int player); // 8035AE68
    void mark(Engine& engine, CommonModelControl& models, int player, const Vec3f& position, int failed,
              const CardIo& io);                            // 8035B448
    int chosen() const { return chosen_; }       // +64
    bool cancelled() const { return cancelled_; } // +54
    bool skipped() const { return skipped_; }     // +55
    void unselect() { selected_ = false; }        // +53

private:
    void refresh(Engine& engine, int player, const CardIo& io);     // 8035988C
    void slot_marks(Engine& engine, CommonModelControl& models, int player, const Vec3f& position, const CardIo& io);
    int cursor_{};                 // +60
    int chosen_{};                 // +64
    int status_line_ = -2;         // +72
    int phase_{}, step_{};         // +76 / +80
    bool opened_{}, selected_{}, cancelled_{}, skipped_{}, closed_{}; // +52..+56
};

// DATASELINTF (80353A30..80355D14): the four save boxes of a card file with
// the cursor, box names and the status windows.
class DataSelect {
public:
    void open(int player, int mode);  // 80353A30: mode 1 saves
    void reset() { step_ = 0; }       // 80353A68
    int cursor() const { return cursor_; } // +20
    bool cancelled() const { return cancelled_; } // +73
    struct Context {
        Engine& engine;
        CommonModelControl& models;
        MessageWindow& message;
        std::array<RecordWindow, 2>& windows;
        SaveCommon& common;
        Vec3f position;
        int player;
        // Polls a status window (80353394 runs only when asked).
        bool done(int i) { return windows[std::size_t(i)].update(engine, models, player, position, common); }
    };
    bool show_boxes(Context& c);       // 80353AD8
    bool select(Context& c);           // 80354168
    bool close_all(Context& c);        // 80354880
    bool show_detail(Context& c);      // 80354B4C
    bool back_to_boxes(Context& c);    // 80354D8C
    bool hide_windows(Context& c);     // 80355244
    bool reopen(Context& c);           // 803553CC
    bool reopen_cursor(Context& c);    // 80355870
    bool close_boxes(Context& c);      // 80355AEC
    bool refresh_users(Context& c);    // 80355D14

private:
    void open_boxes(Context& c);
    void box_names(Context& c, int player);
    void clear_names(Context& c);
    int player_{}, mode_{}, cursor_{}, step_{};
    bool boxes_{}, window0_{}, window1_{}, cursor_open_{}, cancelled_{}; // +48..+52, +73
    bool called_{};                                                         // +64
};

// SAVECTRL (beNDMWSaveCtrl, update 803788A8, commands 80379184): starts the
// memory card worker, opens the backdrop and the four panels, waits for them,
// then leaves for the lobby (stagML12) or, when nobody plays on, the title.
class SaveCtrlManager final : public Manager {
public:
    SaveCtrlManager() : Manager("beNDMWSaveCtrl") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beNDMWSaveCtrlInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override { (void)engine; rams_.clear(); common_ = {}; }
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    void update(Engine& engine) override;
    SaveCommon& common() { return common_; }

private:
    struct CtrlRam : Ram {
        bool created{};
        int state{}, sub{}, step{}, detail{}; // CTRL +28..+40
    };
    std::vector<std::unique_ptr<CtrlRam>> rams_;
    SaveCommon common_;
};

// SAVEINTF_ALL (beNDMWSaveIntf, update 80379790, commands 8037D1DC): one panel
// per controller: save or not, card slot, box, overwrite check, writing, then
// back to the lobby or end the game.
class SaveIntfManager final : public Manager {
public:
    SaveIntfManager() : Manager("beNDMWSaveIntf") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beNDMWSaveIntfInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override { (void)engine; rams_.clear(); }
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    void update(Engine& engine) override;

private:
    struct PanelRam : Ram {
        bool created{};
        bool entry = true, ready{}, on_control = true; // CTRL +17 / +18 / +19
        bool saved{};                                   // CTRL +23
        int state{}, sub{}, step{}, detail{};           // CTRL +36..+48
        Vec3f position{};
        CommonModelControl models;
        MessageWindow message;
        std::array<RecordWindow, 2> windows{RecordWindow(false), RecordWindow(true)};
        SlotSelect slots;
        DataSelect boxes;
        std::array<int, 8> traced{-1}; // DW4_TRACE_SAVE
    };
    void update_panel(Engine& engine, PanelRam& ram, SaveCommon& common);
    void update_ctrl(Engine& engine, PanelRam& ram, SaveCommon& common, std::uint16_t trigger);
    void card_flow(Engine& engine, PanelRam& ram, SaveCommon& common);
    std::vector<std::unique_ptr<PanelRam>> rams_;
};

} // namespace dw4::engine
