#include "automation_protocol.hpp"
#include "dolphin_runtime_internal.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
unsigned checks = 0;
void Check(bool value, const char* message)
{
  ++checks;
  if (!value)
    throw std::runtime_error(message);
}
}

int main()
try
{
  using moderngekko::detail::CheckHeadlessAudio;
  AudioCommon::AudioTelemetrySnapshot snapshot;
  Check(!CheckHeadlessAudio(snapshot, 0).ready && !CheckHeadlessAudio(snapshot, 0).failure,
        "An unavailable stream must remain pending.");
  snapshot.available = snapshot.stream_alive = true;
  snapshot.generation = 2;
  snapshot.device.backend = AudioCommon::AudioTelemetryBackend::Cubeb;
  snapshot.device.init_complete = snapshot.device.init_succeeded = true;
  snapshot.device.start_complete = snapshot.device.start_succeeded = true;
  Check(CheckHeadlessAudio(snapshot, 1).ready, "The real new initialized/started Cubeb stream was rejected.");
  Check(!CheckHeadlessAudio(snapshot, 2).ready, "A retained previous-generation success was accepted.");
  Check(!CheckHeadlessAudio(snapshot, 3).ready, "A stale earlier generation was accepted.");
  const auto changed = [&](auto change) {
    auto copy = snapshot;
    change(copy);
    return copy;
  };
  Check(!CheckHeadlessAudio(changed([](auto& s) { s.stream_alive = false; }), 1).ready,
        "A retired stream was reported as a running device.");
  Check(!CheckHeadlessAudio(changed([](auto& s) { s.device.init_complete = false; }), 1).ready,
        "Initialization was assumed before the result was published.");
  Check(!CheckHeadlessAudio(changed([](auto& s) { s.device.start_complete = false; }), 1).ready,
        "Starting the device was assumed before the result was published.");
  Check(CheckHeadlessAudio(changed([](auto& s) { s.device.backend = AudioCommon::AudioTelemetryBackend::Null; }), 1).failure,
        "Cubeb falling back to NullSound was reported as success.");
  Check(CheckHeadlessAudio(changed([](auto& s) { s.device.init_succeeded = false; }), 1).failure,
        "Actual initialization failure was ignored.");
  Check(CheckHeadlessAudio(changed([](auto& s) { s.device.start_succeeded = false; }), 1).failure,
        "Actual start failure was ignored.");
  Check(CheckHeadlessAudio(changed([](auto& s) { s.device.state_errors = 1; }), 1).failure,
        "An actual Cubeb state error was ignored.");
  Check(CheckHeadlessAudio(changed([](auto& s) { s.device.operation_errors = 1; }), 1).failure,
        "An actual device operation error was ignored.");
  Check(CheckHeadlessAudio(changed([](auto& s) { s.device.backend = AudioCommon::AudioTelemetryBackend::Unknown; }), 1).failure,
        "A completed unknown backend was presented as Cubeb.");
  Check(CheckHeadlessAudio(changed([](auto& s) { s.device.running_requested = false; }), 1).ready,
        "An ordinary pause erased a successfully initialized device.");
  auto old_failure = changed([](auto& s) { s.device.state_errors = 1; });
  Check(!CheckHeadlessAudio(old_failure, 2).failure, "Previous-session errors leaked into a new boot.");
  Check(CheckHeadlessAudio(snapshot, 1, true).failure, "Silent output was assumed without volume confirmation.");
  auto silent = snapshot;
  silent.device.volume_apply_complete = silent.device.volume_apply_succeeded = true;
  silent.device.requested_volume_percent = 0;
  Check(CheckHeadlessAudio(silent, 1, true).ready, "Successful zero-volume Cubeb output was rejected.");
  silent.device.volume_apply_succeeded = false;
  Check(CheckHeadlessAudio(silent, 1, true).failure, "Failed zero-volume application was accepted.");
  silent.device.volume_apply_succeeded = true;
  silent.device.requested_volume_percent = 1;
  Check(CheckHeadlessAudio(silent, 1, true).failure, "A nonzero device volume passed silent diagnostics.");

  moderngekko::automation::Status status;
  status.max_speed = 1.75;
  status.audio_requested_backend = "Cubeb";
  status.audio_headless_device = status.audio_muted = true;
  auto output = moderngekko::automation::FormatStatus(status);
  Check(output.contains("max_speed=1.75\n"), "Maximum speed telemetry is missing.");
  Check(output.contains("audio_available=0\n") && output.contains("audio_device_backend=Unknown\n"),
        "Unavailable telemetry fabricated an actual backend.");
  status.audio = snapshot;
  status.audio.device.backend = AudioCommon::AudioTelemetryBackend::Null;
  output = moderngekko::automation::FormatStatus(status);
  Check(output.contains("audio_requested_backend=Cubeb\n") && output.contains("audio_device_backend=Null\n"),
        "Requested and actual audio backends were conflated.");
  status.audio = snapshot;
  status.audio.device.callbacks = std::numeric_limits<std::uint64_t>::max();
  status.audio.device.callback_frames = 333;
  status.audio.device.requested_volume_percent = 0;
  status.audio.device.volume_apply_complete = status.audio.device.volume_apply_succeeded = true;
  status.audio.device.sample_rate = 48000;
  status.audio.mixer.output_sample_rate = 48000;
  status.audio.mixer.dma.push_calls = 17;
  status.audio.mixer.dma.pushed_nonzero_frames = 64;
  status.audio.mixer.dma.active_empty_events = 3;
  status.audio.mixer.dma.active_replay_events = 2;
  status.audio.mixer.dma.active_fade_events = 1;
  status.audio.mixer.dma.recently_active = true;
  status.audio.mixer.dtk.push_calls = 29;
  output = moderngekko::automation::FormatStatus(status);
  for (const char* required : {"audio_generation=2\n", "audio_stream_alive=1\n",
      "audio_headless_device=1\n", "audio_muted=1\n", "audio_device_backend=Cubeb\n",
      "audio_device_callbacks=18446744073709551615\n", "audio_device_callback_frames=333\n",
      "audio_device_requested_volume_percent=0\n", "audio_device_sample_rate=48000\n",
      "audio_device_volume_apply_complete=1\n", "audio_device_volume_apply_succeeded=1\n",
      "audio_activity_window_ns=250000000\n", "audio_mixer_output_sample_rate=48000\n",
      "audio_dma_push_calls=17\n", "audio_dma_pushed_nonzero_frames=64\n",
      "audio_dma_active_empty_events=3\n", "audio_dma_active_replay_events=2\n",
      "audio_dma_active_fade_events=1\n", "audio_dma_recently_active=1\n",
      "audio_dtk_push_calls=29\n", "audio_dtk_recently_active=0\n",
      "audio_dtk_active_empty_events=0\n"})
    Check(output.contains(required), "Actual audio telemetry was lost or crossed during serialization.");
  std::cout << "Audio runtime/status: " << checks << " checks passed; no audio device or runtime started.\n";
  return 0;
}
catch (const std::exception& error)
{
  std::cerr << error.what() << '\n';
  return 1;
}
