## Summary

Describe the change and the production problem it solves.

## Scope

- [ ] This is a release-blocking fix, a security fix or a documentation fix.
- [ ] This targets the 0.1 alpha line (feature work).

## Verification

- [ ] `ctest --test-dir build --output-on-failure`
- [ ] `python Source/Tools/Dev/CheckStyle.py`
- [ ] Added or updated a regression test
- [ ] Checked the frame-path rules: no `std::function`, no per-frame allocation

## Notes for reviewers

Link the issue, describe the data layout/threading contract, and list any
platform-specific behaviour.
