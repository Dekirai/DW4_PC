#include <array>
#include <atomic>
#include <cstdio>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include "VideoCommon/ComposedFrameCapture.h"

namespace
{
unsigned checks = 0;
void Check(bool condition, const char* message)
{
  ++checks;
  if (!condition)
    throw std::string(message);
}
}

int main()
{
  using namespace VideoCommon;
  try
  {
    ComposedFrameCapture channel;
    Check(channel.Snapshot().state == ComposedCaptureState::Idle, "initial state");
    Check(!channel.TakeRequest(), "no unsolicited capture");
    Check(channel.Request("") == 0, "empty path rejected");
    Check(channel.Request(std::string("bad\0path", 8)) == 0, "embedded null rejected");
    const auto unsupported = channel.Request("unavailable.png");
    Check(unsupported != 0, "unavailable request has an acknowledged id");
    Check(channel.Snapshot().state == ComposedCaptureState::Failed, "unsupported is not success");
    Check(!channel.Snapshot().error.empty(), "unsupported explains failure");
    channel.SetSupport(true);
    const auto first = channel.Request("composed.png");
    Check(first > unsupported, "monotonic request ids");
    Check(channel.Snapshot().state == ComposedCaptureState::Pending, "request waits for present");
    Check(channel.Request("other.png") == 0, "pending request cannot be overwritten");
    const auto taken = channel.TakeRequest();
    Check(taken && taken->id == first && taken->path == "composed.png", "capture ownership");
    Check(!channel.TakeRequest(), "no duplicate renderer claim");
    Check(channel.Request("other.png") == 0, "in-flight request cannot be overwritten");
    channel.Complete(unsupported, 1, 1);
    Check(channel.Snapshot().state == ComposedCaptureState::Capturing, "stale completion ignored");
    channel.Complete(first, 1920, 1080);
    const auto success = channel.Snapshot();
    Check(success.state == ComposedCaptureState::Succeeded, "successful PNG publication");
    Check(success.width == 1920 && success.height == 1080 && success.error.empty(), "result metadata");
    channel.Complete(first, 1, 1, "late failure");
    Check(channel.Snapshot().state == ComposedCaptureState::Succeeded, "duplicate completion ignored");
    const auto failed = channel.Request("failure.png");
    Check(channel.TakeRequest().has_value(), "next renderer claim");
    channel.Complete(failed, 640, 480, "write failed");
    Check(channel.Snapshot().state == ComposedCaptureState::Failed &&
              channel.Snapshot().error == "write failed", "write failure remains explicit");
    Check(channel.Request("resize.png") != 0, "request before resize");
    channel.SetSupport(false, "swapchain recreated");
    Check(channel.Snapshot().state == ComposedCaptureState::Failed && !channel.TakeRequest(),
          "resize cancels pending capture");
    channel.SetSupport(true);
    const auto retired_id = channel.Request("shutdown.png");
    Check(channel.TakeRequest().has_value(), "take before shutdown");
    channel.Retire();
    channel.Complete(retired_id, 1, 1);
    channel.SetSupport(true);
    Check(channel.Snapshot().retired && !channel.Snapshot().supported &&
              channel.Snapshot().state == ComposedCaptureState::Failed, "retirement cannot resurrect");
    Check(channel.Request("late.png") == 0 && !channel.TakeRequest(), "retirement blocks new work");

    for (unsigned round = 0; round < 32; ++round)
    {
      ComposedFrameCapture raced;
      raced.SetSupport(true);
      std::atomic<bool> start{false};
      std::atomic<unsigned> accepted{0};
      std::array<std::thread, 8> workers;
      for (auto& worker : workers)
      {
        worker = std::thread([&] {
          while (!start.load(std::memory_order_acquire))
            std::this_thread::yield();
          if (raced.Request("only-one.png") != 0)
            accepted.fetch_add(1, std::memory_order_relaxed);
        });
      }
      start.store(true, std::memory_order_release);
      for (auto& worker : workers)
        worker.join();
      Check(accepted.load() == 1, "concurrent requests have exactly one owner");
      const auto owned = raced.TakeRequest();
      Check(owned && owned->id == 1, "one renderer request after concurrent publication");
      std::thread retire([&] { raced.Retire(); });
      std::thread finish([&] { raced.Complete(1, 4, 4); });
      retire.join();
      finish.join();
      const auto final = raced.Snapshot();
      Check(final.retired && !final.supported && final.state != ComposedCaptureState::Capturing &&
                final.state != ComposedCaptureState::Pending, "retirement always resolves outstanding work");
    }

    const std::array<u8, 24> bgra = {3, 2, 1, 255, 6, 5, 4, 128, 99, 99, 99, 99,
                                     9, 8, 7, 64, 12, 11, 10, 0, 99, 99, 99, 99};
    const std::vector<u8> expected = {1, 2, 3, 255, 4, 5, 6, 128,
                                     7, 8, 9, 64, 10, 11, 12, 0};
    std::vector<u8> pixels;
    Check(ConvertComposedCaptureToRGBA(bgra, 12, 2, 2, true, &pixels), "BGRA conversion succeeds");
    Check(pixels == expected, "BGRA swizzle preserves rows, alpha, and removes padding");
    Check(ConvertComposedCaptureToRGBA(expected, 8, 2, 2, false, &pixels) && pixels == expected,
          "RGBA is retained without swizzle");
    Check(ConvertComposedCaptureToRGBA(std::span(bgra).first(20), 12, 2, 2, true, &pixels),
          "last-row padding is not read");
    const auto prior = pixels;
    Check(!ConvertComposedCaptureToRGBA(std::span(bgra).first(19), 12, 2, 2, true, &pixels),
          "truncated mapped bytes rejected");
    Check(!ConvertComposedCaptureToRGBA(bgra, 7, 2, 2, true, &pixels), "short stride rejected");
    Check(!ConvertComposedCaptureToRGBA(bgra, 0, 2, 2, true, &pixels), "zero stride rejected");
    Check(!ConvertComposedCaptureToRGBA(bgra, 12, 0, 2, true, &pixels), "zero width rejected");
    Check(!ConvertComposedCaptureToRGBA(bgra, 12, 2, 0, true, &pixels), "zero height rejected");
    Check(!ConvertComposedCaptureToRGBA(bgra, std::numeric_limits<size_t>::max(), 2, 2, true, &pixels),
          "mapped extent overflow rejected");
    Check(!ConvertComposedCaptureToRGBA(bgra, 12, std::numeric_limits<u32>::max(),
                                       std::numeric_limits<u32>::max(), true, &pixels),
          "malformed dimensions rejected");
    Check(!ConvertComposedCaptureToRGBA(bgra, 65536, 16384, 16384, true, &pixels),
          "diagnostic allocation cap enforced");
    Check(!ConvertComposedCaptureToRGBA(bgra, 12, 2, 2, true, nullptr), "null output rejected");
    Check(pixels == prior, "rejected conversions preserve output");
    std::printf("{\"passed\":true,\"checks\":%u,\"runtime_launched\":false}\n", checks);
    return 0;
  }
  catch (const std::string& message)
  {
    std::fprintf(stderr, "Check %u failed: %s\n", checks, message.c_str());
    return 1;
  }
}
