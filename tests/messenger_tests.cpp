// The messenger's message objects (beMessenger +0x10 and the deferred
// records' +0x28) and the bools[0] the soft reset reads without the count
// check: arguments before answers, SEARCH, the post and delivery swaps, the
// stage change, port-only sends. Dolphin checks of the same model (hooks on
// 0x803062E4 / 0x80304E10 / 0x80304F8C / 0x80347DA8, round 42): stag0001 /
// stag0101 / stag0203 / dng101b1 / stagML11 soft resets, swap for swap.
#include "dw4/engine/engine.hpp"

#include <iostream>
#include <stdexcept>

namespace {
using namespace dw4::engine;
int checks{};
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

void sends() {
    MessageObjects objects;
    check(!objects.made() && objects.first_bool() == 0, "nothing made before the first select");
    // 80305344: the first select makes the current object and the 32 records.
    objects.select();
    check(objects.made() && objects.objects() == 1 + MessageObjects::kRecords, "80303C0C makes 33 objects");
    check(objects.first_bool() == 0 && objects.state().bools == 0, "a fresh object");
    // An argument bool goes into data[0]; the next select resets the count only.
    objects.append(1);
    objects.select();
    check(objects.first_bool() == 1 && objects.state().bools == 0, "the count is reset, data[0] stays (80304618)");
    // An answer after an argument does not reach data[0].
    objects.append(0); // argument
    objects.append(1); // answer
    check(objects.first_bool() == 0 && objects.state().bools == 2, "argument first, answer second");
    // A send without bools leaves the last first entry for the unchecked read.
    objects.select();
    check(objects.first_bool() == 0, "no bool: the earlier entry is read");
    // SEARCH appends its found bool like an answer (0x80305B64 / 0x80305BE8).
    objects.select();
    objects.append(1);
    check(objects.first_bool() == 1, "SEARCH writes data[0] without arguments");
    // A stage change releases everything (803040C0).
    objects.clear();
    check(!objects.made() && objects.first_bool() == 0, "the stage change releases the objects");
}

void swaps() {
    MessageObjects objects;
    objects.select();
    const std::size_t first = objects.state().current;
    // A post (80305F9C + 803061BC): the filled object goes into the last
    // record, whose (fresh) object becomes current.
    objects.select();
    objects.append(1);
    const std::size_t posted = objects.commit();
    check(posted == first, "the record takes the filled object");
    check(objects.state().current == MessageObjects::kRecords && objects.first_bool() == 0, "the last record's object is current");
    // Its delivery (80304C58): select, swap the posted object in, never back.
    const std::size_t previous = objects.deliver(posted);
    check(previous == MessageObjects::kRecords && objects.state().current == posted, "the delivered object becomes current");
    check(objects.first_bool() == 1 && objects.state().bools == 1, "it holds its arguments");
    objects.append(0); // an answer after the argument
    objects.retire(previous);
    check(objects.first_bool() == 1, "the answer comes after the argument");
    // The next post takes the record retired last, with the previous object.
    objects.select();
    check(objects.first_bool() == 1, "still the delivered object");
    const std::size_t again = objects.commit();
    check(again == posted && objects.state().current == previous, "the retired record's object comes back");
    // 32 pending posts use up the idle records; the 33rd makes a new record.
    MessageObjects full;
    full.select();
    for (std::size_t i = 0; i < MessageObjects::kRecords; ++i) {
        full.select();
        full.commit();
    }
    full.select();
    full.commit();
    check(full.objects() == 2 + MessageObjects::kRecords && full.state().current == 1 + MessageObjects::kRecords,
          "a new record (802CF068) when none is idle");
    // The pass end resets the current object's count only (0x80304F8C).
    full.append(1);
    full.end_pass();
    check(full.state().bools == 0 && full.first_bool() == 1, "the pass end keeps data[0]");
}

void messenger() {
    // Messenger::post runs the model too (no engine needed): the poster's
    // argument bool goes into the record, the current object is the record's.
    Messenger messenger;
    Packet arguments;
    arguments.bools = {1};
    messenger.post(100, "MODELCTRL", std::nullopt, -1, "SQ_TEST", arguments);
    check(messenger.message_objects().made() && messenger.stale_bool() == 0, "after a post the current object is the record's");
    // Port-only messages (bot queries, input-script SEND lines) leave it alone.
    {
        const Messenger::PortOnly scope(messenger);
        Packet bot;
        bot.bools = {1};
        messenger.post(200, "SUBWND", std::nullopt, -1, "ISRUN", bot);
    }
    check(messenger.message_objects().state().current == MessageObjects::kRecords && messenger.stale_bool() == 0,
          "a port-only post changes no message object");
    messenger.clear();
    check(!messenger.message_objects().made() && messenger.stale_bool() == 0, "clear releases the objects");
}
} // namespace

int main() {
    try {
        sends();
        swaps();
        messenger();
        std::cout << checks << " messenger checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Messenger test failure: " << error.what() << '\n';
        return 1;
    }
}
