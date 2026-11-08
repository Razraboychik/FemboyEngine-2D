# Roadmap

Current public line: **0.3.0-alpha.1**.

FemboyEngine2D is a data-oriented 2D engine. The current alpha focuses on the
runtime, editor, asset pipeline and deterministic networking that already ship in
the tree. 3D rendering, 3D physics and 3D scene formats are out of scope.

## Priorities

1. Collect feedback on the alpha API and remove ambiguous interfaces before they
   become compatibility obligations.
2. Keep the generated documentation, examples and Clean Slate project in sync
   with the engine.
3. Keep the Windows clang-cl gate and the Linux Clang/GCC checks green; expand
   CI only when a hosted runner is available.
4. Replace third-party code with project-owned libraries only where that reduces
   maintenance cost or improves the public contract.
5. Reach 1.0 only after the public API, file formats and platform policy are
   stable enough to promise compatibility.

## Desktop project portability

FemboyEngine2D targets Windows, Linux and macOS. The editor, player and asset
tools are native desktop applications.

The portability sequence is:

1. Project contract: relative paths, case rules, line endings and asset ids.
2. Portable `.fbyproj` archive with checksums and traversal protection.
3. Native build presets and CI jobs for all three OSes.
4. Editor open/save and local-settings separation on each OS.
5. Per-target cook and shader outputs; cooked data stays out of the project.
6. Native packages and store artifacts.

The detailed contract and acceptance criteria live in
[desktop-portability.md](design/desktop-portability.md). The `ProjectBundle`
archive is the first shared piece: it exports source files, excludes build and
cooked output, verifies checksums and rejects paths that escape the project.
A project must open on all three desktop OSes without conversion.

## Version policy

- `0.1.0-alpha.N` for feedback-driven alpha updates;
- `0.3.0-alpha.N` for a new feature batch;
- `0.9.0-beta.N` when the API is feature-complete for 1.0;
- `1.0.0-rc.N`, then `1.0.0`, after the public contract is frozen.

## Contributing

Open an issue before a feature that changes the public API, a serialized layout
or the build contract. Use a feature branch and a pull request; `main` is
protected. See [CONTRIBUTING.md](../.github/CONTRIBUTING.md).
