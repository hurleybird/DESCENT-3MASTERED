# Deterministic renderer corpus schedules

The executable source of truth is `renderer/core/render_verification_contract.cpp`.
This note describes how a runner consumes it; it does not add or waive coverage.

Each `CorpusCaseContract` starts a fresh process, loads its repository-owned D3L
asset, applies the named immutable preference, derives the four named RNG streams
from `root_seed`, and replaces physical input with the fixed 60 Hz input schedule.
The runner then executes the case's `CorpusEventSchedule` at its exact ticks and
captures every inclusive `CorpusFrameSpan` after the named warm-up.

An event with `repeat_count > 1` executes once per consecutive tick beginning at
`tick`. `SweepPreferredField` and `SweepEngineField` advance their enum argument
by one on each repetition; `ReadPixelLoop` repeats its same pixel probe. Faults
are only legal through `InjectFault`, whose argument and `CorpusFault` value must
match. This makes the forced 2x-to-1x MSAA fallback, out-of-date, suboptimal,
surface-lost, and required-device-feature rejection reproducible runner inputs.

`kCorpusCoverageInstantiationContract` is the completion-gate work list. Every
one of the 40 coverage rows has at least one concrete case mapping and a nonempty
mask of events that must occur in that case. Multi-configuration rows enumerate
separate cases: SSAA 1/2/4, requested MSAA 0/2/4/8 plus forced fallback, and gamma
0.5/2.0. The contract tests reject missing rows, unused cases, invalid enum or
span references, missing evidence events, out-of-range repeated fields, events
after the last capture frame, or incomplete fault coverage.

The optional licensed demo remains diagnostic-only and is not required by any
repository CI case. Artifact names remain invocation-explicit and contain no
wildcards.
