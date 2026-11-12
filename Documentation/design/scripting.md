# LogicGraph values, messages and AOT

LogicGraph is a visual front end for the same bytecode the text compiler emits.
The editor runs the bytecode in the VM; a release cook transpiles it to C++26 and
builds a native module. `FemboyScriptSystem` prefers the AOT module for a program
hash and only falls back to the VM when the host allows it.

## Value model

- A graph pin is typed (`Float`, `Vector2D`, `Exec`, `BigInt`, `Decimal`).
- Float constants are stored as `LogicNode::param = bit_cast<uint32_t>(value)`.
  The field is a 32-bit storage slot, not an integer argument type. `ConstVec2`
  uses `param` and `param2` for its two float bits.
- VM registers are a union of `float`, `int32` and `uint32`. A float is preserved
  until a native explicitly converts it.
- `LogInt` uses `LogIntFromFloat`, which reads the float register and casts only
  at logging time. There is no silent truncation on the graph path.
- Strings, arrays and maps are heap handles stored in a register payload and
  tagged with `ValueTag`. `LogMessage` still stores a localization/message id in
  `LogicNode::param`; the VM and AOT host resolve it through
  `LocalizationTable::FindWithFallback`.

## Collections

- `ScriptHeap` is a fixed-capacity, allocation-free-after-construction store for
  strings, arrays and maps. It is owned by `ScriptRuntime`; one heap is shared by
  the VM and the AOT host for the lifetime of the script world.
- The runtime exposes natives instead of new collection opcodes:
  `array_new`, `array_push`, `array_set`, `array_get`, `array_length`,
  `map_new`, `map_set`, `map_get`, `map_has`, `map_length`, `string_concat`,
  `string_length` and `string_equals`.
- `LoadString` (bytecode version 2) interns a cooked UTF-8 literal from the
  program string table. Version 1 programs still parse with an empty table.
- Register tags are mirrored by the generated AOT `Registers` struct; the heap
  is reached through `HostApi` callbacks, so a native plugin never allocates and
  the VM and released build see the same strings, arrays and maps.
- Overflow returns a script fault (`no-heap`, `heap-full`, `bad-collection`,
  `out-of-range`, `missing-key`) rather than growing the heap.
- User functions are textual-only in 0.5.0: `function name(a, b) { ... }` before
  the events. Each function owns an 8-register window at
  `64 + index * 8`; calls are depth-limited to 16 and recursion is rejected at
  runtime. `CallFunction` stores the result register, `Return` moves the result
  to the function window base, and the call site copies it back.
- The AOT backend emits one `fby_fn_<index>` per function with the same absolute
  register window, a `ctx.callDepth` guard and prototypes for every function.
- Coroutines and forward function references are not part of 0.5.0.

## Nodes added for parity

- `GetFrameIndex`;
- `RandomRange`;
- `LogVec2`;
- `LogInt`;
- `LogMessage` (message id, editable on the node);
- `PlaySound` (asset id, VM and AOT implementations).

## Still intentionally not nodes

`log_message` with a raw string is not a node: the graph stores message ids.
Transport group nodes need an AOT host contract for `TransportGroupSystem`; they
are not added until the AOT path has the same behaviour as the VM. Raw pointers,
allocations and editor-only services never become VM nodes.
