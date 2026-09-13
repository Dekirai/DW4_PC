// Copyright 2026 DW4_PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "runtime/ingame_menu_input.hpp"

#include <cstdio>
#include <cstdlib>
#include <thread>

namespace {
unsigned checks = 0;
void Check(bool pass, const char* reason) {
  ++checks;
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", reason);
    std::exit(1);
  }
}
FrontendUI::InputEvent KeyEvent(FrontendUI::Key key, bool down = true, bool repeat = false) {
  FrontendUI::InputEvent event;
  event.kind = FrontendUI::InputKind::Key;
  event.key = key;
  event.down = down;
  event.repeat = repeat;
  return event;
}
} // namespace

int main() {
  using namespace FrontendUI;
  using moderngekko::detail::InGameMenuInput;
  InGameMenuInput input;
  Check(!input.IsOpen(), "initially closed");
  Check(!input.Handle(KeyEvent(Key::A)), "closed menu passes guest input");
  Check(input.Handle(KeyEvent(Key::F1)), "F1 first down consumed");
  Check(input.IsOpen(), "F1 opens immediately before rendering");
  Check(input.Handle(KeyEvent(Key::F1, true, true)) && input.IsOpen(), "F1 auto-repeat cannot close");
  Check(input.Handle(KeyEvent(Key::F1, false)) && input.IsOpen(), "F1 release cannot toggle");
  Check(input.Drain().reset, "opening clears held input");
  Check(input.Handle(KeyEvent(Key::Space)), "open menu captures fast-forward key");
  auto batch = input.Drain();
  Check(batch.size == 1 && batch.events[0].key == Key::Space, "menu receives captured navigation input");

  InputEvent mouse;
  mouse.kind = InputKind::MousePosition;
  for (unsigned i = 0; i < 2048; ++i) {
    mouse.x = static_cast<float>(i);
    (void)input.Handle(mouse);
  }
  batch = input.Drain();
  Check(batch.size == 1 && batch.events[0].x == 2047, "successive mouse moves coalesce to latest position");
  Check(input.Overflows() == 0, "coalescing avoids artificial overflow");

  (void)input.Handle(mouse);
  InputEvent click;
  click.kind = InputKind::MouseButton;
  click.down = true;
  (void)input.Handle(click);
  mouse.x = 100;
  (void)input.Handle(mouse);
  batch = input.Drain();
  Check(batch.size == 3 && batch.events[1].kind == InputKind::MouseButton,
        "mouse coalescing cannot reorder motion around a click");

  for (std::size_t i = 0; i <= InGameMenuInput::Capacity; ++i)
    (void)input.Handle(KeyEvent(Key::LeftShift, (i % 2) == 0));
  batch = input.Drain();
  Check(batch.reset && batch.size == 1 && input.Overflows() == 1,
        "overflow drops stale transitions and requests key/button release before remaining event");
  Check(!input.Drain().reset, "reset notification consumed exactly once");

  (void)input.Handle(KeyEvent(Key::A));
  InputEvent focus;
  focus.kind = InputKind::Focus;
  focus.down = false;
  Check(input.Handle(focus), "open menu retains modal capture on focus loss");
  batch = input.Drain();
  Check(batch.reset && batch.size == 1 && batch.events[0].kind == InputKind::Focus,
        "focus loss cancels stale held input and retains focus notification");
  Check(input.IsOpen(), "focus loss does not silently change open state or resume guest");
  Check(input.Handle(KeyEvent(Key::Escape)), "Escape closes and consumes before native exit");
  Check(!input.IsOpen() && input.Drain().reset, "closing immediately releases capture and input");
  Check(!input.Handle(KeyEvent(Key::Escape, false)), "release after menu closes remains non-toggle");

  Check(input.Handle(KeyEvent(Key::F1)), "reopen");
  input.Close();
  Check(!input.IsOpen() && input.Drain().reset, "video close button has the same reset policy");
  Check(input.Handle(KeyEvent(Key::F1)), "reopen before concurrency exercise");
  std::thread producer([&] {
    for (unsigned i = 0; i < 20000; ++i)
      (void)input.Handle(KeyEvent(Key::A, (i % 2) == 0));
  });
  for (unsigned i = 0; i < 2000; ++i)
    Check(input.Drain().size <= InGameMenuInput::Capacity, "concurrent drain remains bounded");
  input.Retire();
  producer.join();
  Check(!input.IsAlive() && !input.IsOpen(), "retired session cannot retain guest capture");
  const auto previous_events = input.Events();
  Check(!input.Handle(KeyEvent(Key::F1)), "retired session rejects queued reopen");
  Check(!input.Handle(click) && input.Events() == previous_events, "retired session rejects stale pointer input");
  std::printf("PASS: %u menu input policy checks; no core, window or audio device initialized.\n", checks);
}
