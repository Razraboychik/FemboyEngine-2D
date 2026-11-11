# SystemGraph safety contract

[English](system-graph.md) | [Русский](system-graph_ru.md)

`SystemGraph` runs same-phase systems in parallel only when both systems
explicitly set `SystemAccess::ParallelSafe` and their declared access does not
conflict. Serial is the safe default for a public game API. The contract is deliberately explicit: the graph cannot infer
which components a function touches, so a system must declare service bits and,
when it shares ECS data with another same-phase system, component masks.

## Service and component access

`SystemAccess` carries:

- `Reads` / `Writes` - service bits (`kServicePhysics`, `kServiceRenderer`, ...);
- `ComponentReads` / `ComponentWrites` - 256-bit masks built with
  `MakeComponentAccessMask<T...>()`.

`Build()` folds both kinds into dependency edges; a write/read or write/write
overlap keeps registration order instead of running the pair in parallel.
Systems that do not opt into `ParallelSafe` are serialized against every
same-phase peer, so an incomplete declaration cannot silently create a race. This is an explicit contract, not automatic
component tracing; the engine documents that a system which declares nothing may
race another system that touches the same component.

## Exceptions

Worker nodes catch exceptions and report them through `JobSystem::ReportFault`.
The caller-thread node in a multi-node wave catches the same way and then drains
every submitted node before returning, so the per-wave stack storage and
semaphore cannot be unwound under a running worker.

`fby_system_graph_test` exercises ordering, cycles, invalid masks, service
conflicts, component conflicts and a throwing inline node.

## Scaffold budget

The generated `CleanRenderExtract` has a configurable item budget:

- `FBY_PROJECT_MAX_RENDER_ITEMS`, default `16384`;
- `Push()` returns `false` and increments `OverflowCount()` instead of silently
  dropping;
- `Overflowed()` is available to the generated game loop for a diagnostic.

The render path remains allocation-free; the budget is a startup contract.
