# Contributing to FemboyEngine2D

Thank you for helping. FemboyEngine2D is a data-oriented C++26 engine; changes
must preserve its two hard frame-path rules: **no `std::function`** and **no
per-frame heap allocation**.

## Before you start

- Read [`Source/Engine/Core/Definitions.hpp`](Source/Engine/Core/Definitions.hpp)
  and the style contract in [`README.md`](README.md).
- The 0.1 alpha line is open for feedback. Open an issue before starting a
  feature that is not a release-blocking fix, a security fix or a documentation
  correction.
- Small, focused pull requests are easier to review than mixed refactors.

## Build and test

```bash
cmake -S . -B build -G Ninja \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DFBY_BUILD_TESTS=ON
cmake --build build --parallel 8
ctest --test-dir build --output-on-failure
python Source/Tools/Dev/CheckStyle.py
```

On Windows use `clang-cl` and run the commands from a Visual Studio developer
prompt.

## Code rules

- Allman braces; every `if`/`else`/`for`/`while` body is braced.
- Input parameters are `In*`, output parameters `Out*`, locals `Scoped*`,
  members `M*`.
- Components are trivially copyable and standard layout. Behaviour belongs to a
  system, not to a component.
- Systems are free functions taking `ecs::SystemContext&`. Do not store a
  `std::function` or a capturing lambda in the frame graph.
- Do not allocate on the frame path. Reserve capacity in `Initialize()` or use a
  fixed-capacity array.
- Do not introduce `dynamic_cast`, `typeid`, `Vec2`/`Vec3`/`Vec4` identifiers or
  Cyrillic comments in engine source.
- Every new public header needs a short comment explaining its ownership and
  threading contract.

## Tests

Every behavioural fix needs a regression test. Prefer a test in the closest
existing `Source/Tests/*.cpp` executable. A bug that only appears under
rollback, PIE, hot reload or a parallel `SystemGraph` wave must be tested through
that exact path, not only through a helper.

## Pull request checklist

- [ ] The change is scoped and explained.
- [ ] `ctest` passes.
- [ ] `python Source/Tools/Dev/CheckStyle.py` passes.
- [ ] New public API is documented in a header comment.
- [ ] No build outputs, caches, `.femarc`, `.fempak`, `.ftex` or editor state
      files are included.
- [ ] The license header situation is unchanged (MPL 2.0).

## License

By contributing you agree that your contribution is licensed under the Mozilla
Public License 2.0, the license of this repository.
