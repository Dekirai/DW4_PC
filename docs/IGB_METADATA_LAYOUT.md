# Native metadata layout and registered notifications

`IgbMetadataLayoutInvocation` implements USA `80065A58`. It captures the
original signed field count through `80065D88`, then reloads the current
field-list backing through `800658E4` for each reached index. The initial
parent pointer read is preserved even though the following arithmetic yields
a zero negative-offset cursor for either parent identity.

Every field receives its original virtual `+6C` alignment call. The exact
unsigned-integer descriptor vtable `80476E6C` maps to `80075AF8`, which returns
four. Other vtables remain explicit operations. Alignment results are
truncated to 16 bits; zero and non-power-of-two values retain the original
mask arithmetic rather than being normalized or rejected.

Fields with nonzero byte `+25` use wrapping negative-offset allocation:
`(cursor - size) & ~(alignment - 1)`. Normal fields keep their existing offsets.
The caller reloads the parent, reads its current signed slot count, and compares
the inherited descriptor identity at the same index. Only new normal fields
receive the 16-bit ordinal `index - special_count` and a nonowning declaring
metadata binding. That weak binding distinguishes unknown, null and expired
nonnull identities without a metadata/field ownership cycle.

After iteration, the caller publishes maximum alignment at metadata `+58`.
When native size `+48` differs from `0xFFFFFFFF`, it also writes the original
rounded negative-prefix size at `+20`. The original signed maximum-extent
calculation is preserved, but **does not write native size**. The ready flag
`class_flags | 4` is stored before the fresh ArkCore global read and any
registered notification.

`IgbMetadataCallbacksInvocation` implements `8003E574` independently. It
captures the incoming ArkCore and initial callback count, then reloads that
same core's `+2C` list and its backing for every function. A callback may replace
the list, backing or global ArkCore without changing the captured core/count.
Each callback receives the captured metadata argument. Unknown functions remain
pending with their exact address; null functions, released lists and unavailable
physical slots fail where reached. This notification caller does not destroy
the list or run the separate registration-body cleanup path.

The registration adapter binds only `compute_layout`. Tests cover inherited
identities, wrapping negative offsets, alignment and ordinal truncation,
captured counts with live backing changes, ready-flag failure prefixes,
pending callbacks, callback teardown ordering and nonowning metadata lifetime.
Physical field mappings, arbitrary alignment virtuals and registered callback
bodies remain explicit bindings. This module does not establish complete
metadata registration or game startup.

The independent local comparison executes original `80065A58`, `80065D88`,
`800658E4`, `8003E574` and the known `80075AF8` alignment leaf. Across 1,332
cases, including 1,114 bounded failure prefixes, the native driver matches
110,800 original instructions covering 159 addresses with zero differences.
The known alignment leaf executes 153 times, including all 16 special-field
flag combinations for a four-field fixture. Other alignment virtuals and
registered callback bodies remain prepared endpoints. The comparison includes
metadata/descriptor/list state, declaring identities, endpoint order and
failure prefixes through explicitly represented six-slot pointer backings.

The final proof used local core SHA-256
`933392B281D25E8CB8A7CCC7E8C6781ADF363B54D80DD1C65526358BA6C66931`.
Its corresponding local build passed all 166 CTest suites.
