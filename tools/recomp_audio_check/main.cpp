// Copyright 2026 DW4_PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "AudioCommon/AudioTelemetry.h"

#include <array>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
std::uint64_t checks = 0;
void Check(bool condition, const char* message)
{
  ++checks;
  if (!condition)
    throw std::runtime_error(message);
}
}

int main()
{
  using namespace AudioCommon;
  try
  {
    const std::array<short, 8> stereo{0, 0, -4, 4, 0, 2, 0, 0};
    const auto before = stereo;
    Check(CountNonzeroAudioFrames(stereo.data(), 4, 2) == 2,
          "Count frames with signal in either channel; opposite signs must not cancel.");
    Check(stereo == before, "Signal observation must not mutate mixed output.");
    const std::array<float, 12> surround{0, 0, 0, 0, 0, 0.5f};
    Check(CountNonzeroAudioFrames(surround.data(), 2, 6) == 1,
          "Signal in a surround-only channel must be observed.");
    Check(CountNonzeroAudioFrames(static_cast<const short*>(nullptr), 0, 2) == 0,
          "An empty callback has no signal.");

    MixerSourceTelemetry dtk;
    constexpr std::uint64_t now = 1'000'000'000;
    Check(!dtk.IsActive(now, true), "Never-fed DTK is inactive.");
    for (int i = 0; i < 100; ++i)
    {
      dtk.RecordPush(168, 0, true, 108000000, 2250, now);
      dtk.RecordEmpty(dtk.IsActive(now, true), true, true);
    }
    auto source = dtk.Snapshot(now);
    Check(source.pushed_frames == 16800 && source.enqueued_granules == 0,
          "Producer activity is separate from actual granule enqueue activity.");
    Check(source.active_empty_events == 0 && source.active_replay_events == 0 &&
              source.active_fade_events == 0,
          "Repeated silent DTK pushes must not manufacture stutter events.");
    dtk.RecordPush(1, 1, false, 108000000, 2250, now);
    Check(!dtk.IsActive(now, true), "Null output must not activate an unqueued source.");
    dtk.RecordPush(128, 127, true, 108000000, 2250, now);
    Check(dtk.IsActive(now, true), "Accepted nonzero producer signal activates observation.");
    Check(!dtk.IsActive(now, false), "Paused execution is excluded.");
    Check(!dtk.IsActive(now - 1, true), "An earlier observation cannot activate future signal.");
    Check(dtk.IsActive(now + AUDIO_ACTIVITY_WINDOW_NS, true), "Activity includes its boundary.");
    Check(!dtk.IsActive(now + AUDIO_ACTIVITY_WINDOW_NS + 1, true), "Stopped signal expires.");
    dtk.RecordEmpty(true, true, false);
    dtk.RecordEmpty(true, true, true);
    dtk.RecordEmpty(true, false, false);
    dtk.RecordEmpty(false, true, true);
    source = dtk.Snapshot(now + 5);
    Check(source.active_empty_events == 3 && source.active_replay_events == 2 &&
              source.active_fade_events == 1,
          "Empty, replay and fade are distinct branch observations.");
    Check(source.pushed_nonzero_frames == 128 && source.accepted_frames == 16928 &&
              source.last_push_age_ns == 5 && source.sample_rate_divisor == 2250,
          "Source metadata and accepted/produced frames retain their distinct meanings.");

    DeviceTelemetry device;
    device.sample_rate.store(48000);
    Check(device.CanStart(false), "Ordinary playback does not require diagnostic mute.");
    Check(!device.CanStart(true), "A silent diagnostic cannot start before volume application.");
    device.requested_volume_percent.store(0);
    Check(!device.CanStart(true), "Requesting zero volume alone must not permit playback.");
    device.volume_apply_complete.store(true);
    Check(!device.CanStart(true), "A completed but failed mute must prevent playback.");
    device.volume_apply_succeeded.store(true);
    Check(device.CanStart(true), "Successful zero volume permits silent device processing.");
    device.requested_volume_percent.store(1);
    Check(!device.CanStart(true), "Successful nonzero volume is not a silent diagnostic.");
    device.requested_volume_percent.store(0);
    device.volume_apply_complete.store(false);
    Check(!device.CanStart(true), "A new pending volume operation invalidates older success.");

    device.RecordCallback(1, 1'000'001, 240, 120, 0);
    device.RecordCallback(5'000'001, 6'000'001, 240, 0, 0);
    device.RecordCallback(15'000'002, 21'000'002, 240, 240, 0);
    auto callback = device.Snapshot();
    Check(callback.callbacks == 3 && callback.callback_frames == 720 &&
              callback.nonzero_output_frames == 360,
          "Callbacks count requested frames and pre-device-gain signal separately.");
    Check(callback.callback_interval_count == 2 && callback.callback_interval_sum_ns == 15'000'001 &&
              callback.callback_interval_max_ns == 10'000'001,
          "Callback intervals use starts and omit the first callback.");
    Check(callback.callback_gaps_over_two_periods == 1 && callback.callback_work_over_period == 1 &&
              callback.callback_work_sum_ns == 8'000'000 && callback.callback_work_max_ns == 6'000'000,
          "Scheduler gaps and callback work exceeding frame duration are separate observations.");
    device.RecordCallback(5'000'000'000, 5'000'000'010, 240, 0, 1);
    Check(device.Snapshot().callback_interval_count == 2,
          "Restarting the stream must not count paused time as a callback gap.");
    device.RecordCallback(5'000'000'001, 5'000'000'000, 0, 0, 1);
    Check(device.Snapshot().callback_work_over_period == 1,
          "Empty callbacks and reversed synthetic timestamps must not underflow duration.");
    SetRequireSilentAudioStart(true);
    Check(IsSilentAudioStartRequired(), "The explicit diagnostic opt-in is observable.");
    SetRequireSilentAudioStart(false);
    Check(!IsSilentAudioStartRequired(), "The diagnostic opt-in can be restored after a run.");

    auto owner = std::make_shared<AudioTelemetryCounters>();
    RegisterAudioTelemetry(owner);
    const auto generation = GetAudioTelemetrySnapshot().generation;
    Check(generation != 0 && GetAudioTelemetrySnapshot().stream_alive,
          "Registration publishes a new live generation.");
    std::atomic<bool> done{false};
    std::thread producer([&] {
      for (int i = 0; i < 20000; ++i)
        owner->dma.RecordPush(128, 64, true, 108000000, 3375, now);
      done.store(true, std::memory_order_release);
    });
    std::uint64_t last_frames = 0;
    bool monotonic = true;
    while (!done.load(std::memory_order_acquire))
    {
      const auto snapshot = GetAudioTelemetrySnapshot();
      monotonic &= snapshot.generation == generation && snapshot.mixer.dma.pushed_frames >= last_frames;
      last_frames = snapshot.mixer.dma.pushed_frames;
    }
    producer.join();
    Check(monotonic && GetAudioTelemetrySnapshot().mixer.dma.pushed_frames == 2'560'000,
          "Concurrent diagnostic reads retain per-counter monotonicity without a stream pointer.");
    owner->stream_alive.store(false);
    owner.reset();
    Check(!GetAudioTelemetrySnapshot().stream_alive &&
              GetAudioTelemetrySnapshot().mixer.dma.push_calls == 20000,
          "Retirement retains final counters independently of stream ownership.");
    auto replacement = std::make_shared<AudioTelemetryCounters>();
    RegisterAudioTelemetry(replacement);
    const auto replaced = GetAudioTelemetrySnapshot();
    Check(replaced.generation > generation && replaced.mixer.dma.push_calls == 0 &&
              !replaced.device.init_complete && !replaced.device.start_complete,
          "A replacement backend cannot inherit stale startup success or counters.");
    std::cout << "{\"passed\":true,\"checks\":" << checks
              << ",\"device_started\":false,\"runtime_launched\":false}\n";
    return 0;
  }
  catch (const std::exception& error)
  {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
