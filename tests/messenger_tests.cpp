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

// A module answering like the original's command handlers: ANSWER puts one
// bool (its value) into the reply, QUIET none.
class Answering final : public Manager {
public:
    Answering() : Manager("TestAnswering") {}
    bool accepts(const ObjectView&) const override { return false; }
    void add_info(Engine&, const ObjectView&) override {}
    bool command(Engine&, Ram&, std::string_view command, Packet& packet) override {
        ++handled;
        if (command == "ANSWER") packet.bools.push_back(value);
        return command == "ANSWER" || command == "QUIET";
    }
    std::uint8_t value = 1;
    int handled = 0;
};

void through_engine() {
    // The whole path on an engine without game data: Messenger::send (select,
    // argument bools, dispatch, answers), SEARCH, post + delivery pass, the
    // stage change and the port-only scope.
    Engine engine{dw4::MountedResources{}};
    Messenger& m = engine.messenger();
    Answering module;
    Ram ram;
    ram.manager = &module;
    m.add_row("TEST", std::string("a"), 0, &ram);
    auto send = [&](std::string_view command, std::vector<std::uint8_t> arguments) {
        Packet packet;
        packet.bools = std::move(arguments);
        m.send(engine, "TEST", std::nullopt, -1, command, packet);
        return packet;
    };
    // Argument before answer: data[0] is the argument.
    module.value = 1;
    const Packet first = send("ANSWER", {0});
    check(first.bools.size() == 2 && first.bools[1] == 1 && m.stale_bool() == 0, "an argument bool takes data[0], the answer data[1]");
    // No argument: the answer is data[0].
    send("ANSWER", {});
    check(m.stale_bool() == 1, "without arguments the answer takes data[0]");
    // A send without any bool leaves data[0] to the unchecked read.
    module.value = 0;
    send("QUIET", {});
    check(m.stale_bool() == 1 && m.message_objects().state().bools == 0, "no bool: the last entry stays, count 0");
    // SEARCH answers its found bool (0x80305AE0..0x80305C40) and the count.
    Packet search;
    m.send(engine, "NONE", std::nullopt, -1, "SEARCH", search);
    check(search.bools == std::vector<std::uint8_t>{0} && search.ints == std::vector<std::int32_t>{0} && m.stale_bool() == 0,
          "SEARCH without a row: bool 0 in data[0]");
    Packet found;
    m.send(engine, "TEST", std::nullopt, -1, "SEARCH", found);
    check(found.bools == std::vector<std::uint8_t>{1} && found.ints == std::vector<std::int32_t>{1} && m.stale_bool() == 1 &&
              module.handled == 3,
          "SEARCH with a row: bool 1, no dispatch to the module");
    // A post swaps the filled object into its record: the current object
    // is the record's (fresh), until the delivery swaps the posted one in.
    Packet posted;
    posted.bools = {1};
    m.post(engine.time(), "TEST", std::nullopt, -1, "QUIET", posted);
    check(m.stale_bool() == 0, "after the post the current object is the record's");
    send("QUIET", {});
    check(m.stale_bool() == 0, "a later send selects the record's object");
    m.update(engine);
    check(module.handled == 5 && m.stale_bool() == 1, "the delivered object stays current (its argument in data[0])");
    send("QUIET", {});
    check(m.stale_bool() == 1, "and stays for the following sends");
    // Port-only sends and posts leave the objects alone.
    {
        const Messenger::PortOnly scope(m);
        module.value = 0;
        send("ANSWER", {0});
        Packet bot;
        bot.bools = {0};
        m.post(engine.time(), "TEST", std::nullopt, -1, "ANSWER", bot);
    }
    check(m.stale_bool() == 1, "port-only messages change no message object");
    const auto before = m.message_objects().state();
    m.update(engine); // delivers the port-only post, dispatch without the model
    check(module.handled == 8 && m.stale_bool() == 1 && m.message_objects().state().current == before.current,
          "a port-only delivery swaps nothing");
    // The stage change releases the objects (803040C0): data[0] reads 0
    // until the next stage's first bool.
    m.clear();
    check(m.stale_bool() == 0 && !m.message_objects().made(), "the stage change releases the objects");
}
} // namespace

int main() {
    try {
        sends();
        swaps();
        messenger();
        through_engine();
        std::cout << checks << " messenger checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Messenger test failure: " << error.what() << '\n';
        return 1;
    }
}
