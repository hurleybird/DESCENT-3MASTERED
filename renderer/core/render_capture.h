/* Ordered, API-neutral presented-frame capture storage. */
#pragma once

#include "render_contract.h"
#include "render_wsi_contract.h"

#include <memory>
#include <vector>

namespace piccu
{
namespace render
{

struct CaptureReserve
{
	uint32_t commands;
	uint32_t states;
	uint32_t materials;
	uint32_t texture_versions;
	uint32_t transforms;
	uint32_t views;
	uint32_t viewports;
	uint32_t target_layouts;
	uint32_t target_signatures;
	uint32_t target_versions;
	uint32_t present_rects;
	uint32_t wsi_signatures;
	uint32_t payload_bindings;
	uint32_t payload_records;
	uint32_t payload_bytes;
	uint32_t stream_vertices;
	uint32_t stream_indices;
	uint32_t stream_payload_words;
};

enum CaptureValidationBits : uint64_t
{
	kCaptureInvalidLifecycle = uint64_t(1) << 0,
	kCaptureInvalidCommand = uint64_t(1) << 1,
	kCaptureInvalidTableReference = uint64_t(1) << 2,
	kCaptureInvalidSpan = uint64_t(1) << 3,
	kCaptureIllegalMrtMask = uint64_t(1) << 4,
	kCaptureInvalidReservedBits = uint64_t(1) << 5,
	kCaptureInvalidPayloadShape = uint64_t(1) << 6,
	kCaptureInvalidPayloadBinding = uint64_t(1) << 7,
	kCaptureInvalidTextureVersion = uint64_t(1) << 8,
	kCaptureInvalidTargetRelation = uint64_t(1) << 9,
	kCaptureInvalidEnum = uint64_t(1) << 10,
	kCaptureSerialExhausted = uint64_t(1) << 11,
	kCaptureInvalidCommandGrammar = uint64_t(1) << 12,
	kCaptureInvalidRect = uint64_t(1) << 13,
	kCaptureInvalidGeometry = uint64_t(1) << 14,
};

struct CaptureValidationResult
{
	uint64_t errors;
	uint32_t command_index;
	uint32_t table_index;
};

constexpr uint32_t kCaptureContinuationSchemaVersion = 1;

enum class CaptureSegmentStartKind : uint32_t
{
	Fresh = 0,
	ContinuationAfterReadback = 1,
};

// API-neutral handoff from a synchronously submitted readback prefix.  The
// compiler owns the immutable resource-state snapshot named by serial; this
// record freezes all logical capture grammar/state needed to resume with LOAD.
struct CaptureContinuationState
{
	uint32_t schema_version;
	RenderTargetClass active_target;
	LogicalRect logical_clip;
	uint32_t active_attachment_mask;
	uint32_t load_attachment_mask;
	TargetVersionId active_target_version;
	uint32_t color_epoch;
	uint32_t depth_epoch;
	uint32_t post_present_begun;
	uint32_t cockpit_open;
	uint32_t cockpit_capture_serial;
	uint32_t have_last_view_interval_serial;
	uint32_t last_view_interval_serial;
	uint32_t have_last_font_enqueue_serial;
	uint32_t last_font_enqueue_serial;
	uint64_t prior_submitted_timeline;
	uint64_t resource_state_snapshot_serial;
};

class RenderCaptureSegment
{
public:
	enum class Lifecycle : uint32_t
	{
		Empty = 0,
		Capturing,
		Frozen,
		Compiled,
	};

	RenderCaptureSegment();

	void Reserve(const CaptureReserve &reserve);
	bool Reset(uint32_t presented_frame_serial, uint32_t segment_serial,
		uint64_t first_command_serial = 0);
	bool ResetContinuation(uint32_t presented_frame_serial, uint32_t segment_serial,
		uint64_t first_command_serial, const CaptureContinuationState &state);
	// Starts an independently replayable continuation with every intern table
	// inherited at the same index. Stream geometry is segment-local and is not
	// copied; new continuation draws append from offset zero.
	bool ResetContinuationFrom(const RenderCaptureSegment &prefix,
		uint32_t segment_serial, uint64_t first_command_serial,
		const CaptureContinuationState &state);

	StateId InternState(const CapturedShaderRasterState &state);
	MaterialRef InternMaterial(const CapturedMaterial &material);
	bool RegisterTextureVersion(const CapturedTextureVersion &version);
	TransformId InternTransform(const CapturedTransform &transform);
	ViewStateId InternView(const CapturedWorldView &view);
	// Replaces a capture-owned view before Freeze. Emitted draw commands own
	// immutable view IDs and must never rely on a later replacement.
	bool ReplaceView(ViewStateId id, const CapturedWorldView &view);
	ViewportId InternViewport(const CapturedViewport &viewport);
	TargetLayoutId InternTargetLayout(const CapturedTargetLayout &layout);
	RenderTargetSignatureId InternTargetSignature(const CapturedTargetSignature &signature);
	TargetVersionId InternTargetVersion(const CapturedTargetVersion &version);
	PresentRectId InternPresentRect(const CapturedPresentRect &rect);
	WsiSignatureId InternWsiSignature(const CapturedWsiSignature &signature);
	PayloadDataId CopyPayloadData(const void *data, uint32_t byte_size,
		uint32_t alignment, CapturedPayloadSemantic semantic);
	// Shares an already-immutable payload with the capture instead of copying it
	// into the small transient payload arena. This is intended for persistent,
	// potentially large resource uploads such as texture snapshots.
	PayloadDataId ReferencePayloadData(
		const std::shared_ptr<const std::vector<uint8_t>> &data,
		uint32_t alignment, CapturedPayloadSemantic semantic);
	const uint8_t *PayloadData(PayloadDataId id) const;
	PayloadRef InternPayloadBinding(const CapturedPayloadBinding &binding);

	StreamGeometryRef CopyStreamGeometry(const BaseVertex *vertices,
		uint32_t vertex_count, const uint32_t *indices, uint32_t index_count,
		const uint32_t *optional_payload_words, uint32_t optional_payload_word_count,
		DepthInterpretation depth_interpretation);

	bool AppendCopy(const CaptureCommand &command);

	bool Validate(CaptureValidationResult *result = nullptr) const;
	bool Freeze(CaptureValidationResult *result = nullptr);
	bool MarkCompiled();
	Lifecycle GetLifecycle() const { return lifecycle_; }
	bool IsFrozen() const { return lifecycle_ == Lifecycle::Frozen; }
	bool IsEmpty() const { return commands_.empty(); }

	uint32_t PresentedFrameSerial() const { return presented_frame_serial_; }
	uint32_t SegmentSerial() const { return segment_serial_; }
	CaptureSegmentStartKind StartKind() const { return start_kind_; }
	const CaptureContinuationState &ContinuationState() const { return continuation_state_; }

	const std::vector<CaptureCommand> &Commands() const { return commands_; }
	const std::vector<CapturedShaderRasterState> &States() const { return states_; }
	const std::vector<CapturedMaterial> &Materials() const { return materials_; }
	const std::vector<CapturedTextureVersion> &TextureVersions() const { return texture_versions_; }
	const std::vector<CapturedTransform> &Transforms() const { return transforms_; }
	const std::vector<CapturedWorldView> &Views() const { return views_; }
	const std::vector<CapturedViewport> &Viewports() const { return viewports_; }
	const std::vector<CapturedTargetLayout> &TargetLayouts() const { return target_layouts_; }
	const std::vector<CapturedTargetSignature> &TargetSignatures() const { return target_signatures_; }
	const std::vector<CapturedTargetVersion> &TargetVersions() const { return target_versions_; }
	const std::vector<CapturedPresentRect> &PresentRects() const { return present_rects_; }
	const std::vector<CapturedWsiSignature> &WsiSignatures() const { return wsi_signatures_; }
	const std::vector<CapturedPayloadBinding> &PayloadBindings() const { return payload_bindings_; }
	const std::vector<CapturedPayloadRecord> &PayloadRecords() const { return payload_records_; }
	const std::vector<uint8_t> &PayloadBytes() const { return payload_bytes_; }
	const std::vector<BaseVertex> &StreamVertices() const { return stream_vertices_; }
	const std::vector<uint32_t> &StreamIndices() const { return stream_indices_; }
	const std::vector<uint32_t> &StreamPayloadWords() const { return stream_payload_words_; }

private:
	bool CanMutate() const;
	void RebuildStateHash(size_t minimum_entries);
	void RebuildMaterialHash(size_t minimum_entries);
	void RebuildTransformHash(size_t minimum_entries);

	uint32_t presented_frame_serial_;
	uint32_t segment_serial_;
	uint64_t next_command_serial_;
	Lifecycle lifecycle_;
	CaptureSegmentStartKind start_kind_;
	CaptureContinuationState continuation_state_;

	std::vector<CaptureCommand> commands_;
	std::vector<CapturedShaderRasterState> states_;
	std::vector<uint32_t> state_hash_slots_;
	std::vector<CapturedMaterial> materials_;
	std::vector<uint32_t> material_hash_slots_;
	std::vector<CapturedTextureVersion> texture_versions_;
	std::vector<CapturedTransform> transforms_;
	std::vector<uint32_t> transform_hash_slots_;
	std::vector<CapturedWorldView> views_;
	std::vector<CapturedViewport> viewports_;
	std::vector<CapturedTargetLayout> target_layouts_;
	std::vector<CapturedTargetSignature> target_signatures_;
	std::vector<CapturedTargetVersion> target_versions_;
	std::vector<CapturedPresentRect> present_rects_;
	std::vector<CapturedWsiSignature> wsi_signatures_;
	std::vector<CapturedPayloadBinding> payload_bindings_;
	std::vector<CapturedPayloadRecord> payload_records_;
	std::vector<uint8_t> payload_bytes_;
	// Parallel to payload_records_. Null entries name data in payload_bytes_;
	// non-null entries keep external immutable storage alive through compile.
	std::vector<std::shared_ptr<const std::vector<uint8_t>>> external_payloads_;
	std::vector<BaseVertex> stream_vertices_;
	std::vector<uint32_t> stream_indices_;
	std::vector<uint32_t> stream_payload_words_;
};

} // namespace render
} // namespace piccu
