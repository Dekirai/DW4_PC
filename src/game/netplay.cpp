#include "netplay.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_handle = SOCKET;
#define DW4_INVALID_SOCKET INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_handle = int;
#define DW4_INVALID_SOCKET (-1)
#endif

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace dw4::game {
namespace {

constexpr std::uint32_t kProtocol = 21; // 21: a locked effect form (and a MOVERL follow) takes 8030CB2C's position-lock path 0x8030CD54 instead of 8030A448: no channel step, turn or KEIKATIMER query, placed on the kept source node plus the offset (without the GENERATE rotation) before the wait, subcommands and walls, so its hitboxes fire from this update's placement; the next MOVE / MOVEEX / MOVERL ends the lock (8030A71C) and the effect moves on; the translated byte +78 keeps its value while locked; 20: GENERATE empties the RAM's motion channel list like the fresh info work (802C6340 / 802C6D18 field 39), so a remade bullet accelerates from 0 again (Megadramon bolts); the rest of the fresh work too: HP 0 -> 100 unless the generator set it, record -1, no status work (DOWN / UP values, poison), no subcommands, player Spiral / Slam / combo state, GAMERAM UPDATA[0..6] / DOWNDATA[0..5] cleared, SETMETAL in dng204b1..dng205b3 (8034D61C); the motion step 8030A448 does nothing while the channel list is empty and rebuilds the basis only when an applied angle changed (8030C85C), so a ride's turn, a follow's tilt and a placement's X / Z rotation stay (the stag0203 train and stag0103 boat carry the player's heading); the HitLand candidate pass 803009D0 copies a kind 0x400 child's model matrix at query time (80128CF4 at 0x80300CE0), so a model moved earlier in the frame (the ridden stag0203 car, boats, moving floors) is met where it is now, not where it was before ModelCtrl ran; 19: the MODELCTRL command loop 8030C3AC walks the list and index it loaded at entry; a sequence a LUACALL / AIMAP script or an ACTION start selects inside it runs nested, then the old list continues and its index becomes the new sequence's cursor (only JUMP skips the store; Lucemon SQ_SWITCH7 inherits index 4), the AIMAP WPos script no longer cuts the loop; delayed model subcommands (SETEFFECT, HITBOX) and the Slam double-tap timer count the system frame step (world time delta / 16666667, 80310754 / 8034C78C); the player's Atck02 wait still checks the action end after releasing A selected SQ_WAIT (8034D47C); 18: a FADESET that restarts the running ACTION fade timer keeps its last sample (8031DE44) and the fade value is no longer clamped to [0, 1] (802F057C): erase effects of defeated enemies stay full longer; a DW4_CARD_RECORD player gets its form and status once the stage database is loaded; 17: SAVEUTILITY runs the beSv*Api requests on the simulated memory card (card_timing.hpp: mount, file work and Dolphin's EXI access times), beSvStartApi / beSvEndApi take two updates, the logo reports missing, full and broken cards (803B18BC state 1, sub 50); the box camera blends its rotations with the normalized linear blend 8012C15C and writes them with 801299D8 (was a spherical blend), translation fused (8012D3F4): camera-relative steering differs during box blends; SOUND entries like the Cri object (four SE stream channels with the original choice, serials, a 50 ms repeat guard per sound effect, the replaced stream fades only while running and unpaused, VOLUMECTL2 fades through the fade table, stream volume + base volume, SESTOP stops every effect); the stage loader reads the final chunk of the last queued file on the simulated drive 14 fields before its load ends (state 4 ends no earlier than three fields after that booking); 16: SOUND ISREADY / ISALLREADY follow the simulated disc reads of the ADX streams (disc_timing.hpp: drive timing, CRI reader, decoder, renderer); a replaced BGM stops when its fade-out reaches -21 dB (802F53B8); 15: water surfaces run only while in view and restart their drop cycle when they come back (80317EA8); GENERATE resets the turn rates (0, players Y 7); messenger index -2 = the first free RAM; KIND_OBJECT GENERATE with ints takes string0/int0 as its owner; 14: two-string GENERATEs of effect and bullet kinds use the effect form (placed at their source); 13: walking and turning need the held stick direction mask (activation radius 0.4, was magnitude > 0.25); beResetNode keeps only the translation (stagBS04 Daemon ATCKON1 route via its recorder); 12: named RAM timers of generators, camera shake, quick panel and status, AIH homing turn and aim height, motion end exactly 0 kept, water period overshoot; 11: posted panel/GAMERAM effects and HUD values, quick panel line mode 2; MOTIONSET cross-fades blend the track quaternions (bone matrices during fades); named RAM timers AITIMER / ATDELAY / KEIKATIMER (channel, turn and MUTEKI steps), CHKBULLET turns to the bullet matrix; new-game UNIQUE from the world clock (was std::random_device); 10: managers wait for the active stage, mat RAM init pass; 9: GENERATE starts with empty sequence labels (respawn SQ_ENTR dual section); 8: once-played clips hold at duration - 1/30, bake from the last traversal, PLAYERgetPos, box camera timer; 7: MOTIONSET restarts, MOVEEX rotation, per-player base flags; 6: chained effects shoot for their root model, frame camera first; 5: HUD attack-delay ring; 4: first-info order; 3: NTSC clock, generator
constexpr const char* kGame = "GDJEB2";
enum : std::uint8_t { kHello = 1, kWelcome, kStart, kInput, kFingerprint, kDesync, kLeave, kReject };

socket_handle to_socket(std::intptr_t value) { return static_cast<socket_handle>(value); }

void close_socket(std::intptr_t value) {
    if (value == std::intptr_t(DW4_INVALID_SOCKET) || value == -1) return;
#ifdef _WIN32
    closesocket(to_socket(value));
#else
    close(to_socket(value));
#endif
}

void make_non_blocking(socket_handle s) {
#ifdef _WIN32
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof one);
}

bool would_block() {
#ifdef _WIN32
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EWOULDBLOCK || errno == EAGAIN;
#endif
}

struct Writer {
    std::vector<std::uint8_t> out;
    void u8(std::uint8_t v) { out.push_back(v); }
    void u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) out.push_back(std::uint8_t(v >> (8 * i)));
    }
    void u64(std::uint64_t v) {
        for (int i = 0; i < 8; ++i) out.push_back(std::uint8_t(v >> (8 * i)));
    }
    void f32(float v) {
        std::uint32_t bits;
        std::memcpy(&bits, &v, 4);
        u32(bits);
    }
    void bytes(const std::vector<std::uint8_t>& v) {
        u32(std::uint32_t(v.size()));
        out.insert(out.end(), v.begin(), v.end());
    }
    void text(const std::string& v) {
        u32(std::uint32_t(v.size()));
        out.insert(out.end(), v.begin(), v.end());
    }
    void pad(const PadSample& p) {
        u8(p.connected ? 1 : 0);
        f32(p.left.x);
        f32(p.left.y);
        f32(p.right.x);
        f32(p.right.y);
        u32(p.buttons);
    }
    void cards(const std::vector<CardFile>& files) {
        u32(std::uint32_t(files.size()));
        for (const auto& f : files) {
            text(f.name);
            bytes(f.bytes);
        }
    }
};

struct Reader {
    const std::vector<std::uint8_t>& in;
    std::size_t at = 0;
    void need(std::size_t n) {
        if (at + n > in.size()) throw std::runtime_error("short network message");
    }
    std::uint8_t u8() {
        need(1);
        return in[at++];
    }
    std::uint32_t u32() {
        need(4);
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= std::uint32_t(in[at++]) << (8 * i);
        return v;
    }
    std::uint64_t u64() {
        need(8);
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= std::uint64_t(in[at++]) << (8 * i);
        return v;
    }
    float f32() {
        const std::uint32_t bits = u32();
        float v;
        std::memcpy(&v, &bits, 4);
        return v;
    }
    std::vector<std::uint8_t> bytes() {
        const auto n = u32();
        need(n);
        std::vector<std::uint8_t> v(in.begin() + std::ptrdiff_t(at), in.begin() + std::ptrdiff_t(at + n));
        at += n;
        return v;
    }
    std::string text() {
        const auto v = bytes();
        return std::string(v.begin(), v.end());
    }
    PadSample pad() {
        PadSample p;
        p.connected = u8() != 0;
        p.left.x = f32();
        p.left.y = f32();
        p.right.x = f32();
        p.right.y = f32();
        p.buttons = u32();
        return p;
    }
    std::vector<CardFile> cards() {
        std::vector<CardFile> files(u32());
        if (files.size() > 127) throw std::runtime_error("too many card files");
        for (auto& f : files) {
            f.name = text();
            f.bytes = bytes();
        }
        return files;
    }
};

} // namespace

struct NetSession::Peer {
    std::intptr_t socket = -1;
    std::vector<std::uint8_t> in, out;
    int slot = -1;
    bool hello{};
    bool closed{};
    std::vector<CardFile> cards;
    std::uint64_t last_frame{}; // highest input frame received
};

NetSession::NetSession() {
#ifdef _WIN32
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
#endif
}

NetSession::~NetSession() {
    for (auto& peer : peers_) close_socket(peer->socket);
    close_socket(listener_);
#ifdef _WIN32
    WSACleanup();
#endif
}

bool NetSession::host(int port, int players, int delay, std::uint64_t data_hash, std::string& error, bool loopback) {
    const socket_handle s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == DW4_INVALID_SOCKET) {
        error = "cannot create a socket";
        return false;
    }
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof one);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(loopback ? INADDR_LOOPBACK : INADDR_ANY);
    address.sin_port = htons(std::uint16_t(port));
    if (bind(s, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0 || listen(s, 4) != 0) {
        close_socket(std::intptr_t(s));
        error = "cannot listen on port " + std::to_string(port);
        return false;
    }
    make_non_blocking(s);
    listener_ = std::intptr_t(s);
    role_ = Role::host;
    slot_ = 0;
    players_wanted_ = std::clamp(players, 1, 4);
    delay_ = std::clamp(delay, 1, 30);
    data_hash_ = data_hash;
    joined_ = {};
    joined_[0] = true;
    return true;
}

bool NetSession::join(const std::string& host_name, int port, std::uint64_t data_hash, std::vector<CardFile> card_a,
                      std::string& error) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (getaddrinfo(host_name.c_str(), std::to_string(port).c_str(), &hints, &found) != 0 || !found) {
        error = "cannot find host " + host_name;
        return false;
    }
    const socket_handle s = socket(found->ai_family, found->ai_socktype, found->ai_protocol);
    const bool connected = s != DW4_INVALID_SOCKET && connect(s, found->ai_addr, int(found->ai_addrlen)) == 0;
    freeaddrinfo(found);
    if (!connected) {
        if (s != DW4_INVALID_SOCKET) close_socket(std::intptr_t(s));
        error = "cannot connect to " + host_name + ":" + std::to_string(port);
        return false;
    }
    make_non_blocking(s);
    auto peer = std::make_unique<Peer>();
    peer->socket = std::intptr_t(s);
    peer->slot = 0; // the host
    peers_.push_back(std::move(peer));
    role_ = Role::guest;
    data_hash_ = data_hash;
    card_a_ = std::move(card_a);
    Writer hello;
    hello.u32(kProtocol);
    hello.text(kGame);
    hello.u64(data_hash_);
    hello.cards(card_a_);
    send(*peers_.front(), kHello, hello.out);
    return true;
}

void NetSession::send(Peer& peer, std::uint8_t type, const std::vector<std::uint8_t>& payload) {
    if (peer.closed) return;
    peer.out.push_back(type);
    const auto size = std::uint32_t(payload.size());
    for (int i = 0; i < 4; ++i) peer.out.push_back(std::uint8_t(size >> (8 * i)));
    peer.out.insert(peer.out.end(), payload.begin(), payload.end());
}

void NetSession::broadcast(std::uint8_t type, const std::vector<std::uint8_t>& payload, int except_slot) {
    for (auto& peer : peers_)
        if (peer->hello && peer->slot != except_slot) send(*peer, type, payload);
}

void NetSession::drop_peer(Peer& peer) {
    if (peer.closed) return;
    peer.closed = true;
    close_socket(peer.socket);
    peer.socket = -1;
    if (role_ == Role::host && peer.slot > 0 && peer.hello) {
        // The slot plays on released from the first step it never sent.
        const std::uint64_t from = peer.last_frame + 1;
        left_from_[std::size_t(peer.slot)] = from;
        Writer leave;
        leave.u8(std::uint8_t(peer.slot));
        leave.u64(from);
        broadcast(kLeave, leave.out);
        messages_.push_back("Player " + std::to_string(peer.slot + 1) + " left the session.");
    } else if (role_ == Role::guest) {
        // Without the host every other player stops where the host was.
        for (int slot = 0; slot < 4; ++slot)
            if (slot != slot_ && joined_[std::size_t(slot)] && !left_from_[std::size_t(slot)]) left_from_[std::size_t(slot)] = peer.last_frame + 1;
        messages_.push_back("The host left; playing on alone.");
    }
}

void NetSession::store_input(int slot, std::uint64_t frame, const PadSample& sample) {
    if (slot < 0 || slot > 3) return;
    inputs_[frame][std::size_t(slot)] = sample;
}

void NetSession::poll() {
    if (role_ == Role::offline) return;
    // New guests (host lobby only).
    if (role_ == Role::host && !started_ && listener_ != -1) {
        for (;;) {
            const socket_handle s = accept(to_socket(listener_), nullptr, nullptr);
            if (s == DW4_INVALID_SOCKET) break;
            int slot = -1;
            for (int candidate = 1; candidate < 4 && slot < 0; ++candidate) {
                bool used = false;
                for (const auto& peer : peers_) used = used || (!peer->closed && peer->slot == candidate);
                if (!used) slot = candidate;
            }
            make_non_blocking(s);
            auto peer = std::make_unique<Peer>();
            peer->socket = std::intptr_t(s);
            peer->slot = slot;
            if (slot < 0) {
                Writer reject;
                reject.text("The session is full.");
                send(*peer, kReject, reject.out);
            }
            peers_.push_back(std::move(peer));
        }
    }
    for (auto& owned : peers_) {
        auto& peer = *owned;
        if (peer.closed) continue;
        // Send what is queued.
        while (!peer.out.empty()) {
            const int sent = ::send(to_socket(peer.socket), reinterpret_cast<const char*>(peer.out.data()), int(peer.out.size()), 0);
            if (sent > 0) {
                peer.out.erase(peer.out.begin(), peer.out.begin() + sent);
                continue;
            }
            if (sent < 0 && would_block()) break;
            drop_peer(peer);
            break;
        }
        if (peer.closed) continue;
        // Receive.
        char buffer[16384];
        for (;;) {
            const int got = recv(to_socket(peer.socket), buffer, int(sizeof buffer), 0);
            if (got > 0) {
                peer.in.insert(peer.in.end(), buffer, buffer + got);
                continue;
            }
            if (got < 0 && would_block()) break;
            drop_peer(peer);
            break;
        }
        // Complete messages.
        while (peer.in.size() >= 5) {
            const std::uint32_t size = std::uint32_t(peer.in[1]) | std::uint32_t(peer.in[2]) << 8 | std::uint32_t(peer.in[3]) << 16 |
                                       std::uint32_t(peer.in[4]) << 24;
            if (size > 64u * 1024u * 1024u) {
                drop_peer(peer);
                break;
            }
            if (peer.in.size() < 5 + size) break;
            const std::uint8_t type = peer.in[0];
            const std::vector<std::uint8_t> payload(peer.in.begin() + 5, peer.in.begin() + 5 + std::ptrdiff_t(size));
            peer.in.erase(peer.in.begin(), peer.in.begin() + 5 + std::ptrdiff_t(size));
            try {
                handle(peer, type, payload);
            } catch (const std::exception&) {
                drop_peer(peer);
                break;
            }
        }
    }
}

void NetSession::handle(Peer& peer, std::uint8_t type, const std::vector<std::uint8_t>& payload) {
    Reader r{payload};
    switch (type) {
    case kHello: {
        if (role_ != Role::host || started_ || peer.slot < 0) return;
        const auto protocol = r.u32();
        const auto game = r.text();
        const auto hash = r.u64();
        auto cards = r.cards();
        std::string problem;
        if (protocol != kProtocol) problem = "Another program version.";
        else if (game != kGame) problem = "Another game.";
        else if (hash != data_hash_) problem = "Different game data.";
        if (!problem.empty()) {
            Writer reject;
            reject.text(problem);
            send(peer, kReject, reject.out);
            return;
        }
        peer.hello = true;
        peer.cards = std::move(cards);
        joined_[std::size_t(peer.slot)] = true;
        Writer welcome;
        welcome.u8(std::uint8_t(peer.slot));
        send(peer, kWelcome, welcome.out);
        messages_.push_back("Player " + std::to_string(peer.slot + 1) + " joined.");
        break;
    }
    case kWelcome:
        slot_ = r.u8();
        break;
    case kStart: {
        start_.players = r.u8();
        start_.delay = r.u8();
        start_.stage = r.text();
        start_.cheats = r.u8();
        start_.exp_rate = int(r.u32());
        start_.damage_dealt = int(r.u32());
        start_.damage_taken = int(r.u32());
        start_.drop_seed = r.u32();
        start_.card_a = r.cards();
        start_.card_b = r.cards();
        const auto mask = r.u8();
        for (int slot = 0; slot < 4; ++slot) joined_[std::size_t(slot)] = (mask >> slot) & 1;
        delay_ = start_.delay;
        started_ = true;
        for (std::uint64_t frame = 1; frame <= std::uint64_t(delay_); ++frame)
            for (int slot = 0; slot < 4; ++slot)
                if (joined_[std::size_t(slot)]) store_input(slot, frame, PadSample{true, {}, {}, 0});
        break;
    }
    case kInput: {
        const int slot = r.u8();
        const auto frame = r.u64();
        const auto sample = r.pad();
        if (role_ == Role::host) {
            if (slot != peer.slot || left_from_[std::size_t(slot)]) return;
            peer.last_frame = std::max(peer.last_frame, frame);
            store_input(slot, frame, sample);
            broadcast(kInput, payload, peer.slot);
        } else {
            peer.last_frame = std::max(peer.last_frame, frame);
            store_input(slot, frame, sample);
        }
        break;
    }
    case kFingerprint: {
        const auto frame = r.u64();
        const auto value = r.u64();
        if (role_ != Role::host) return;
        if (const auto own = fingerprints_.find(frame); own != fingerprints_.end()) {
            if (own->second != value && !desync_) {
                desync_ = frame;
                Writer d;
                d.u64(frame);
                broadcast(kDesync, d.out);
                messages_.push_back("Desync detected at step " + std::to_string(frame) + ".");
            }
        } else {
            pending_fingerprints_[frame] = value;
        }
        break;
    }
    case kDesync:
        if (!desync_) {
            desync_ = r.u64();
            messages_.push_back("Desync detected at step " + std::to_string(*desync_) + ".");
        }
        break;
    case kLeave: {
        const int slot = r.u8();
        const auto from = r.u64();
        if (slot >= 0 && slot < 4) left_from_[std::size_t(slot)] = from;
        messages_.push_back("Player " + std::to_string(slot + 1) + " left the session.");
        break;
    }
    case kReject:
        messages_.push_back("Rejected: " + r.text());
        drop_peer(peer);
        break;
    default:
        break;
    }
}

bool NetSession::lobby(const SessionStart& host_start, std::string& status, std::string& error) {
    poll();
    if (started_) return true;
    if (role_ == Role::guest) {
        if (peers_.empty() || peers_.front()->closed) {
            error = messages_.empty() ? "The connection to the host closed." : messages_.back();
            return false;
        }
        status = slot_ ? "Joined as player " + std::to_string(slot_ + 1) + "; waiting for the host to start..." : "Connecting...";
        return false;
    }
    int ready = 1;
    for (const auto& peer : peers_) ready += peer->hello && !peer->closed ? 1 : 0;
    status = "Waiting for players: " + std::to_string(ready) + " of " + std::to_string(players_wanted_);
    if (ready < players_wanted_) return false;
    // Everyone is here: start.
    start_ = host_start;
    start_.players = players_wanted_;
    start_.delay = delay_;
    start_.card_b.clear();
    for (const auto& peer : peers_)
        if (peer->hello && !peer->closed && peer->slot == 1) start_.card_b = peer->cards;
    std::uint8_t mask = 0;
    for (int slot = 0; slot < 4; ++slot)
        if (joined_[std::size_t(slot)]) mask |= std::uint8_t(1 << slot);
    Writer w;
    w.u8(std::uint8_t(start_.players));
    w.u8(std::uint8_t(start_.delay));
    w.text(start_.stage);
    w.u8(start_.cheats);
    w.u32(std::uint32_t(start_.exp_rate));
    w.u32(std::uint32_t(start_.damage_dealt));
    w.u32(std::uint32_t(start_.damage_taken));
    w.u32(start_.drop_seed);
    w.cards(start_.card_a);
    w.cards(start_.card_b);
    w.u8(mask);
    broadcast(kStart, w.out);
    poll();
    started_ = true;
    close_socket(listener_);
    listener_ = -1;
    for (std::uint64_t frame = 1; frame <= std::uint64_t(delay_); ++frame)
        for (int slot = 0; slot < 4; ++slot)
            if (joined_[std::size_t(slot)]) store_input(slot, frame, PadSample{true, {}, {}, 0});
    return true;
}

void NetSession::close() {
    for (auto& peer : peers_) close_socket(peer->socket);
    peers_.clear();
    close_socket(listener_);
    listener_ = -1;
    role_ = Role::offline;
    started_ = false;
    slot_ = 0;
    joined_ = {};
    left_from_ = {};
    inputs_.clear();
    fingerprints_.clear();
    pending_fingerprints_.clear();
    desync_.reset();
}

bool NetSession::slot_joined(int slot) const { return slot >= 0 && slot < 4 && joined_[std::size_t(slot)]; }

void NetSession::submit_local(std::uint64_t frame, const PadSample& sample) {
    store_input(slot_, frame, sample);
    Writer w;
    w.u8(std::uint8_t(slot_));
    w.u64(frame);
    w.pad(sample);
    if (role_ == Role::host) broadcast(kInput, w.out);
    else if (!peers_.empty()) send(*peers_.front(), kInput, w.out);
}

bool NetSession::ready(std::uint64_t frame) const {
    const auto found = inputs_.find(frame);
    for (int slot = 0; slot < 4; ++slot) {
        if (!joined_[std::size_t(slot)]) continue;
        const auto gone = left_from_[std::size_t(slot)];
        if (gone && frame >= gone) continue;
        if (found == inputs_.end() || !found->second[std::size_t(slot)]) return false;
    }
    return true;
}

std::array<PadSample, 4> NetSession::take(std::uint64_t frame) {
    std::array<PadSample, 4> pads{};
    if (const auto found = inputs_.find(frame); found != inputs_.end())
        for (int slot = 0; slot < 4; ++slot) {
            const auto gone = left_from_[std::size_t(slot)];
            if (joined_[std::size_t(slot)] && !(gone && frame >= gone) && found->second[std::size_t(slot)])
                pads[std::size_t(slot)] = *found->second[std::size_t(slot)];
        }
    inputs_.erase(inputs_.begin(), inputs_.upper_bound(frame));
    return pads;
}

void NetSession::report_fingerprint(std::uint64_t frame, std::uint64_t value) {
    if (role_ == Role::host) {
        fingerprints_[frame] = value;
        if (const auto pending = pending_fingerprints_.find(frame); pending != pending_fingerprints_.end()) {
            if (pending->second != value && !desync_) {
                desync_ = frame;
                Writer d;
                d.u64(frame);
                broadcast(kDesync, d.out);
                messages_.push_back("Desync detected at step " + std::to_string(frame) + ".");
            }
            pending_fingerprints_.erase(pending);
        }
        while (fingerprints_.size() > 256) fingerprints_.erase(fingerprints_.begin());
    } else if (!peers_.empty()) {
        Writer w;
        w.u64(frame);
        w.u64(value);
        send(*peers_.front(), kFingerprint, w.out);
    }
}

std::string NetSession::take_message() {
    if (messages_.empty()) return {};
    auto message = messages_.front();
    messages_.pop_front();
    return message;
}

} // namespace dw4::game
