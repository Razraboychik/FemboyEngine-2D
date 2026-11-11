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
- Strings are not register values. `LogMessage` stores a localization/message id
  in `LogicNode::param`; the VM and AOT host resolve it through
  `LocalizationTable::FindWithFallback`. This keeps the graph POD, deterministic
  and free of owning strings.

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
