# Renderer contract transcription notes

The authoritative checked-in specification is
`docs/piccuengine_vulkan_renderer_spec.md`, SHA-256
`8e5fe75e3533e51aced7887e9e2c7af9b2fbb3d5577ad159c95eecbc6ac024fd`.
It is an exact copy of the document supplied for this implementation.

The supplied specification's `CapturedDraw::optional_payload` is transcribed
as one `PayloadRef` in both `DrawStream` and `DrawRetained`. It selects the
capture-owned immutable per-draw payload binding described by section 6.1.
`DrawRetained::geometry_mode` makes its two legal forms explicit. T1 freezes an
indexed triangle range and the three persistent typed ranges returned by
`IRetainedWorld::ResolveFace`; those ranges are independent face-token outputs
and cannot be reconstructed uniquely from a mesh/index tuple. T2 uses the
canonical nonindexed zero form and empty T1 ranges. Its `PayloadRef` must bind
the four capture-owned typed records `TerrainEmitterCell`, `TerrainWorkItem`,
`TerrainBatchInput`, and `TerrainViewInput`. The generic `geometry_aux` member
is never a substitute for any T2 record.

The T2 records live only in `render_contract.h`; the retained-world upload API
consumes those same declarations. Capture validation locks their semantic IDs,
validity bits, offsets, alignments, batch/work/cell relationships, output
capacity, source ordering, reserved lanes, and the exact one-view shape.

The command stream therefore remains schema version 1 and contains no
backend-local extension to the supplied capture schema.

`BeginFrameTarget::active_target_version` is a `TargetVersionId` table index,
not the target record's generation value. It identifies the exact physical
attachment set receiving LOAD/clear/draw operations. A readback continuation
uses the same table-index meaning and must reproduce the referenced target
class, layout, attachment mask, color epoch, and depth epoch before its first
resumed command. This keeps synchronous-prefix continuation state serializable
without a backend image handle.

The frozen post descriptor ABI selects deterministic custom multisample
resolve shaders for every `RES_*` node. This is the uniform implementation of
section 10.6's exact average/sample-zero rules; fixed-function resolve remains
permitted by the prose only when identical, but is not a second baseline path.

Trace format version 1 is the initial, not-yet-shipped trace definition. Its
table-kind values are append-only: `SegmentStartStates` was appended after the
previous v1 inventory. Every segment serializes exactly one versioned
`TraceSegmentStartRecord`. The header names that directory entry, duplicates
the start kind and continuation-schema version for early rejection, and the
record contains the complete capture-owned `CaptureContinuationState`.
Consequently a continuation after a synchronous readback is independently
replayable; a replay may not infer its active target, LOAD state, epochs,
timeline wait, or resource-state snapshot from an earlier trace file.

Physical-device selection first filters the complete required API/feature/
format/limit/surface/requested-signature profile. An explicit UUID or original
enumeration-index override is exclusive and exact: missing or profile-rejected
overrides are reported and never fall through to an automatic choice. The
automatic stable rank is, in order, a unified graphics/present family, device
type (discrete, integrated, virtual, CPU, other), device-local budget,
descriptor-page tier, common attachment sample count, then ascending 16-byte
UUID. Duplicate UUIDs or original enumeration indices invalidate the inventory,
so array enumeration order can never become an implicit final tie-break.

Captured texture bytes use one canonical manifest: mip-major/layer-minor
subresources, top-down rows, and tight row/layer/mip packing beginning at byte
zero. The capture payload alignment remains exactly four bytes. Each row pitch
is `mip_width * format_bytes_per_texel`, every relative subresource offset is
texel-block aligned, supplied mip dimensions use repeated floor-halving clamped
to one, and no padding or invented mip is serialized. Builders and validators
reject invalid mip chains, misalignment, capacity/size mismatches, and integer
overflow before a compiler may consume the payload.
