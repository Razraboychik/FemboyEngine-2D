# Parser fuzz corpus

The seeds here are valid inputs for the two first-party importers covered by the
deterministic mutation sweep. `FuzzParsersTest` loads each seed, applies bit,
truncation, erasure and insertion mutations, and requires the parser to return a
controlled result rather than crash.

The corpus is deliberately small; `FBY_FUZZ_ITERATIONS` scales the mutation
budget without changing the seeds.
