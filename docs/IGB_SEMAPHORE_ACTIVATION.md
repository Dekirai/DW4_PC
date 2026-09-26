# Native semaphore activation

`IgbSemaphoreRuntimeInitInvocation` implements reached activation `80077434`,
including original `OSInitSemaphore` (`80260DA8`) and its queue initializer
(`8026110C`). It uses the constructed native semaphore owner documented in
[IGB_STRUCT_MEMBERS.md](IGB_STRUCT_MEMBERS.md).

## Ordered effects

The operation captures `_initCount` at receiver `+0C` before disabling
interrupts. It then preserves the original sequence:

1. Disable the represented execution-exclusion bit and capture its prior value.
2. Write queue tail `+20` to zero, then queue head `+1C` to zero.
3. Write the captured full 32-bit count to `+18`.
4. Restore the captured prior exclusion bit.
5. Write active byte `+14` to one.
6. Read canonical success status `805622D0` freshly and write its four bytes
   to the captured hidden result destination.

Maximum count `+10` and padding `+15..+17` are unchanged. A later count mutation
does not replace the already captured argument. A late success status is read
after activation; the status cell aliases the existing memory-construction
cell rather than copying or assuming a value.

## Explicit platform state and continuations

`IgbExecutionInterruptState` represents only the exclusion bit needed by these
callers. It is not a host CPU register. Its optional initial value is not
inferred. The disable/restore adapter follows the original prior-bit return
and nonzero restore argument, without installing an OS thread or scheduler.
Future asynchronous execution must use the same canonical exclusion state.

Interrupt helpers retain their selected operation and callback across
suspension. Runtime owners, arithmetic addresses and byte owners remain
captured identities. A failure before the original restore leaves the
exclusion state disabled and preserves the completed queue/count prefix;
automatic cleanup must not invent a restore or publish active/status bytes.
An invalid result destination fails after the preceding activation effects.

The manager hook prefers the operation's optional `resume_checked` continuation
and supplies its borrowed fault guard. Existing zero-argument continuations
remain supported, and the selected callback kind does not change while
pending. This prevents a swallowed recursive manager failure inside an
interrupt helper from allowing further child stores. No parent callback is
retained by the activation operation.

## Connected startup boundary

The cold integration first stops with unknown exclusion state. With an
explicitly prepared prior bit, the same retained invocation completes
activation, ends manager construction and publishes the original manager
singleton. Original thread factory caller `80023B90` then reaches abstract
thread redirect `80023E20`, which requires concrete getter `8003AF84`.

The manager, thread list and semaphore are constructed; the concrete main
thread is not allocated. There are 27 registered types, 92 descriptors, 104
default buffers, twelve prototypes and 96 enabled allocations at this boundary.
No wait/signal implementation, host scheduling, complete startup or playable
game is established by activation alone.

See [VALIDATION.md](VALIDATION.md) for local tests and independent comparison
scope. Primary evidence is the supplied USA DOL, SHA1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`; original artifacts remain local.
