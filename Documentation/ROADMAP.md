# Roadmap

Current public line: **0.1.0-alpha.1**.

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

## Version policy

- `0.1.0-alpha.N` for feedback-driven alpha updates;
- `0.2.0-alpha.N` for a new feature batch;
- `0.9.0-beta.N` when the API is feature-complete for 1.0;
- `1.0.0-rc.N`, then `1.0.0`, after the public contract is frozen.

## Contributing

Open an issue before a feature that changes the public API, a serialized layout
or the build contract. Use a feature branch and a pull request; `main` is
protected. See [CONTRIBUTING.md](../.github/CONTRIBUTING.md).
