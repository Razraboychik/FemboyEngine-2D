# Scripting: debugger and nativization

The language and the VM contract are in [FemboyScript](../md/en/FemboyScript_EN.md);
this document covers the debug section, the source/field helpers, the editor
debugger and the AOT path that turns a cooked program into native code.

## Debug section

A debug build appends an optional section after the program tables: one
`DebugSpan { pc, line, column, flags }` per instruction, named register locals
and the source name. The section is flagged in `ProgramHeader::flags`, so a
program without it loads unchanged and old readers ignore the trailing bytes.

- `TextScript` emits a span per instruction with 1-based line/column and one
  `DebugVariable` per `let`.
- The LogicGraph compiler emits one graph-node span per emitted node range.
- `CompiledDebug::Locate` maps a pc back to a span.

The editor runs a `ScriptRuntime` in the shared graph, so scripts attached to
entities run in the authoring world and in PIE.

## VM debug execution

`ScriptVm::RunDebug` executes the same interpreter with a caller-owned
`VmDebugState`:

- breakpoints are a pc bitmap (8 KiB);
- a step consumes a budget;
- a pause request preserves the registers and the pc for resume;
- `VmStopInfo` reports `Completed`, `Faulted` or `Paused` with the reason and pc.

`Source/Engine/Script/ScriptDebug.hpp` is the source/field layer: `LocatePc`,
`FirstPcForLine`, `VariableName`, `DescribeFields`, `ReadFieldValue`,
`WriteFieldValue` and `MakeFrame`.

## Editor debugger

`Source/Editor/Panels/ScriptDebuggerPanel.*` shows the source with clickable
breakpoints, Continue/Step/Pause/Reset, a Locals table, editable Self fields, a
stack frame and the program counters.

The panel can also load the LogicGraph panel's compiled program ("Debug graph"),
list its node spans with toggles, report the node that owns the paused pc and
mark that node on the canvas with a highlight ring inside the ImNodes frame.

Tests: `script_debug` (spans, locals, breakpoint, resume, stepping, fields, graph
spans, `.fanim` round trip) and `script_debugger_panel` (compile error,
breakpoint pause, resume, fault text, graph breakpoint and node id).

## Nativization

Bytecode is a development format; a cooked build runs native modules.

- `Script::HashProgramBytes` gives a cooked program a stable identity (FNV-1a
  over its bytes). `ScriptRuntime` stores it per slot, and the editor's AOT cook
  uses the same function, so no runtime handle leaks into a build.
- `Script::AotHost` is the nativized program registry. A versioned `.fbyaot`
  manifest (`FBYAOT1`, tab-separated `program <hash> <library> <name>`) maps a
  program hash to a native module, loads modules through `AotModule` (ABI and
  `LayoutHash` validation) and runs an event. `ValidateRequired` is the cook
  gate: every program a scene references must be in the manifest, and missing
  hashes are reported together instead of after a partial deploy.
- `FemboyAotScriptSystem` (`Script/AotScriptSystem.hpp`) is the cooked build's
  VM-free system: `ScriptComponent::Program` is an index into the manifest order,
  and a program without a module is reported and skipped, never interpreted.
- `FemboyScriptSystem` also accepts the host: a nativized program runs native
  code, and `InAllowVmFallback == false` reports skips instead of running the VM.
- **Compile + Nativize**: the debugger compiles the text source, starts
  `AotBuilder::StartProgram` and writes the matching `.fbyaot` manifest on
  success. The LogicGraph inspector has the same button next to "Compile (PIE)".
- **Build without the interpreter**: `FBY_WITH_SCRIPT_VM=OFF` excludes
  `Vm.cpp`/`ScriptSystem.cpp` from the engine and defines
  `FBY_WITH_SCRIPT_VM=0`; the editor and the VM test suite are development-only
  and are skipped in that configuration.

### AOT parity

The plugin ABI is version 5. Parity with the VM is closed:

- the host log callback is signed (`logI32`);
- `DespawnSelf` returns the same 1/0 result the VM writes;
- generated code calls a `fault(code, pc)` hook, with `Aot::FaultCode`
  static-asserted against `VmFault::Code` and fault telemetry in `AotModule`;
- the `LoadI32` immediate is a signed 16-bit value in the VM and the plugin;
- `AppendFormat` is dynamically sized, so generated C++ cannot truncate.

Tests: `LoadI32 decodes a negative immediate`, `AOT logs a signed int32`, `AOT
writes the DespawnSelf result`, `AOT reports division by zero`.

## Known limits

- Per-module hot reload in a cooked build: `AotHost` unloads every module or
  none, and `AotModule` uses a plain `LoadLibraryA` with no watcher.
- No cross-module call inlining and no shipping app instantiates `AotHost`.
- The VM remains a flat event machine: arrays, strings, user functions and
  coroutines need a versioned object model.

These are tracked in [ROADMAP.md](../ROADMAP.md).

## Test

`aot_host` covers the manifest round trip and corruption, the cook gate naming
missing programs, native dispatch with the VM fallback disabled, a skipped
unnativized program, VM fallback still available in dev,
`FemboyAotScriptSystem` without a `ScriptRuntime`, and the stable program hash.
