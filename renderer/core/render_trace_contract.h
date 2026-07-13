/* Versioned deterministic command and attachment trace file ABI. */
#pragma once

#include "render_capabilities.h"
#include "render_capture.h"

#include <string.h>

namespace piccu
{
namespace render
{

constexpr uint32_t kTraceFileVersion = 1;
constexpr uint32_t kTraceSegmentStartRecordSchemaVersion = 1;
constexpr uint8_t kPinnedOracleCommit[20] = {
	0xd3, 0x44, 0x94, 0xae, 0x2b, 0x3c, 0x75, 0x8c, 0x88, 0xb7,
	0xf9, 0xe2, 0x17, 0x51, 0xc0, 0x10, 0x7e, 0xc9, 0x41, 0x4d
};
constexpr uint8_t kPinnedRendererSpecSha256[32] = {
	0x8e, 0x5f, 0xe7, 0x5e, 0x35, 0x33, 0xe5, 0x1a,
	0xce, 0xd7, 0x88, 0x7e, 0x9e, 0x2c, 0x7a, 0xf9,
	0xb2, 0xfb, 0xb3, 0xd5, 0x57, 0x7a, 0xd1, 0x59,
	0xc9, 0x5e, 0xec, 0xbc, 0x6a, 0xc0, 0x24, 0xfd
};

enum class TraceTableKind : uint32_t
{
	Commands = 0,
	States,
	Materials,
	Transforms,
	Views,
	Viewports,
	TargetLayouts,
	TargetSignatures,
	TargetVersions,
	PresentRects,
	TextureVersions,
	PayloadBindings,
	PayloadRecords,
	PayloadBytes,
	StreamVertices,
	StreamIndices,
	StreamPayloadWords,
	WsiSignatures,
	DeviceIdentity,
	ReproMetadata,
	// Appended so the initially frozen numeric values above remain stable.
	SegmentStartStates,
	Count,
};

struct DeterministicTraceMetadata
{
	uint32_t schema_version;
	uint32_t fixed_timestep_numerator;
	uint32_t fixed_timestep_denominator;
	uint32_t warmup_frame_count;
	uint32_t capture_frame;
	uint32_t graph_stage;
	uint32_t target_signature_id;
	uint32_t wsi_signature_id;
	uint32_t case_frame_origin;
	uint32_t engine_frame_at_level_load;
	uint32_t rng_stream_count;
	uint32_t serialization_version;
	uint64_t random_seed;
	uint64_t simulation_tick_at_level_load;
	uint8_t renderer_spec_sha256[32];
	uint8_t build_source_tree_sha256[32];
	uint8_t asset_sha256[32];
	uint8_t runtime_content_closure_sha256[32];
	uint8_t input_stream_sha256[32];
	uint8_t preferences_sha256[32];
	uint8_t rng_registry_sha256[32];
	uint32_t derived_rng_seeds[4];
	char corpus_case_id[64];
	char asset_id[128];
	char input_stream_id[64];
	char preference_case_id[64];
	char build_id[64];
};

struct CapturedDeviceIdentity
{
	uint32_t vendor_id;
	uint32_t device_id;
	uint32_t driver_id;
	uint32_t driver_version;
	uint32_t api_version;
	uint32_t device_type;
	uint32_t graphics_queue_family;
	uint32_t present_queue_family;
	uint8_t device_uuid[16];
	uint8_t driver_uuid[16];
	char device_name[128];
};

struct TraceSerializationContract
{
	uint32_t version;
	uint32_t canonical_little_endian;
	uint32_t ieee754_binary32_and_binary64;
	uint32_t field_serialized_without_native_padding;
	uint32_t utf8_nul_terminated_fixed_text;
	uint32_t sha256_payload_integrity;
};

constexpr TraceSerializationContract kTraceSerializationContract =
	{ 1, 1, 1, 1, 1, 1 };

struct TraceFileHeader
{
	uint8_t magic[8];                 // "PICCURTR"
	uint32_t trace_file_version;
	uint32_t renderer_capabilities_abi_version;
	uint32_t render_contract_version;
	uint32_t capture_schema_version;
	uint32_t shader_abi_version;
	uint8_t oracle_commit[20];
	uint32_t backend;
	uint32_t presented_frame_serial;
	uint32_t segment_serial;
	CaptureSegmentStartKind segment_start_kind;
	uint32_t segment_start_table_index;
	uint32_t continuation_state_schema_version;
	uint32_t command_count;
	uint64_t first_command_serial;
	uint64_t last_command_serial;
	uint32_t target_signature_id;
	uint32_t branch_flags;
	uint32_t history_flags;
	uint32_t table_directory_count;
	uint64_t table_directory_offset;
	uint64_t file_size;
};

// There is exactly one of these records in every trace segment.  Fresh
// segments carry the capture's canonical zero continuation state; readback
// continuations carry the complete CaptureContinuationState copied at the
// capture/trace boundary.  A replay therefore never needs an earlier segment
// to reconstruct the logical target, epochs, timeline, or resource snapshot.
struct TraceSegmentStartRecord
{
	uint32_t schema_version;
	CaptureSegmentStartKind start_kind;
	CaptureContinuationState continuation_state;
};

struct TraceTableDirectoryEntry
{
	TraceTableKind kind;
	uint32_t element_stride;
	uint64_t element_count;
	uint64_t file_offset;
	uint64_t byte_size;
};

struct AttachmentCaptureHeader
{
	uint8_t magic[8];                 // "PICCUATT"
	uint32_t trace_file_version;
	uint32_t presented_frame_serial;
	uint32_t graph_node_id;
	uint32_t graph_invocation_index;
	uint32_t image_semantic;
	uint32_t format;
	uint32_t width;
	uint32_t height;
	uint32_t layers;
	uint32_t samples;
	uint32_t top_down_rows;
	uint32_t target_signature_id;
	uint32_t branch_flags;
	uint32_t history_flags;
	uint64_t payload_byte_size;
	uint32_t row_pitch_bytes;
	uint32_t serialization_flags;
	uint8_t payload_sha256[32];
	char diagnostic_name[64];
};

enum TraceValidationBits : uint32_t
{
	kTraceInvalidHeader = 1u << 0,
	kTraceInvalidSchema = 1u << 1,
	kTraceInvalidDirectory = 1u << 2,
	kTraceInvalidTableKind = 1u << 3,
	kTraceDuplicateOrMissingTable = 1u << 4,
	kTraceInvalidTableBounds = 1u << 5,
	kTraceInvalidTableShape = 1u << 6,
	kTraceInvalidSegmentStart = 1u << 7,
	kTraceSegmentStartMismatch = 1u << 8,
};

struct TraceValidationResult
{
	uint32_t errors;
	uint32_t table_index;
};

// Serialized sizes are sums of fields, not sizeof(native structs).  The trace
// serialization contract writes every field in declaration order without
// compiler padding.
constexpr uint32_t kTraceFileHeaderSerializedSize = 124;
constexpr uint32_t kTraceTableDirectoryEntrySerializedSize = 32;
constexpr uint32_t kTraceSegmentStartRecordSerializedSize =
	8 + static_cast<uint32_t>(sizeof(CaptureContinuationState));

inline TraceSegmentStartRecord MakeTraceSegmentStartRecord(
	const RenderCaptureSegment &segment)
{
	TraceSegmentStartRecord record = {};
	record.schema_version = kTraceSegmentStartRecordSchemaVersion;
	record.start_kind = segment.StartKind();
	record.continuation_state = segment.ContinuationState();
	return record;
}

inline bool TraceContinuationStatesEqual(const CaptureContinuationState &a,
	const CaptureContinuationState &b)
{
	return a.schema_version == b.schema_version &&
		a.active_target == b.active_target &&
		a.logical_clip.x == b.logical_clip.x &&
		a.logical_clip.y == b.logical_clip.y &&
		a.logical_clip.width == b.logical_clip.width &&
		a.logical_clip.height == b.logical_clip.height &&
		a.active_attachment_mask == b.active_attachment_mask &&
		a.load_attachment_mask == b.load_attachment_mask &&
		a.active_target_version == b.active_target_version &&
		a.color_epoch == b.color_epoch &&
		a.depth_epoch == b.depth_epoch &&
		a.post_present_begun == b.post_present_begun &&
		a.cockpit_open == b.cockpit_open &&
		a.cockpit_capture_serial == b.cockpit_capture_serial &&
		a.have_last_view_interval_serial == b.have_last_view_interval_serial &&
		a.last_view_interval_serial == b.last_view_interval_serial &&
		a.have_last_font_enqueue_serial == b.have_last_font_enqueue_serial &&
		a.last_font_enqueue_serial == b.last_font_enqueue_serial &&
		a.prior_submitted_timeline == b.prior_submitted_timeline &&
		a.resource_state_snapshot_serial == b.resource_state_snapshot_serial;
}

inline bool TraceContinuationStateIsCanonicalZero(
	const CaptureContinuationState &state)
{
	const CaptureContinuationState zero = {};
	return TraceContinuationStatesEqual(state, zero);
}

inline bool TraceSegmentStartMatchesCapture(
	const TraceSegmentStartRecord &record, const RenderCaptureSegment &segment)
{
	return record.schema_version == kTraceSegmentStartRecordSchemaVersion &&
		record.start_kind == segment.StartKind() &&
		TraceContinuationStatesEqual(record.continuation_state,
			segment.ContinuationState());
}

// Validates the field-serialized trace envelope and the segment-start table.
// The caller supplies the decoded SegmentStartStates payload and, when
// serializing directly from capture, may supply the source segment to prove
// that the complete continuation state was copied exactly.
inline bool ValidateTraceFileContract(const TraceFileHeader &header,
	const TraceTableDirectoryEntry *directory, size_t directory_count,
	const TraceSegmentStartRecord *segment_start_records,
	size_t segment_start_record_count,
	const RenderCaptureSegment *source_segment,
	TraceValidationResult *result = nullptr)
{
	TraceValidationResult local = {};
	local.table_index = kInvalidId;
	const uint8_t expected_magic[8] =
		{ 'P', 'I', 'C', 'C', 'U', 'R', 'T', 'R' };
	if (memcmp(header.magic, expected_magic, sizeof(expected_magic)) != 0 ||
		header.file_size < kTraceFileHeaderSerializedSize)
		local.errors |= kTraceInvalidHeader;
	if (header.trace_file_version != kTraceFileVersion ||
		header.renderer_capabilities_abi_version !=
			RENDERER_CAPABILITIES_ABI_VERSION ||
		header.render_contract_version != kRenderContractVersion ||
		header.capture_schema_version != kCaptureSchemaVersion ||
		header.shader_abi_version != kShaderAbiVersion ||
		memcmp(header.oracle_commit, kPinnedOracleCommit,
			sizeof(kPinnedOracleCommit)) != 0 ||
		header.backend > static_cast<uint32_t>(RENDERER_BACKEND_VULKAN))
		local.errors |= kTraceInvalidSchema;
	if (source_segment &&
		(header.presented_frame_serial != source_segment->PresentedFrameSerial() ||
		header.segment_serial != source_segment->SegmentSerial() ||
		header.segment_start_kind != source_segment->StartKind() ||
		header.command_count != source_segment->Commands().size()))
		local.errors |= kTraceSegmentStartMismatch;

	const size_t required_count = static_cast<size_t>(TraceTableKind::Count);
	if (!directory || directory_count != required_count ||
		header.table_directory_count != required_count ||
		header.table_directory_offset < kTraceFileHeaderSerializedSize ||
		header.table_directory_offset > header.file_size ||
		required_count > (UINT64_MAX / kTraceTableDirectoryEntrySerializedSize) ||
		header.file_size - header.table_directory_offset <
			required_count * kTraceTableDirectoryEntrySerializedSize)
	{
		local.errors |= kTraceInvalidDirectory;
	}

	bool seen[static_cast<size_t>(TraceTableKind::Count)] = {};
	const TraceTableDirectoryEntry *commands = nullptr;
	const TraceTableDirectoryEntry *target_versions = nullptr;
	const TraceTableDirectoryEntry *segment_starts = nullptr;
	if (directory && directory_count == required_count)
	{
		for (size_t i = 0; i < directory_count; ++i)
		{
			const TraceTableDirectoryEntry &entry = directory[i];
			const size_t kind = static_cast<size_t>(entry.kind);
			if (kind >= required_count)
			{
				local.errors |= kTraceInvalidTableKind;
				local.table_index = static_cast<uint32_t>(i);
				continue;
			}
			if (seen[kind])
			{
				local.errors |= kTraceDuplicateOrMissingTable;
				local.table_index = static_cast<uint32_t>(i);
			}
			seen[kind] = true;
			if ((entry.element_count != 0 && entry.element_stride == 0) ||
				(entry.element_stride != 0 && entry.element_count >
					UINT64_MAX / entry.element_stride) ||
				entry.byte_size != entry.element_count * entry.element_stride)
			{
				local.errors |= kTraceInvalidTableShape;
				local.table_index = static_cast<uint32_t>(i);
			}
			if (entry.file_offset > header.file_size ||
				entry.byte_size > header.file_size - entry.file_offset)
			{
				local.errors |= kTraceInvalidTableBounds;
				local.table_index = static_cast<uint32_t>(i);
			}
			if (entry.kind == TraceTableKind::Commands)
				commands = &entry;
			else if (entry.kind == TraceTableKind::TargetVersions)
				target_versions = &entry;
			else if (entry.kind == TraceTableKind::SegmentStartStates)
				segment_starts = &entry;
		}
		for (size_t i = 0; i < required_count; ++i)
			if (!seen[i])
				local.errors |= kTraceDuplicateOrMissingTable;
	}

	if (!commands || commands->element_count != header.command_count ||
		commands->element_stride != sizeof(CaptureCommand))
		local.errors |= kTraceInvalidTableShape;
	if (source_segment &&
		(!target_versions || target_versions->element_count !=
			source_segment->TargetVersions().size()))
		local.errors |= kTraceSegmentStartMismatch;

	const uint32_t start_index = header.segment_start_table_index;
	if (start_index >= directory_count || !directory ||
		directory[start_index].kind != TraceTableKind::SegmentStartStates ||
		!segment_starts || segment_starts != &directory[start_index] ||
		segment_starts->element_count != 1 ||
		segment_starts->element_stride != kTraceSegmentStartRecordSerializedSize ||
		segment_start_record_count != 1 || !segment_start_records)
	{
		local.errors |= kTraceInvalidSegmentStart;
	}
	else
	{
		const TraceSegmentStartRecord &record = segment_start_records[0];
		const uint32_t kind = static_cast<uint32_t>(record.start_kind);
		if (record.schema_version != kTraceSegmentStartRecordSchemaVersion ||
			kind > static_cast<uint32_t>(
				CaptureSegmentStartKind::ContinuationAfterReadback) ||
			record.start_kind != header.segment_start_kind ||
			record.continuation_state.schema_version !=
				header.continuation_state_schema_version ||
			(record.start_kind == CaptureSegmentStartKind::Fresh &&
				!TraceContinuationStateIsCanonicalZero(
					record.continuation_state)) ||
			(record.start_kind == CaptureSegmentStartKind::ContinuationAfterReadback &&
				(record.continuation_state.schema_version !=
					kCaptureContinuationSchemaVersion ||
				 !target_versions ||
				 target_versions->element_stride != sizeof(CapturedTargetVersion) ||
				 record.continuation_state.active_target_version >=
					target_versions->element_count)))
			local.errors |= kTraceInvalidSegmentStart;
		if (source_segment && !TraceSegmentStartMatchesCapture(record,
			*source_segment))
			local.errors |= kTraceSegmentStartMismatch;
	}

	if (result)
		*result = local;
	return local.errors == 0;
}

static_assert(sizeof(TraceTableDirectoryEntry) == 32,
	"trace directory ABI changed");
static_assert(offsetof(TraceTableDirectoryEntry, kind) == 0,
	"trace directory ABI changed");
static_assert(offsetof(TraceTableDirectoryEntry, element_count) == 8,
	"trace directory ABI changed");
static_assert(sizeof(AttachmentCaptureHeader) == 176,
	"attachment capture ABI changed");
static_assert(sizeof(DeterministicTraceMetadata) == 688,
	"deterministic trace metadata ABI changed");
static_assert(offsetof(DeterministicTraceMetadata, random_seed) == 48,
	"deterministic trace metadata ABI changed");
static_assert(offsetof(DeterministicTraceMetadata, renderer_spec_sha256) == 64,
	"deterministic trace metadata ABI changed");
static_assert(sizeof(CapturedDeviceIdentity) == 192,
	"captured device identity ABI changed");
static_assert(sizeof(CaptureSegmentStartKind) == sizeof(uint32_t),
	"trace segment-start enum ABI changed");
static_assert(sizeof(CaptureContinuationState) == 88,
	"trace v1 continuation-state ABI changed");
static_assert(offsetof(CaptureContinuationState, active_target_version) == 32,
	"trace v1 continuation target-version index moved");
static_assert(offsetof(CaptureContinuationState, prior_submitted_timeline) == 72,
	"trace v1 continuation timeline moved");
static_assert(offsetof(CaptureContinuationState,
	resource_state_snapshot_serial) == 80,
	"trace v1 continuation resource-state snapshot moved");
static_assert(std::is_standard_layout<TraceSegmentStartRecord>::value,
	"trace segment-start record must remain field-serializable");
static_assert(offsetof(TraceSegmentStartRecord, continuation_state) == 8,
	"trace segment-start record ABI changed");
static_assert(sizeof(TraceSegmentStartRecord) ==
	kTraceSegmentStartRecordSerializedSize,
	"trace segment-start record gained native padding");
static_assert(sizeof(TraceFileHeader) == 128,
	"trace header ABI changed");
static_assert(offsetof(TraceFileHeader, segment_start_kind) == 60,
	"trace header ABI changed");
static_assert(offsetof(TraceFileHeader, first_command_serial) == 80,
	"trace header ABI changed");
static_assert(std::is_standard_layout<TraceFileHeader>::value,
	"trace header must remain field-serializable");

} // namespace render
} // namespace piccu
