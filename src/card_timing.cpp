#include "dw4/card_timing.hpp"

namespace dw4 {

int CardChannel::start(std::int64_t now, std::int64_t ticks, int outcome) {
    // __CARDGetControlBlock: one call at a time.
    if (result_code(now) == card::kBusy) return card::kBusy;
    result = card::kBusy;
    end = now + ticks;
    final_result = outcome;
    return card::kReady;
}

int CardChannel::result_code(std::int64_t now) {
    if (result == card::kBusy && now >= end) result = final_result;
    return result;
}

// +0x5C. 80321430: USE_CHECK prepares the mount at once (8031FC40, mount
// state 0); the others in their first step. 80320F60: FILEFIND without a
// slot tries slot A, then B.
CardRequest::CardRequest(Kind kind, int slot) : kind_(kind), slot_(slot) {
    if (kind == kFileFind && slot < 0) {
        slot_ = 0;
        any_slot_ = true;
    }
}

// 8031FCF0: one step of the mount per frame. 0 running, 1 mounted and
// checked; -1 I/O error, -2 broken or unformatted, -3 no card, -4 wrong
// device.
int CardRequest::mount(CardChannel& channel, const CardSlot& card, std::int64_t now) {
    switch (mount_state_) {
    case 0:
        mount_retries_ = 0;
        mount_state_ = 1;
        return 0;
    case 1: {
        // CARDProbeEx: busy waits, three failures give up.
        const int r = channel.probe(card);
        if (r == card::kBusy) return 0;
        if (r == card::kReady) {
            mount_state_ = 2;
            return 0;
        }
        if (++mount_retries_ >= 3) return r == card::kWrongDevice ? -4 : -3;
        return 0;
    }
    case 2: {
        // CARDMountAsync reads the five system blocks; a card without a
        // valid file system mounts as broken.
        const int r = channel.start(now, card::kMountTicks, card.formatted ? card::kReady : card::kBroken);
        if (r < card::kBusy) return -1;
        channel.attached = true;
        mount_flag_ = true;
        mount_state_ = 3;
        return 0;
    }
    case 3: {
        const int r = channel.result_code(now);
        if (r == card::kBusy) return 0;
        if (r == card::kReady || r == card::kBroken || r == card::kEncoding) {
            mount_state_ = 4;
            return 0;
        }
        if (r == card::kIoError || r == card::kFatal) return -1;
        if (r == card::kWrongDevice) return -4;
        return -3;
    }
    case 4: {
        // CARDCheck (synchronous): a sound card needs no repair, so no I/O.
        const int r = channel.result_code(now) == card::kBusy ? card::kBusy : card.formatted ? card::kReady : card::kBroken;
        if (r == card::kBusy) return 0;
        if (r == card::kWrongDevice) return -4;
        mount_state_ = 5;
        if (r == card::kReady) return 1;
        if (r == card::kIoError || r == card::kFatal) return -1;
        if (r == card::kBroken || r == card::kEncoding) return -2;
        return -3;
    }
    default:
        return 0;
    }
}

// 8032029C (after 80320004 built the header): write the 8 KiB header, then
// CARDGetStatus and the synchronous CARDSetStatus with the banner and icon
// formats. 0 running, 1 done, else an error (-1 I/O, -3 no card, -10).
int CardRequest::header(CardChannel& channel, CardSlot& card, std::int64_t now) {
    switch (header_state_) {
    case 0:
        channel.start(now, card::write_ticks(kHeaderBytes), card::kReady);
        header_state_ = 1;
        return 0;
    case 1: {
        const int r = channel.result_code(now);
        if (r == card::kBusy) return 0;
        if (r == card::kReady) {
            header_state_ = 2;
            return 0;
        }
        if (r == card::kNoCard) return -3;
        if (r == card::kIoError) return -1;
        return -10;
    }
    case 2:
        header_state_ = 3; // CARDGetStatus reads the directory in memory
        return 0;
    case 3:
        // CARDSetStatus waits for its directory update inside this step.
        stalled_ += card::kStatusTicks;
        card.image_offset = kImageOffset;
        header_state_ = 4;
        return 1;
    default:
        return 1;
    }
}

// 8031FF98: unmount when this request mounted the card.
void CardRequest::finish(CardChannel& channel) {
    if (!mount_flag_) return;
    channel.attached = false;
    mount_flag_ = false;
}

// Step 0 of the file requests: prepare the mount (+36 0), then mount;
// errors end the request with the beSv code (still mounted).
bool CardRequest::mounted(CardChannel& channel, const CardSlot& card, std::int64_t now, bool format) {
    if (sub_ == 0) {
        mount_state_ = 0; // 8031FC40
        mount_flag_ = false;
        sub_ = 1;
        return false;
    }
    const int r = mount(channel, card, now);
    if (r == 0) return false;
    if (r == 1 || (format && r == -2)) return true; // FORMAT goes on with a broken card
    result_ = r == -1 ? kMountError : r == -3 ? kNoCard : r == -2 ? kBroken : r == -4 ? kWrongDevice : kError;
    done_ = true;
    return false;
}

bool CardRequest::update(CardChannel& channel, CardSlot& card, std::int64_t now) {
    if (done_) return true;
    // Both data copies (CARDReadAsync / CARDWriteAsync of +16 bytes at the
    // offsets +24 / +28): sub `first` starts a copy, the next one waits for
    // it. 0 running, 1 both moved, -1 failed (result and step set).
    auto move_copies = [&](bool read, int first) {
        if (sub_ == first) {
            channel.start(now, read ? card::read_ticks(kCopyBytes) : card::write_ticks(kCopyBytes), card::kReady);
            sub_ = first + 1;
            return 0;
        }
        const int r = channel.result_code(now);
        if (r == card::kBusy) return 0;
        if (r == card::kReady) {
            if (copy_) return 1;
            copy_ = 1;
            sub_ = first;
            return 0;
        }
        fail(r == card::kNoCard ? kNoCard : r == card::kIoError && !read ? kMountError : kError, 19);
        return -1;
    };
    // Step 3 of WRITE_MEDIA / FILEMAKE: 80320004 builds the header, then
    // 8032029C writes it and sets the status.
    auto header_step = [&] {
        if (sub_ == 0) {
            header_state_ = 0;
            sub_ = 1;
            return;
        }
        const int r = header(channel, card, now);
        if (r == 1) fail(kDone, 19);
        else if (r != 0) fail(r == -1 ? kMountError : r == -3 ? kNoCard : r == -2 ? kBroken : kError, 19);
    };
    switch (kind_) {
    case kUseCheck: { // 80321488
        const int r = mount(channel, card, now);
        if (r != 0) {
            result_ = r == 1 ? kDone : r == -1 ? kMountError : r == -3 ? kNoCard : r == -2 ? kBroken : kWrongDevice;
            done_ = true;
            finish(channel);
        }
        break;
    }
    case kFileFind: // 80320FBC
        if (step_ == 0) {
            if (mounted(channel, card, now)) next(1);
        } else if (step_ == 1) {
            // CARDOpen (+ CARDClose); a missing file ends here, still mounted.
            if (card.file) {
                fail(kDone, 20);
            } else {
                result_ = kMissing;
                done_ = true;
            }
        } else if (step_ == 20) {
            done_ = true;
            finish(channel);
        }
        if (any_slot_ && done_ && result_ != kDone && !tried_b_) {
            // Not on slot A: the same on slot B.
            done_ = false;
            result_ = kRunning;
            next(0);
            slot_ = 1;
            tried_b_ = true;
        }
        break;
    case kGetFreeSize: // 8032224C
        if (step_ == 0) {
            if (mounted(channel, card, now)) next(1);
        } else if (step_ == 1) {
            free_bytes_ = card.free_blocks * card::kSectorSize; // CARDFreeBlocks
            free_files_ = card.free_files;
            fail(kDone, 20);
        } else if (step_ == 20) {
            done_ = true;
            finish(channel);
        }
        break;
    case kReadMedia: // 80321670
        if (step_ == 0) {
            if (mounted(channel, card, now)) next(1);
        } else if (step_ == 1) {
            if (card.file) {
                next(2);
            } else {
                result_ = kMissing; // CARDOpen failed: done, still mounted
                done_ = true;
            }
        } else if (step_ == 2) {
            // CARDGetStatus; a file whose banner is not where the game puts it
            // gets the game's header and status first.
            if (sub_ == 0) {
                sub_ = 1;
            } else if (sub_ == 1) {
                if (card.image_offset == kImageOffset) next(3);
                else sub_ = 2;
            } else if (sub_ == 2) {
                header_state_ = 0;
                sub_ = 3;
            } else {
                const int r = header(channel, card, now);
                if (r == 1) next(3);
                else if (r != 0) fail(r == -1 ? kMountError : r == -3 ? kNoCard : r == -2 ? kBroken : kError, 19);
            }
        } else if (step_ == 3) {
            if (sub_ == 0) {
                copy_ = 0;
                sub_ = 1;
            } else if (move_copies(true, 1) == 1) {
                result_ = kDone;
                next(4);
            }
        } else if (step_ == 4) {
            if (!card.data_valid) result_ = kBadData; // neither copy passes its check
            next(19);
        } else if (step_ == 19) {
            next(20); // CARDClose
        } else if (step_ == 20) {
            done_ = true;
            finish(channel);
        }
        break;
    case kWriteMedia: // 80321DA0
        if (step_ == 0) {
            if (mounted(channel, card, now)) next(1);
        } else if (step_ == 1) {
            if (card.file) {
                next(2);
                copy_ = 0;
            } else {
                fail(kMissing, 20);
            }
        } else if (step_ == 2) {
            if (move_copies(false, 0) == 1) next(3);
        } else if (step_ == 3) {
            header_step();
        } else if (step_ == 19) {
            next(20); // CARDClose
        } else if (step_ == 20) {
            done_ = true;
            finish(channel);
        }
        break;
    case kFileMake: // 80320968
        if (step_ == 0) {
            if (mounted(channel, card, now)) next(1);
        } else if (step_ == 1) {
            if (sub_ == 0) {
                // CARDCreateAsync: an existing name, a full directory or too
                // few blocks fail at once.
                if (card.file) channel.result = card::kExist;
                else if (card.free_files == 0) channel.result = card::kNoEntry;
                else if (card.free_blocks * card::kSectorSize < kFileBytes) channel.result = card::kInsSpace;
                else if (channel.start(now, card::kCreateTicks, card::kReady) == card::kReady) {
                    card.file = true;
                    card.free_blocks -= kFileBytes / card::kSectorSize;
                    card.free_files -= 1;
                }
                sub_ = 1;
            } else if (sub_ == 1) {
                const int r = channel.result_code(now);
                if (r == card::kExist) {
                    sub_ = 2;
                } else if (r == card::kReady) {
                    next(2);
                    copy_ = 0;
                } else if (r != card::kBusy) {
                    fail(r == card::kIoError ? kMountError : r == card::kNoCard ? kNoCard : kError, 20);
                }
            } else if (sub_ == 2) {
                // CARDDeleteAsync, then create again.
                if (channel.start(now, card::kDeleteTicks, card::kReady) == card::kReady) {
                    card.file = false;
                    card.free_blocks += kFileBytes / card::kSectorSize;
                    card.free_files += 1;
                }
                sub_ = 3;
            } else {
                const int r = channel.result_code(now);
                if (r == card::kReady || r == card::kNoFile) sub_ = 0;
                else if (r == card::kNoCard) fail(kNoCard, 20);
                else if (r != card::kBusy) fail(kError, 20);
            }
        } else if (step_ == 2) {
            if (move_copies(false, 0) == 1) {
                result_ = kDone;
                next(3);
            }
        } else if (step_ == 3) {
            header_step();
        } else if (step_ == 19) {
            next(20); // CARDClose
        } else if (step_ == 20) {
            done_ = true;
            finish(channel);
        }
        break;
    case kFormat: // 80321230
        if (step_ == 0) {
            if (mounted(channel, card, now, true)) next(1);
        } else if (sub_ == 0) {
            channel.start(now, card::kFormatTicks, card::kReady); // CARDFormatAsync
            sub_ = 1;
        } else {
            const int r = channel.result_code(now);
            if (r != card::kBusy) {
                result_ = r == card::kReady ? kDone : r == card::kNoCard ? kNoCard : kMountError;
                done_ = true;
                if (r == card::kReady) {
                    card.formatted = true;
                    card.file = false;
                }
            }
        }
        if (done_) finish(channel);
        break;
    }
    return done_;
}

} // namespace dw4
