#include "core/render_capture.h"
#include "core/render_capabilities.h"
#include "core/render_coordinate_contract.h"
#include "core/render_device_contract.h"
#include "core/render_graph_contract.h"
#include "core/render_lifetime_contract.h"
#include "core/render_state_contract.h"
#include "core/render_trace_contract.h"
#include "core/render_verification_contract.h"
#include "core/retained_world.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits>

using namespace piccu::render;

#define ABI_OFFSET(type, member, expected) \
	static_assert(offsetof(type, member) == expected, #type "." #member " offset")

ABI_OFFSET(BaseVertex, position, 0); ABI_OFFSET(BaseVertex, rgba8, 12);
ABI_OFFSET(BaseVertex, uv0, 16); ABI_OFFSET(BaseVertex, uv1, 24);
ABI_OFFSET(GpuDrawHeader, state_index, 0); ABI_OFFSET(GpuDrawHeader, material_index, 4);
ABI_OFFSET(GpuDrawHeader, transform_index, 8); ABI_OFFSET(GpuDrawHeader, flags, 12);
ABI_OFFSET(GpuDrawHeader, vertex_payload_offset, 16); ABI_OFFSET(GpuDrawHeader, motion_payload_offset, 20);
ABI_OFFSET(GpuDrawHeader, specular_payload_offset, 24); ABI_OFFSET(GpuDrawHeader, room_or_terrain_index, 28);
ABI_OFFSET(GpuShaderState, shader_flags, 0); ABI_OFFSET(GpuShaderState, texture_type, 4);
ABI_OFFSET(GpuShaderState, overlay_type, 8); ABI_OFFSET(GpuShaderState, lighting_color_model, 12);
ABI_OFFSET(GpuShaderState, alpha_type, 16); ABI_OFFSET(GpuShaderState, alpha_value, 20);
ABI_OFFSET(GpuShaderState, blend_class, 24); ABI_OFFSET(GpuShaderState, draw_classification, 28);
ABI_OFFSET(GpuShaderState, alpha_factor, 32); ABI_OFFSET(GpuShaderState, z_bias, 36);
ABI_OFFSET(GpuShaderState, fog_near_mapped, 40); ABI_OFFSET(GpuShaderState, fog_far_mapped, 44);
ABI_OFFSET(GpuShaderState, flat_color, 48); ABI_OFFSET(GpuShaderState, fog_color, 64);
ABI_OFFSET(GpuShaderState, light_direction, 80); ABI_OFFSET(GpuShaderState, post_values, 96);
ABI_OFFSET(GpuShaderState, dynamic_light_first, 112); ABI_OFFSET(GpuShaderState, dynamic_light_count, 116);
ABI_OFFSET(GpuShaderState, specular_block_index, 120); ABI_OFFSET(GpuShaderState, motion_object_id, 124);
ABI_OFFSET(GpuShaderState, motion_flags, 128); ABI_OFFSET(GpuShaderState, ao_class, 132);
ABI_OFFSET(GpuShaderState, state_flags2, 136); ABI_OFFSET(GpuShaderState, vertex_index_base, 140);
ABI_OFFSET(GpuMaterial, image2d, 0); ABI_OFFSET(GpuMaterial, image2d_array, 16);
ABI_OFFSET(GpuMaterial, sampler, 32); ABI_OFFSET(GpuMaterial, uv_params, 48);
ABI_OFFSET(GpuTransform, current_model, 0); ABI_OFFSET(GpuTransform, previous_model, 64);
ABI_OFFSET(GpuDynamicLight, position_radius, 0); ABI_OFFSET(GpuDynamicLight, color_falloff, 16);
ABI_OFFSET(GpuDynamicLight, direction_dot_range, 32); ABI_OFFSET(GpuDynamicLight, specular_position_radius, 48);
ABI_OFFSET(GpuDynamicLight, specular_and_flags, 64);
ABI_OFFSET(GpuSpecularDef, center, 0); ABI_OFFSET(GpuSpecularDef, color, 16);
ABI_OFFSET(GpuSpecularBlock, count, 0); ABI_OFFSET(GpuSpecularBlock, exponent, 4);
ABI_OFFSET(GpuSpecularBlock, strength, 8); ABI_OFFSET(GpuSpecularBlock, lightmap_mix, 12);
ABI_OFFSET(GpuSpecularBlock, alpha_strength, 16); ABI_OFFSET(GpuSpecularBlock, field_mode, 20);
ABI_OFFSET(GpuSpecularBlock, debug_tint, 24); ABI_OFFSET(GpuSpecularBlock, debug_authored, 28);
ABI_OFFSET(GpuSpecularBlock, sources, 32);
ABI_OFFSET(GpuWorldAux, fog_color, 0); ABI_OFFSET(GpuWorldAux, fog_plane, 16);
ABI_OFFSET(GpuWorldAux, params, 32); ABI_OFFSET(GpuWorldAux, indices, 48);
ABI_OFFSET(PerspectiveVertexPayload, value_q, 0);
ABI_OFFSET(MotionVertexPayload, current_q, 0); ABI_OFFSET(MotionVertexPayload, previous_q, 16);
ABI_OFFSET(SpecularVertexPayload, normal_or_position_q, 0);
ABI_OFFSET(SpecularVertexPayload, field_center_q, 16); ABI_OFFSET(SpecularVertexPayload, field_color, 80);
ABI_OFFSET(TerrainVertexPayload, world_q, 0); ABI_OFFSET(TerrainVertexPayload, packed_pages, 16);
ABI_OFFSET(FrameViewGlobals, projection, 0); ABI_OFFSET(FrameViewGlobals, view, 64);
ABI_OFFSET(FrameViewGlobals, view_projection, 128); ABI_OFFSET(FrameViewGlobals, inverse_modelview, 192);
ABI_OFFSET(FrameViewGlobals, inverse_view_projection, 256); ABI_OFFSET(FrameViewGlobals, previous_view_projection, 320);
ABI_OFFSET(FrameViewGlobals, cockpit_previous_view_projection, 384); ABI_OFFSET(FrameViewGlobals, viewport_xywh, 448);
ABI_OFFSET(FrameViewGlobals, visible_origin_size, 464); ABI_OFFSET(FrameViewGlobals, target_extent_inv_extent, 480);
ABI_OFFSET(FrameViewGlobals, history_target_flags, 496);
ABI_OFFSET(WorldBatchPush, draw_header_base, 0); ABI_OFFSET(WorldBatchPush, view_record_index, 4);
ABI_OFFSET(WorldBatchPush, target_flags, 8); ABI_OFFSET(WorldBatchPush, payload_word_base, 12);

static_assert(alignof(PerspectiveVertexPayload) == 16, "payload alignment");
static_assert(alignof(MotionVertexPayload) == 16, "payload alignment");
static_assert(alignof(SpecularVertexPayload) == 16, "payload alignment");
static_assert(alignof(TerrainVertexPayload) == 16, "payload alignment");
static_assert(alignof(GpuDrawHeader) == 16, "GPU ABI alignment");
static_assert(alignof(GpuShaderState) == 16, "GPU ABI alignment");
static_assert(alignof(GpuMaterial) == 16, "GPU ABI alignment");
static_assert(alignof(GpuTransform) == 16, "GPU ABI alignment");
static_assert(alignof(GpuDynamicLight) == 16, "GPU ABI alignment");
static_assert(alignof(GpuSpecularBlock) == 16, "GPU ABI alignment");
static_assert(alignof(GpuWorldAux) == 16, "GPU ABI alignment");
static_assert(alignof(FrameViewGlobals) == 16, "GPU ABI alignment");
static_assert(alignof(WorldBatchPush) == 16, "GPU ABI alignment");

static_assert(sizeof(CaptureCommandPayload) == 216, "capture payload ABI");
static_assert(sizeof(CaptureCommand) == 232, "capture command ABI");
static_assert(sizeof(BeginFrameTargetCommand) == 36, "BeginFrameTarget ABI");
static_assert(sizeof(ClearColorCommand) == 64, "ClearColor ABI");
static_assert(sizeof(ClearDepthCommand) == 28, "ClearDepth ABI");
static_assert(sizeof(ClearAlphaOnlyCommand) == 28, "ClearAlphaOnly ABI");
static_assert(sizeof(DrawStreamCommand) == 60, "DrawStream ABI");
static_assert(sizeof(DrawRetainedCommand) == 80, "DrawRetained ABI");
static_assert(sizeof(EnqueueFontGlyphCommand) == 216, "EnqueueFontGlyph ABI");
static_assert(sizeof(FlushFontBatchesCommand) == 12, "FlushFontBatches ABI");
static_assert(sizeof(AcquireSoftDepthCommand) == 12, "AcquireSoftDepth ABI");
static_assert(sizeof(CaptureBloomSourceCommand) == 32, "CaptureBloomSource ABI");
static_assert(sizeof(BeginPostPresentCommand) == 8, "BeginPostPresent ABI");
static_assert(sizeof(BeginCockpitSceneCommand) == 24, "BeginCockpitScene ABI");
static_assert(sizeof(EndCockpitSceneCommand) == 4, "EndCockpitScene ABI");
static_assert(sizeof(PerfMarkerCommand) == 8, "PerfMarker ABI");
static_assert(sizeof(ReadPixelCommand) == 20, "ReadPixel ABI");
static_assert(sizeof(ReadImageCommand) == 32, "ReadImage ABI");
static_assert(sizeof(EndFrameCommand) == 4, "EndFrame ABI");
static_assert(sizeof(PresentCommand) == 12, "Present ABI");
ABI_OFFSET(ClearColorCommand, selected_attachments, 40);
ABI_OFFSET(ClearColorCommand, attachment_channel_masks, 44);
ABI_OFFSET(BeginFrameTargetCommand, active_target_version, 32);
ABI_OFFSET(DrawRetainedCommand, geometry_mode, 20);
ABI_OFFSET(DrawRetainedCommand, perspective_payload, 24);
ABI_OFFSET(DrawRetainedCommand, motion_payload, 32);
ABI_OFFSET(DrawRetainedCommand, specular_payload, 40);
ABI_OFFSET(DrawRetainedCommand, optional_payload, 60);
static_assert(sizeof(CapturedPayloadBinding) == 56, "captured payload binding ABI");
ABI_OFFSET(CapturedPayloadBinding, perspective_vertices, 0);
ABI_OFFSET(CapturedPayloadBinding, geometry_aux, 32);
ABI_OFFSET(CapturedPayloadBinding, validity_flags, 36);
ABI_OFFSET(CapturedPayloadBinding, terrain_cells, 40);
ABI_OFFSET(CapturedPayloadBinding, terrain_work_items, 44);
ABI_OFFSET(CapturedPayloadBinding, terrain_batches, 48);
ABI_OFFSET(CapturedPayloadBinding, terrain_view_input, 52);
ABI_OFFSET(BeginCockpitSceneCommand, capture_serial, 20);
static_assert(sizeof(CaptureContinuationState)==88,"capture continuation ABI");
ABI_OFFSET(CaptureContinuationState, active_target, 4);
ABI_OFFSET(CaptureContinuationState, logical_clip, 8);
ABI_OFFSET(CaptureContinuationState, load_attachment_mask, 28);
ABI_OFFSET(CaptureContinuationState, prior_submitted_timeline, 72);
ABI_OFFSET(CaptureContinuationState, resource_state_snapshot_serial, 80);
static_assert(static_cast<uint32_t>(TraceTableKind::ReproMetadata)==19,
	"trace table values are append-only");
static_assert(static_cast<uint32_t>(TraceTableKind::SegmentStartStates)==20,
	"segment-start table must be appended to trace v1");
static_assert(static_cast<uint32_t>(TraceTableKind::Count)==21,
	"trace table inventory ABI");
static_assert(sizeof(TraceFileHeader)==128,"trace header ABI");
ABI_OFFSET(TraceFileHeader, segment_start_kind, 60);
ABI_OFFSET(TraceFileHeader, segment_start_table_index, 64);
ABI_OFFSET(TraceFileHeader, continuation_state_schema_version, 68);
ABI_OFFSET(TraceFileHeader, first_command_serial, 80);
static_assert(sizeof(TraceSegmentStartRecord)==96,"trace segment-start ABI");
ABI_OFFSET(TraceSegmentStartRecord, schema_version, 0);
ABI_OFFSET(TraceSegmentStartRecord, start_kind, 4);
ABI_OFFSET(TraceSegmentStartRecord, continuation_state, 8);
ABI_OFFSET(CaptureCommand, type, 0); ABI_OFFSET(CaptureCommand, schema_version, 4);
ABI_OFFSET(CaptureCommand, serial, 8); ABI_OFFSET(CaptureCommand, payload, 16);
static_assert(alignof(CaptureCommand) == 8, "capture command alignment");
static_assert(sizeof(TerrainEmitterCell) == 32, "terrain cell ABI");
ABI_OFFSET(TerrainEmitterCell, packed, 0); ABI_OFFSET(TerrainEmitterCell, height, 16);
static_assert(alignof(TerrainEmitterCell) == 16, "terrain cell alignment");
static_assert(sizeof(TerrainWorkItem) == 16, "terrain work ABI");
ABI_OFFSET(TerrainWorkItem, cell_index, 0); ABI_OFFSET(TerrainWorkItem, source_texture, 4);
ABI_OFFSET(TerrainWorkItem, source_flags, 8); ABI_OFFSET(TerrainWorkItem, full_draw_order, 12);
static_assert(alignof(TerrainWorkItem) == 16, "terrain work alignment");
static_assert(sizeof(TerrainBatchInput) == 32, "terrain batch ABI");
ABI_OFFSET(TerrainBatchInput, source_texture, 0); ABI_OFFSET(TerrainBatchInput, texture_layer, 4);
ABI_OFFSET(TerrainBatchInput, first_work_item, 8); ABI_OFFSET(TerrainBatchInput, work_item_count, 12);
ABI_OFFSET(TerrainBatchInput, first_output_vertex, 16); ABI_OFFSET(TerrainBatchInput, output_vertex_capacity, 20);
ABI_OFFSET(TerrainBatchInput, indirect_command_index, 24); ABI_OFFSET(TerrainBatchInput, reserved0, 28);
static_assert(alignof(TerrainBatchInput) == 16, "terrain batch alignment");
static_assert(sizeof(TerrainViewInput) == 112, "terrain view ABI");
ABI_OFFSET(TerrainViewInput, terrain_row0, 0); ABI_OFFSET(TerrainViewInput, terrain_x_step, 16);
ABI_OFFSET(TerrainViewInput, terrain_z_step, 32); ABI_OFFSET(TerrainViewInput, terrain_y_step, 48);
ABI_OFFSET(TerrainViewInput, projection_center_half_size, 64);
ABI_OFFSET(TerrainViewInput, viewport_size_inv_size, 80); ABI_OFFSET(TerrainViewInput, clip_scale, 96);
static_assert(alignof(TerrainViewInput) == 16, "terrain view alignment");
static_assert(sizeof(TerrainIndirectCommand) == 16, "terrain indirect ABI");

static int failures = 0;
static void Check(bool value, const char *message)
{
	if (!value) { fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
static bool Near(float a, float b, float epsilon = 1.0e-7f)
{
	return fabsf(a - b) <= epsilon;
}

static const DeviceLimitRequirement *FindLimit(DeviceLimit limit)
{
	for (size_t i = 0; i < kRequiredDeviceLimitCount; ++i)
		if (kRequiredDeviceLimits[i].limit == limit)
			return &kRequiredDeviceLimits[i];
	return nullptr;
}

static const GraphInputRuleContract *FindGraphRule(GraphNodeId id)
{
	for (size_t i = 0; i < kGraphInputRuleContractCount; ++i)
		if (kGraphInputRuleContract[i].id == id)
			return &kGraphInputRuleContract[i];
	return nullptr;
}

static void TestFrozenTables()
{
	static const char *names[] = {
		"CAP_WORLD", "CAP_DEPTH_LOGICAL", "RES_COLOR", "RES_DEPTH", "RES_VEL",
		"RES_ID", "RES_MASK", "RES_AOCLASS", "SSAA_4_TO_2", "SSAA_2_TO_1",
		"PREP_DEPTH_LOGICAL", "AO_DEPTH", "AO_RAW", "AO_BLUR_X", "AO_BLUR_Y",
		"AO_TEMPORAL", "AO_SUPPRESS", "AO_APPLY", "AO_DEFERRED_COMPOSITE",
		"BLOOM_THRESHOLD", "BLOOM_DOWN_n", "BLOOM_UP_n", "NORMAL_COMPOSITE",
		"NORMAL_BLIT", "MOTION_NORMAL", "MOTION_DEBUG_NORMAL", "NORMAL_UI",
		"COCKPIT_LINEAR_COPY", "MOTION_COCKPIT_PRE", "MOTION_DEBUG_COCKPIT_PRE",
		"COCKPIT_UI_PRE", "COCKPIT_SCENE", "POST_ALPHA_CLEAR", "COCKPIT_RESOLVE",
		"BLOOM_DEFERRED_*", "COCKPIT_OVER", "COCKPIT_BLOOM_GAMMA",
		"COCKPIT_GAMMA_ONLY", "COCKPIT_UI_POST", "PRESENT"
	};
	static const GraphPredicate predicates[] = {
		GraphPredicate::ExactCaptureCall, GraphPredicate::LatePostAtCapture,
		GraphPredicate::MsaaColorConsumer, GraphPredicate::MsaaDepthConsumer,
		GraphPredicate::MsaaVelocityConsumer, GraphPredicate::MsaaObjectIdConsumer,
		GraphPredicate::MsaaProtectionConsumer, GraphPredicate::MsaaAoConsumer,
		GraphPredicate::SsaaFour, GraphPredicate::SsaaAtLeastTwo,
		GraphPredicate::LatePost, GraphPredicate::Gtao, GraphPredicate::Gtao,
		GraphPredicate::GtaoWithBlur, GraphPredicate::GtaoWithBlur,
		GraphPredicate::GtaoTemporalOrDebug, GraphPredicate::Gtao,
		GraphPredicate::Gtao, GraphPredicate::GtaoDeferred, GraphPredicate::Bloom,
		GraphPredicate::BloomPyramidLevel, GraphPredicate::BloomMergeLevel,
		GraphPredicate::NormalBranchBloom, GraphPredicate::NormalBranchNoBloom,
		GraphPredicate::NormalBranchMotion, GraphPredicate::NormalBranchMotionDebug,
		GraphPredicate::NormalBranchUi, GraphPredicate::CockpitBranch,
		GraphPredicate::CockpitBranchMotion, GraphPredicate::CockpitBranchMotionDebug,
		GraphPredicate::CockpitUiPre, GraphPredicate::CockpitSceneActive,
		GraphPredicate::CockpitSceneActive, GraphPredicate::CockpitSceneActive,
		GraphPredicate::CockpitDeferredBloom, GraphPredicate::CockpitSceneActive,
		GraphPredicate::CockpitBloomResult, GraphPredicate::CockpitNoBloomResult,
		GraphPredicate::CockpitUiPost, GraphPredicate::Present
	};
	Check(kFrozenGraphManifestCount == 40, "graph manifest count");
	Check(kFrozenGraphNodeContractCount == kFrozenGraphManifestCount, "graph dependency count");
	for (size_t i = 0; i < kFrozenGraphManifestCount; ++i)
	{
		Check(static_cast<size_t>(kFrozenGraphManifest[i].id) == i, "graph order");
		Check(strcmp(kFrozenGraphManifest[i].symbolic_name, names[i]) == 0, "graph symbolic name");
		Check(kFrozenGraphNodeContract[i].id == kFrozenGraphManifest[i].id, "graph schemas agree");
		Check(kFrozenGraphNodeContract[i].predicate == predicates[i], "graph predicate");
	}
	Check(kSceneAttachmentContractCount == 6, "five color attachments plus depth");
	const RenderFormat attachment_formats[] = { RenderFormat::R8G8B8A8Unorm,
		RenderFormat::R16G16Sfloat, RenderFormat::R8G8Unorm, RenderFormat::R8Unorm,
		RenderFormat::R32Uint, RenderFormat::D32Sfloat };
	const ResolveRule resolve_rules[] = { ResolveRule::AverageSamples,
		ResolveRule::AverageSamples, ResolveRule::AverageSamples,
		ResolveRule::AverageSamples, ResolveRule::SampleZero, ResolveRule::SampleZero };
	const AttachmentBlendMode attachment_blends[] = {
		AttachmentBlendMode::LegacyColorByDraw, AttachmentBlendMode::Replace,
		AttachmentBlendMode::ComponentMax, AttachmentBlendMode::Replace,
		AttachmentBlendMode::Replace, AttachmentBlendMode::Depth };
	for (size_t i = 0; i < 6; ++i)
	{
		Check(kSceneAttachmentContract[i].format == attachment_formats[i],
			"exact attachment format");
		Check(kSceneAttachmentContract[i].resolve_rule == resolve_rules[i],
			"exact attachment resolve");
		Check(kSceneAttachmentContract[i].blend_mode == attachment_blends[i],
			"exact attachment blend mode");
	}
	Check(kSceneAttachmentContract[0].clear_float[3] == 1.0f &&
		kSceneAttachmentContract[5].clear_float[0] == 1.0f,
		"scene color/depth clear values");
	Check(kSceneAttachmentContract[4].format == RenderFormat::R32Uint, "integer object ID format");
	Check(kSceneAttachmentContract[5].format == RenderFormat::D32Sfloat, "depth format");
	Check(kBlendClassContractCount == 4, "blend class count");
	Check(kBlendClassContract[0].blend_enabled == 0 &&
		kBlendClassContract[1].source_rgb == BlendFactorContract::SourceAlpha &&
		kBlendClassContract[2].destination_rgb == BlendFactorContract::One &&
		kBlendClassContract[3].source_rgb == BlendFactorContract::DestinationColor,
		"four exact legacy blend classes");
	Check(kRequiredDeviceFeatureCount == static_cast<size_t>(RequiredDeviceFeature::Count), "device feature schema");
	Check(kRequiredDeviceLimitCount == static_cast<size_t>(DeviceLimit::Count), "device limit schema");
	Check(FindLimit(DeviceLimit::MaxColorAttachments)->minimum == 5,
		"five color attachment limit");
	Check(FindLimit(DeviceLimit::MaxBoundDescriptorSets)->minimum == 3,
		"three world descriptor sets");
	Check(FindLimit(DeviceLimit::MaxPerStageResources)->validation ==
		LimitValidation::DerivedFromReflectedAbi, "reflected total per-stage resources");
	Check(FindLimit(DeviceLimit::MaxPerStageDescriptorUniformBuffers)->minimum == 1 &&
		FindLimit(DeviceLimit::MaxDescriptorSetUniformBuffersDynamic)->minimum == 1,
		"world uniform descriptor totals");
	Check(FindLimit(DeviceLimit::MaxComputeWorkGroupInvocations)->minimum == 256 &&
		FindLimit(DeviceLimit::MaxComputeSharedMemorySize)->minimum == 16*1024,
		"T2 compute limits");
	Check(kRequiredFormatCount == static_cast<size_t>(FormatSemantic::Count), "format schema");
	Check(kWorldDescriptorBindingCount == 11, "world descriptor ABI entries");
	for (size_t i = 0; i < kWorldDescriptorBindingCount; ++i)
	{
		const WorldDescriptorBindingContract &binding = kWorldDescriptorBindings[i];
		Check(binding.set <= 2, "world descriptor set range");
		Check(binding.stage_mask == (kStageVertex | kStageFragment | kStageCompute),
			"world descriptors visible to all world stages");
	}
	Check(kWorldDescriptorBindings[0].kind == DescriptorKind::DynamicUniformBuffer &&
		kWorldDescriptorBindings[0].count == 1, "set zero frame globals");
	Check(kWorldDescriptorBindings[1].kind == DescriptorKind::CombinedFloat2D &&
		kWorldDescriptorBindings[1].page_tier_count == 1 &&
		kWorldDescriptorBindings[2].kind == DescriptorKind::CombinedFloat2DArray &&
		kWorldDescriptorBindings[2].count == 8, "set one combined image page");
	Check(kSamplerContractCount == static_cast<size_t>(SamplerSemantic::Count), "sampler matrix");
	Check(kHistoryEventContractCount == static_cast<size_t>(HistoryEvent::Count), "history contract");
	Check(kTextureMappingInvalidationContractCount ==
		static_cast<size_t>(TextureMappingInvalidationReason::Count),
		"texture invalidation contract");
	Check(kDescriptorPageContract.float_image_array_capacity == 8,
		"descriptor array page capacity");
	Check(kDescriptorPageContract.diagnostic_fill_required == 1 &&
		kDescriptorPageContract.immutable_after_recording == 1 &&
		kDescriptorPageContract.timeline_retired_pool == 1 &&
		kDescriptorPageContract.update_after_bind_allowed == 0 &&
		kDescriptorPageContract.variable_descriptor_count_allowed == 0,
		"immutable descriptor page lifetime");
	Check(sizeof(kDependencyEdgeContract) / sizeof(kDependencyEdgeContract[0]) ==
		static_cast<size_t>(DependencyEdge::Count), "resource dependency inventory");
	Check(kValidationContractCount == static_cast<size_t>(ValidationRequirement::Count), "validation contract");
	Check(kCaptureArtifactContractCount == 49, "attachment capture inventory");
	Check(kGraphArtifactContractCount==static_cast<size_t>(GraphNodeId::Count),
		"one comparison artifact contract per graph node");
	Check(kCorpusCoverageContractCount == 40, "parity corpus inventory");
	uint32_t covered_groups=0;
	for(size_t i=0;i<kCaptureArtifactContractCount;++i)
		for(size_t j=i+1;j<kCaptureArtifactContractCount;++j)
			Check(strcmp(kCaptureArtifactContract[i].name_pattern,
				kCaptureArtifactContract[j].name_pattern)!=0,"unique capture artifact pattern");
	for(size_t i=0;i<kGraphArtifactContractCount;++i)
	{
		Check(static_cast<size_t>(kGraphArtifactContract[i].node)==i &&
			kGraphArtifactContract[i].capture_every_invocation==1 &&
			strchr(kGraphArtifactContract[i].artifact.name_pattern,'*')==nullptr,
			"graph artifacts are complete, ordered, and invocation-explicit");
		for(size_t j=i+1;j<kGraphArtifactContractCount;++j)
			Check(strcmp(kGraphArtifactContract[i].artifact.name_pattern,
				kGraphArtifactContract[j].artifact.name_pattern)!=0,
				"graph artifact semantics are one-to-one");
	}
	for(size_t i=0;i<kCorpusCoverageContractCount;++i)
	{
		covered_groups |= 1u << static_cast<uint32_t>(kCorpusCoverageContract[i].group);
		for(size_t j=i+1;j<kCorpusCoverageContractCount;++j)
			Check(strcmp(kCorpusCoverageContract[i].id,kCorpusCoverageContract[j].id)!=0,
				"unique corpus coverage ID");
	}
	Check(covered_groups == (1u << static_cast<uint32_t>(CorpusGroup::Count))-1,
		"every corpus group represented");
	Check(kArtifactNameGrammarContract.includes_case_id == 1 &&
		kArtifactNameGrammarContract.includes_graph_node_and_invocation == 1 &&
		kArtifactNameGrammarContract.forbids_overwrite == 1,
		"collision-free artifact name grammar");
	Check(kInterpolationContractCount == static_cast<size_t>(InterpolationSemantic::Count),
		"interpolation qualifier inventory");
	Check(kEntryPointCoordinateContractCount == static_cast<size_t>(LegacyEntryPoint::Count),
		"entry-point coordinate inventory");
	Check(kSurfaceFormatPreferences[0].format == SurfacePixelFormat::B8G8R8A8Unorm &&
		kSurfaceFormatPreferences[1].format == SurfacePixelFormat::R8G8B8A8Unorm,
		"safe WSI format preference");
	Check(kPresentModePreference[0] == PresentModeContract::Immediate &&
		kPresentModePreference[1] == PresentModeContract::Mailbox &&
		kPresentModePreference[2] == PresentModeContract::Fifo,
		"present mode parity order");
	Check(kSwapchainOwnershipContract.cpu_frame_contexts == 2 &&
		kSwapchainOwnershipContract.render_finished_semaphore_per_swapchain_image == 1 &&
		kSwapchainOwnershipContract.device_wait_idle_in_normal_recreation == 0,
		"WSI ownership policy");
	Check(kStartFrameResetContractCount == 3, "three StartFrame routes");
	Check(kStartFrameResetContract[0].selected_attachments ==
		(kWriteColor | kWriteProtectionMask | kWriteAoClass),
		"ordinary StartFrame default draw buffers");
	Check((kStartFrameResetContract[1].reset_bits &
		(kFlushFontsBeforeRoute | kColorAttachmentOnly | kDisableMsaa)) ==
		(kFlushFontsBeforeRoute | kColorAttachmentOnly | kDisableMsaa),
		"post-present StartFrame routing");
	Check((kStartFrameResetContract[2].reset_bits &
		kUnconditionalTransparentColorAndDepthClear) != 0,
		"cockpit StartFrame unconditional clear");
	const uint32_t expected_history_actions[] = { kHistoryInvalidate, kHistoryInvalidate,
		kHistoryInvalidate, kHistoryInvalidate, kHistoryInvalidate, kHistoryInvalidate,
		kHistoryFreeze|kHistoryDoNotAdvance, kHistoryDoNotAdvance, kHistoryAdvance };
	for (size_t i=0;i<kHistoryEventContractCount;++i)
		Check(kHistoryEventContract[i].actions==expected_history_actions[i],
			"exact history action");
}

static void TestGraphSelections()
{
	GraphSourceSelectionContext context = {};
	context.msaa_samples = 1;
	context.ssaa_factor = 1;
	Check(SelectGraphInputSource(GraphInputSemantic::SceneColorAfterMsaa, context) ==
		GraphResource::SceneColor, "single-sample color source");
	Check(SelectGraphInputSource(GraphInputSemantic::VelocitySource, context) ==
		GraphResource::SceneVelocity, "single-sample velocity source");
	Check(SelectGraphInputSource(GraphInputSemantic::ObjectIdSource, context) ==
		GraphResource::SceneObjectId, "single-sample ID source");
	context.msaa_samples = 4;
	Check(SelectGraphInputSource(GraphInputSemantic::SceneColorAfterMsaa, context) ==
		GraphResource::ResolvedColor, "MSAA color source");
	Check(SelectGraphInputSource(GraphInputSemantic::VelocitySource, context) ==
		GraphResource::ResolvedVelocity, "MSAA velocity source");
	context.ssaa_factor = 4;
	Check(SelectGraphInputSource(GraphInputSemantic::SsaaTwoToOneColor, context) ==
		GraphResource::SsaaIntermediate2x, "SSAA4 second-stage source priority");
	context.captured_logical_depth_valid = 1;
	Check(SelectGraphInputSource(GraphInputSemantic::PostDepthSource, context) ==
		GraphResource::CapturedWorldDepth, "captured depth priority");
	context.gtao_applied_output_valid = 1;
	Check(SelectGraphInputSource(GraphInputSemantic::FinalAuthoredColor, context) ==
		GraphResource::GtaoApplied, "GTAO-applied color priority");
	context.gtao_deferred_output_valid = 1;
	Check(SelectGraphInputSource(GraphInputSemantic::FinalAuthoredColor, context) ==
		GraphResource::GtaoDeferredComposite, "deferred color priority");
	context.gtao_blur_output_valid = 0;
	context.gtao_temporal_output_valid = 0;
	Check(SelectGraphInputSource(GraphInputSemantic::AoPreTemporalSource, context) ==
		GraphResource::GtaoRaw, "unblurred AO source");
	context.gtao_temporal_output_valid = 1;
	Check(SelectGraphInputSource(GraphInputSemantic::AoPreTemporalSource, context) ==
		GraphResource::GtaoRaw, "temporal node cannot read its own output");
	Check(SelectGraphInputSource(GraphInputSemantic::AoFinalSource, context) ==
		GraphResource::GtaoCurrent, "temporal AO source");

	GraphSourceSelectionContext contexts[3] = {};
	contexts[0].msaa_samples=1; contexts[0].ssaa_factor=1;
	contexts[1].msaa_samples=4; contexts[1].ssaa_factor=2;
	contexts[2].msaa_samples=4; contexts[2].ssaa_factor=4;
	contexts[2].captured_logical_depth_valid=1;
	contexts[2].gtao_blur_output_valid=1;
	contexts[2].gtao_history_required=1;
	contexts[2].gtao_applied_output_valid=1;
	contexts[2].gtao_deferred_output_valid=1;
	GraphResourceMask produced =
		GraphResourceBit(GraphResource::SceneColor) |
		GraphResourceBit(GraphResource::SceneDepth) |
		GraphResourceBit(GraphResource::SceneVelocity) |
		GraphResourceBit(GraphResource::SceneObjectId) |
		GraphResourceBit(GraphResource::SceneProtectionMask) |
		GraphResourceBit(GraphResource::SceneAoClass) |
		GraphResourceBit(GraphResource::CapturedMatrices) |
		GraphResourceBit(GraphResource::GtaoHistoryPrevious) |
		GraphResourceBit(GraphResource::GtaoNoise);
	for(size_t n=0;n<kFrozenGraphNodeContractCount;++n)
	{
		const GraphNodeContract &node=kFrozenGraphNodeContract[n];
		const GraphInputRuleContract *rule=FindGraphRule(node.id);
		const GraphResourceMask required=rule?rule->required_inputs:node.inputs;
		Check((required&~produced)==0,
			"every direct required graph input has an earlier producer or external source");
		if(rule)
			for(size_t c=0;c<3;++c)
				for(size_t s=0;s<4;++s)
				{
					const GraphResource selected=SelectGraphInputSource(
						rule->selected_inputs[s],contexts[c]);
					if(selected!=GraphResource::Count)
						Check((produced&GraphResourceBit(selected))!=0,
							"selected graph input has an earlier producer or external source");
				}
		produced|=node.outputs;
	}
	for (size_t r = 0; r < kGraphInputRuleContractCount; ++r)
	{
		const GraphInputRuleContract &rule = kGraphInputRuleContract[r];
		const GraphNodeContract &node = kFrozenGraphNodeContract[static_cast<size_t>(rule.id)];
		Check((rule.required_inputs & ~node.inputs) == 0, "graph required inputs declared");
		Check((rule.optional_inputs & ~node.inputs) == 0, "graph optional inputs declared");
		for (size_t c = 0; c < 3; ++c)
			for (size_t s = 0; s < 4; ++s)
			{
				const GraphResource selected =
					SelectGraphInputSource(rule.selected_inputs[s], contexts[c]);
				if (selected != GraphResource::Count)
					Check((node.inputs & GraphResourceBit(selected)) != 0,
						"selected graph source declared by node");
			}
	}
	Check(FindGraphRule(GraphNodeId::MotionNormal) != nullptr &&
		FindGraphRule(GraphNodeId::MotionCockpitPre) != nullptr,
		"both motion branches have explicit source selection");
	Check(kInsertedGraphNodeContractCount == 1 &&
		(kInsertedGraphNodeContract[0].outputs &
		 GraphResourceBit(GraphResource::SoftDepthSnapshot)) != 0,
		"ordered soft-depth graph insertion");
}

static void TestExecutableGraphEvaluation()
{
	GraphEvaluationContext normal = {};
	normal.capture_call_count = 2;
	normal.world_color_capture_call_count = 1;
	normal.depth_capture_call_count = 2;
	normal.post_frame_active = 1;
	normal.present_count = 1;
	normal.msaa_samples = 4;
	normal.ssaa_factor = 4;
	normal.resolve_consumer_mask = kTargetAttachmentAll;
	normal.late_post_active = 1;
	normal.gtao_enabled = 1;
	normal.gtao_blur_radius = 2;
	normal.gtao_temporal_active = 1;
	normal.gtao_debug_active = 1;
	normal.gtao_deferred_active = 1;
	normal.bloom_enabled = 1;
	normal.bloom_source_width = 1920;
	normal.bloom_source_height = 1080;
	normal.motion_consumer_active = 1;
	normal.motion_debug_active = 1;
	normal.normal_ui_span_count = 2;
	Check(ValidateGraphEvaluationContext(normal) == 0,
		"normal executable graph context validates");
	Check(ComputeBloomPyramidLevelCount(15, 1080) == 0 &&
		ComputeBloomPyramidLevelCount(16, 16) == 1 &&
		ComputeBloomPyramidLevelCount(1920, 1080) == 7 &&
		ComputeBloomPyramidLevelCount(UINT32_MAX, UINT32_MAX) == 8,
		"exact bounded GL4 bloom pyramid count");
	Check(EvaluateGraphNodeInvocationCount(GraphNodeId::CapWorld, normal) == 2 &&
		EvaluateGraphNodeInvocationCount(GraphNodeId::CapDepthLogical, normal) == 2,
		"capture graph nodes retain exact call counts");
	Check(EvaluateGraphNodeInvocationCount(GraphNodeId::BloomThreshold, normal) == 1 &&
		EvaluateGraphNodeInvocationCount(GraphNodeId::BloomDown, normal) == 6 &&
		EvaluateGraphNodeInvocationCount(GraphNodeId::BloomUp, normal) == 6,
		"normal bloom wildcard nodes have exact invocation counts");
	Check(EvaluateCompilerGraphPhaseInvocationCount(
		kCompilerGraphPhaseContract[2], normal) == 1 &&
		EvaluateCompilerGraphPhaseInvocationCount(
		kCompilerGraphPhaseContract[3], normal) == 1 &&
		EvaluateCompilerGraphPhaseInvocationCount(
		kCompilerGraphPhaseContract[4], normal) == 1 &&
		EvaluateCompilerGraphPhaseInvocationCount(
		kCompilerGraphPhaseContract[5], normal) == 2,
		"CAP_WORLD resolves MSAA, quantizes 4-to-2-to-1, then alpha-clears per call");
	Check(EvaluateGraphNodeInvocationCount(GraphNodeId::NormalComposite, normal) == 1 &&
		EvaluateGraphNodeInvocationCount(GraphNodeId::NormalBlit, normal) == 0 &&
		EvaluateGraphNodeInvocationCount(GraphNodeId::NormalUi, normal) == 2,
		"normal branch is mutually exclusive and UI spans remain ordered");
	Check((EvaluateGraphNodeOutputs(GraphNodeId::CapWorld, normal) &
		GraphResourceBit(GraphResource::CapturedWorldColor)) != 0,
		"capture-world color is emitted only when requested by a consumer");
	Check((EvaluateGraphNodeOutputs(GraphNodeId::MotionDebugNormal, normal) &
		GraphResourceBit(GraphResource::VelocityDebugReadback)) == 0,
		"motion debug preview does not invent a readback output");
	normal.motion_debug_readback_active = 1;
	Check((EvaluateGraphNodeOutputs(GraphNodeId::MotionDebugNormal, normal) &
		GraphResourceBit(GraphResource::VelocityDebugReadback)) != 0,
		"requested motion debug readback is an explicit conditional output");

	GraphEvaluationContext debug_only_temporal = normal;
	debug_only_temporal.gtao_temporal_active = 0;
	Check((EvaluateGraphNodeOutputs(GraphNodeId::AoTemporal,
		debug_only_temporal) & GraphResourceBit(GraphResource::GtaoHistoryNext)) == 0 &&
		(EvaluateGraphNodeOutputs(GraphNodeId::AoTemporal,
		debug_only_temporal) & GraphResourceBit(GraphResource::GtaoCurrent)) != 0,
		"AO debug invocation does not advance temporal history");

	GraphEvaluationContext authored = {};
	authored.capture_call_count = 1;
	authored.post_frame_active = 1;
	authored.present_count = 1;
	authored.msaa_samples = 1;
	authored.ssaa_factor = 1;
	Check(ValidateGraphEvaluationContext(authored) == 0 &&
		EvaluateGraphNodeInvocationCount(GraphNodeId::NormalBlit, authored) == 1 &&
		EvaluateGraphNodeInvocationCount(GraphNodeId::PrepareDepthLogical, authored) == 0 &&
		EvaluateGraphNodeOutputs(GraphNodeId::CapWorld, authored) ==
			GraphResourceBit(GraphResource::SceneColor),
		"authored-only graph clears capture alpha without allocating captured color/depth");

	GraphEvaluationContext cockpit = normal;
	cockpit.cockpit_deferral_active = 1;
	cockpit.cockpit_frame_count = 1;
	cockpit.normal_ui_span_count = 0;
	cockpit.cockpit_ui_pre_span_count = 2;
	cockpit.cockpit_ui_post_span_count = 3;
	Check(ValidateGraphEvaluationContext(cockpit) == 0 &&
		EvaluateGraphNodeInvocationCount(GraphNodeId::BloomThreshold, cockpit) == 0 &&
		EvaluateGraphNodeInvocationCount(GraphNodeId::BloomDeferred, cockpit) == 13 &&
		EvaluateGraphNodeInvocationCount(GraphNodeId::CockpitBloomGamma, cockpit) == 1 &&
		EvaluateGraphNodeInvocationCount(GraphNodeId::CockpitGammaOnly, cockpit) == 0,
		"cockpit graph owns the complete deferred bloom chain");
	Check(EvaluateGraphNodeInvocationCount(GraphNodeId::CockpitResolve, cockpit) == 3 &&
		EvaluateCompilerGraphPhaseInvocationCount(
			kCompilerGraphPhaseContract[8], cockpit) == 1 &&
		EvaluateCompilerGraphPhaseInvocationCount(
			kCompilerGraphPhaseContract[9], cockpit) == 1 &&
		EvaluateCompilerGraphPhaseInvocationCount(
			kCompilerGraphPhaseContract[10], cockpit) == 1 &&
		EvaluateCompilerGraphPhaseInvocationCount(
			kCompilerGraphPhaseContract[11], cockpit) == 1 &&
		EvaluateCompilerGraphPhaseInvocationCount(
			kCompilerGraphPhaseContract[12], cockpit) == 1 &&
		EvaluateCompilerGraphPhaseInvocationCount(
			kCompilerGraphPhaseContract[13], cockpit) == 6 &&
		EvaluateCompilerGraphPhaseInvocationCount(
			kCompilerGraphPhaseContract[14], cockpit) == 6,
		"cockpit resolve, alpha alias, and deferred bloom phases have exact counts");
	Check((EvaluateGraphNodeOutputs(GraphNodeId::CockpitResolve, cockpit) &
		GraphResourceBit(GraphResource::CockpitAlpha)) != 0,
		"cockpit alpha is materialized for deferred bloom");
	cockpit.bloom_source_width = 15;
	Check(EvaluateGraphNodeInvocationCount(GraphNodeId::BloomDeferred, cockpit) == 0 &&
		EvaluateGraphNodeInvocationCount(GraphNodeId::CockpitBloomGamma, cockpit) == 0 &&
		EvaluateGraphNodeInvocationCount(GraphNodeId::CockpitGammaOnly, cockpit) == 1 &&
		(EvaluateGraphNodeOutputs(GraphNodeId::CockpitResolve, cockpit) &
		 GraphResourceBit(GraphResource::CockpitAlpha)) == 0,
		"too-small cockpit bloom source selects gamma-only and omits alpha side resource");

	GraphEvaluationContext invalid = authored;
	invalid.msaa_samples = 3;
	invalid.ssaa_factor = 3;
	invalid.gtao_temporal_active = 2;
	invalid.world_color_capture_call_count = 2;
	invalid.resolve_consumer_mask = kTargetAttachmentAll | (1u << 20);
	Check((ValidateGraphEvaluationContext(invalid) &
		(kGraphEvaluationInvalidBoolean | kGraphEvaluationInvalidSamples |
		 kGraphEvaluationInvalidResolveMask | kGraphEvaluationInvalidCaptureCounts)) ==
		(kGraphEvaluationInvalidBoolean | kGraphEvaluationInvalidSamples |
		 kGraphEvaluationInvalidResolveMask | kGraphEvaluationInvalidCaptureCounts),
		"malformed graph contexts fail all applicable normalization gates");

	const GraphEvaluationContext contexts[] = { normal, authored, cockpit };
	for (size_t c = 0; c < sizeof(contexts) / sizeof(contexts[0]); ++c)
		for (size_t n = 0; n < static_cast<size_t>(GraphNodeId::Count); ++n)
		{
			const GraphNodeId node = static_cast<GraphNodeId>(n);
			const uint32_t invocations =
				EvaluateGraphNodeInvocationCount(node, contexts[c]);
			const GraphResourceMask outputs =
				EvaluateGraphNodeOutputs(node, contexts[c]);
			Check((invocations != 0) == EvaluateGraphPredicate(
				kFrozenGraphNodeContract[n].predicate, contexts[c]),
				"node invocation and predicate evaluations agree");
			Check((outputs & ~kFrozenGraphNodeContract[n].outputs) == 0 &&
				(invocations != 0 || outputs == 0),
				"active graph outputs are an explicit subset of the frozen inventory");
		}
}

static void TestStickyStateRules()
{
	AlphaTypeTransitionInput alpha = {};
	alpha.current_type = static_cast<uint32_t>(LegacyAlphaType::Always);
	alpha.requested_type = alpha.current_type;
	alpha.current_alpha_byte = 7;
	AlphaTypeTransitionDecision decision = EvaluateAlphaTypeTransition(alpha);
	Check(decision.redundant_early_return == 1 && decision.resulting_alpha_byte == 7 &&
		decision.resulting_alpha_multiplier == 255 &&
		decision.configure_auxiliary_blend_masks == 0,
		"redundant AT_ALWAYS preserves sticky alpha byte");
	alpha.current_type = static_cast<uint32_t>(LegacyAlphaType::Constant);
	decision = EvaluateAlphaTypeTransition(alpha);
	Check(decision.resulting_alpha_byte == 255 && decision.blend_class == BlendClass::Opaque,
		"actual AT_ALWAYS transition forces alpha 255");
	alpha.requested_type = static_cast<uint32_t>(LegacyAlphaType::Specular);
	alpha.current_alpha_byte = 19; alpha.active_texture_unit = 2;
	decision = EvaluateAlphaTypeTransition(alpha);
	Check(decision.resulting_alpha_byte == 19 && decision.resulting_alpha_multiplier == 255 &&
		decision.force_texture_quality_2 == 1 &&
		decision.force_texture_type_perspective == 1 &&
		decision.select_texture_unit_zero == 1 && decision.luminance_post_mask == 0,
		"AT_SPECULAR exact sticky transition");

	LiteralAttachmentMasks masks = {{ kChannelRgba, kChannelRgba, kChannelRgba,
		kChannelRgba, kChannelRed }};
	PostMaskOnlyTransitionDecision post = EvaluatePostMaskOnlyTransition(0, 1, masks);
	Check(post.flush_fonts_before_compare == 1 && post.redundant_after_flush == 0 &&
		post.masks.rgba[0] == 0 && post.masks.rgba[1] == 0 &&
		post.masks.rgba[2] == kChannelRgba && post.masks.rgba[3] == 0 &&
		post.masks.rgba[4] == kChannelRed, "post-mask literal channel transition");
	post = EvaluatePostMaskOnlyTransition(1, 1, post.masks);
	Check(post.flush_fonts_before_compare == 1 && post.redundant_after_flush == 1,
		"redundant post-mask still flushes fonts");
	LiteralAttachmentMasks configured = ApplyConfigurePostMaskBlend(post.masks);
	Check(configured.rgba[0] == 0 && configured.rgba[2] == kChannelRgba &&
		configured.rgba[1] == kChannelRgba && configured.rgba[3] == kChannelRgba &&
		configured.rgba[4] == kChannelRgba, "auxiliary blend literal masks");
	Check(kSetterTransitionContractCount == static_cast<size_t>(LegacySetterFamily::Count),
		"setter transition inventory");
	Check(kSetterTransitionContract[static_cast<size_t>(LegacySetterFamily::PostMaskOnly)].
		flush_fonts_before_compare == 1, "post-mask precomparison flush policy");
	Check(kSetterTransitionContract[static_cast<size_t>(LegacySetterFamily::FogState)].
		truthy_marks_protection_dirty_before_compare == 1,
		"redundant fog-enable side effect");

	Check(kMotionAlphaPredicateContractCount == kAlphaTypeTransitionContractCount,
		"motion alpha predicate covers every handled sticky alpha type");
	for (size_t i = 0; i < kMotionAlphaPredicateContractCount; ++i)
	{
		Check(kMotionAlphaPredicateContract[i].alpha_type ==
			kAlphaTypeTransitionContract[i].type,
			"motion alpha predicate order matches alpha transition inventory");
		Check(FindMotionAlphaPredicateContract(static_cast<uint32_t>(
			kMotionAlphaPredicateContract[i].alpha_type)) ==
			&kMotionAlphaPredicateContract[i],
			"motion alpha predicate lookup");
	}

	// Independent source-equivalent oracle.  Enumerating all Boolean inputs for
	// every handled alpha type prevents a shortened motion rule from passing by
	// covering only the familiar opaque/object cases.
	auto expected_motion_alpha = [](uint32_t alpha_type, bool alpha255,
		bool opaque_poly_or_cockpit, bool object_attached, bool flat_texture)
	{
		switch (alpha_type)
		{
		case static_cast<uint32_t>(LegacyAlphaType::Always):
		case static_cast<uint32_t>(LegacyAlphaType::Texture):
			return true;
		case static_cast<uint32_t>(LegacyAlphaType::Constant):
		case static_cast<uint32_t>(LegacyAlphaType::ConstantTexture):
			return alpha255 || object_attached;
		case static_cast<uint32_t>(LegacyAlphaType::TextureVertex):
			return opaque_poly_or_cockpit || object_attached;
		case static_cast<uint32_t>(LegacyAlphaType::ConstantVertex):
			return (opaque_poly_or_cockpit && flat_texture) || object_attached;
		case static_cast<uint32_t>(LegacyAlphaType::Vertex):
		case static_cast<uint32_t>(LegacyAlphaType::SaturateTexture):
		case static_cast<uint32_t>(LegacyAlphaType::SaturateVertex):
		case static_cast<uint32_t>(LegacyAlphaType::SaturateConstantVertex):
		case static_cast<uint32_t>(LegacyAlphaType::SaturateTextureVertex):
			return object_attached;
		default:
			return false;
		}
	};

	for (size_t alpha_index = 0;
		alpha_index < kMotionAlphaPredicateContractCount; ++alpha_index)
	{
		const uint32_t alpha_type = static_cast<uint32_t>(
			kMotionAlphaPredicateContract[alpha_index].alpha_type);
		for (uint32_t bits = 0; bits < (1u << 14); ++bits)
		{
			MotionWritePredicateInput motion = {};
			motion.suppression_depth = (bits >> 0) & 1u;
			motion.pixel_target_enabled = (bits >> 1) & 1u;
			motion.frozen = (bits >> 2) & 1u;
			motion.post_present_pending = (bits >> 3) & 1u;
			motion.zbuffer_enabled = (bits >> 4) & 1u;
			motion.motion_object_active = (bits >> 5) & 1u;
			motion.capture_locked = (bits >> 6) & 1u;
			motion.cockpit_draw = (bits >> 7) & 1u;
			const bool polyobject = ((bits >> 8) & 1u) != 0;
			motion.ao_class = polyobject ? kLegacyAoClassPolyobject : 0;
			const bool alpha255 = ((bits >> 9) & 1u) != 0;
			motion.alpha_value = alpha255 ? 255 : 37;
			motion.force_capture = (bits >> 10) & 1u;
			const bool flat_texture = ((bits >> 11) & 1u) != 0;
			motion.texture_type = flat_texture ? kLegacyTextureTypeFlat : 2;
			motion.motion_object_id = ((bits >> 12) & 1u) != 0 ? 41 : 0;
			motion.cockpit_motion_scope_active = (bits >> 13) & 1u;
			motion.alpha_type = alpha_type;

			const bool opaque_poly_or_cockpit =
				(polyobject || motion.cockpit_draw != 0) && alpha255;
			const bool object_attached = motion.force_capture != 0 ||
				(polyobject && motion.motion_object_active != 0);
			const bool alpha_eligible = expected_motion_alpha(alpha_type,
				alpha255, opaque_poly_or_cockpit, object_attached, flat_texture);
			uint32_t expected_blocks = 0;
			if (motion.suppression_depth > 0)
				expected_blocks |= kMotionBlockedBySuppression;
			if (motion.pixel_target_enabled == 0)
				expected_blocks |= kMotionBlockedByTarget;
			if (motion.frozen != 0)
				expected_blocks |= kMotionBlockedByFrozenHistory;
			if (motion.post_present_pending != 0)
				expected_blocks |= kMotionBlockedByPostPresent;
			if (motion.zbuffer_enabled == 0)
				expected_blocks |= kMotionBlockedByDepthTest;
			if (motion.motion_object_active == 0)
				expected_blocks |= kMotionBlockedByInactiveObject;
			if (motion.capture_locked != 0 && motion.cockpit_draw == 0)
				expected_blocks |= kMotionBlockedByCaptureLock;
			if (!alpha_eligible)
				expected_blocks |= kMotionBlockedByAlphaRule;
			const bool expected_write = expected_blocks == 0;
			const bool expected_id = expected_write && motion.motion_object_id != 0 &&
				motion.cockpit_motion_scope_active == 0;
			const bool expected_late_override = motion.pixel_target_enabled != 0 &&
				motion.zbuffer_enabled != 0 && motion.capture_locked != 0 &&
				motion.cockpit_draw != 0;

			MotionWritePredicateDecision motion_decision =
				EvaluateMotionWritePredicate(motion);
			Check(motion_decision.block_bits == expected_blocks &&
				(motion_decision.motion_write != 0) == expected_write &&
				(motion_decision.object_id_write != 0) == expected_id &&
				(motion_decision.late_cockpit_draw_buffer_override != 0) ==
					expected_late_override,
				"exhaustive GL4 motion/object-ID predicate");

			// Every predicate result routed from the ordinary scene literal state
			// must land in the finite pipeline matrix.
			MrtDrawRoutingInput routing = {};
			routing.literal_state = DefaultSceneMrtState();
			routing.draw_kind = MrtDrawKind::Polygon;
			routing.target = RenderTargetClass::Scene;
			routing.drawing_to_scene_framebuffer = 1;
			routing.pixel_motion_mode_enabled = motion.pixel_target_enabled;
			routing.zbuffer_enabled = motion.zbuffer_enabled;
			routing.capture_locked = motion.capture_locked;
			routing.cockpit_draw = motion.cockpit_draw;
			routing.motion_write = motion_decision.motion_write;
			routing.object_id_write = motion_decision.object_id_write;
			MrtDrawRoutingDecision routed = EvaluateMrtDrawRouting(routing);
			Check(routed.legal_write_mask == 1 &&
				IsLegalMrtWriteMask(routed.logical_write_mask),
				"every source-equivalent motion predicate outcome has a legal MRT key");
		}
	}

	MotionObjectBeginInput begin_motion = {};
	begin_motion.object_handle = 17;
	begin_motion.framebuffer_available = 1;
	begin_motion.pixel_consumer_active = 1;
	begin_motion.velocity_texture_available = 1;
	MotionObjectScopeState scope = EvaluateBeginMotionObject(begin_motion);
	Check(scope.active == 1 && scope.object_id == 18 &&
		scope.cockpit_active == 0 && scope.force_capture == 0,
		"ordinary motion-object begin scope");
	begin_motion.motion_object_flags =
		kMotionObjectFlagLegacyBlur | kMotionObjectFlagForceCapture;
	scope = EvaluateBeginMotionObject(begin_motion);
	Check(scope.active == 1 && scope.force_capture == 1 &&
		scope.object_id == (kMotionObjectLegacyBlurMask | 18u),
		"motion-object force/high-bit encoding");
	begin_motion.capture_locked = 1;
	begin_motion.cockpit_draw = 0;
	Check(EvaluateBeginMotionObject(begin_motion).active == 0,
		"capture lock rejects an ordinary motion-object scope");
	begin_motion.cockpit_draw = 1;
	scope = EvaluateBeginMotionObject(begin_motion);
	Check(scope.active == 1 && scope.cockpit_active == 1 &&
		scope.force_capture == 0 && scope.object_id == 0,
		"capture lock permits the separate cockpit motion scope");
	MotionObjectEndDecision end_motion = EvaluateEndMotionObject(scope, 1);
	Check(end_motion.capture_cockpit_previous_view_projection == 1 &&
		end_motion.resulting_scope.active == 0 &&
		end_motion.resulting_scope.object_id == 0,
		"cockpit motion end captures matrices then clears the scope");
	begin_motion.object_handle = -1;
	begin_motion.capture_locked = 0;
	Check(EvaluateBeginMotionObject(begin_motion).active == 0,
		"negative motion handle never opens a scope");

	MotionSuppressionTransitionDecision suppression =
		EvaluateMotionSuppressionTransition(0, MotionSuppressionOperation::Suspend, 1);
	Check(suppression.resulting_depth == 1 &&
		suppression.mark_shader_state_dirty == 1,
		"motion suspension increments and dirties when the mode is active");
	suppression = EvaluateMotionSuppressionTransition(
		suppression.resulting_depth, MotionSuppressionOperation::Suspend, 0);
	Check(suppression.resulting_depth == 2 &&
		suppression.mark_shader_state_dirty == 0,
		"nested motion suspension");
	suppression = EvaluateMotionSuppressionTransition(
		suppression.resulting_depth, MotionSuppressionOperation::Resume, 1);
	Check(suppression.resulting_depth == 1 && suppression.unmatched_resume == 0,
		"motion resume decrements one nesting level");
	suppression = EvaluateMotionSuppressionTransition(
		0, MotionSuppressionOperation::Resume, 1);
	Check(suppression.resulting_depth == 0 && suppression.unmatched_resume == 1 &&
		suppression.mark_shader_state_dirty == 1,
		"unmatched motion resume stays at zero but preserves GL4 dirty side effect");

	MotionCaptureLockTransitionInput lock = {};
	lock.event = MotionCaptureLockEvent::CaptureWorldForLatePost;
	lock.pixel_target_enabled = 1;
	MotionCaptureLockTransitionDecision lock_decision =
		EvaluateMotionCaptureLockTransition(lock);
	Check(lock_decision.resulting_locked == 1 && lock_decision.changed == 1,
		"world capture locks ordinary motion writes");
	lock.currently_locked = 1;
	lock.frozen = 1;
	lock.pixel_target_enabled = 1;
	lock_decision = EvaluateMotionCaptureLockTransition(lock);
	Check(lock_decision.resulting_locked == 1 && lock_decision.changed == 0,
		"frozen capture does not mutate an existing lock");
	lock.currently_locked = 0;
	lock_decision = EvaluateMotionCaptureLockTransition(lock);
	Check(lock_decision.resulting_locked == 0 && lock_decision.changed == 0,
		"frozen world capture does not acquire the motion lock");
	lock.currently_locked = 1;
	lock.frozen = 0;
	lock.event = MotionCaptureLockEvent::PresentNextFramebuffer;
	lock_decision = EvaluateMotionCaptureLockTransition(lock);
	Check(lock_decision.resulting_locked == 0 && lock_decision.changed == 1,
		"presented-frame rotation releases capture lock");
	lock.event = MotionCaptureLockEvent::FramebufferRebuild;
	Check(EvaluateMotionCaptureLockTransition(lock).resulting_locked == 0,
		"framebuffer rebuild releases capture lock");

	MotionRegionFillInput fill = {};
	fill.pixel_motion_mode_enabled = 1;
	fill.framebuffer_available = 1;
	fill.velocity_texture_available = 1;
	fill.positive_clip_extent = 1;
	fill.object_handle = 41;
	MotionRegionFillDecision fill_decision = EvaluateMotionRegionFill(fill);
	Check(fill_decision.execute == 1 && fill_decision.flush_fonts_before_predicate == 1 &&
		fill_decision.write_mask == (kWriteVelocity | kWriteObjectId) &&
		fill_decision.protective_object_id == 42,
		"small-view motion protection fill is an ordered V|I operation");
	fill.frozen = 1;
	fill_decision = EvaluateMotionRegionFill(fill);
	Check(fill_decision.execute == 0 && fill_decision.write_mask == 0,
		"frozen histories suppress small-view protection fill");

	LiteralMrtState default_mrt = DefaultSceneMrtState();
	Check(default_mrt.selected_draw_buffers ==
		(kWriteColor | kWriteProtectionMask | kWriteAoClass) &&
		DeriveLiteralMrtWriteMask(default_mrt) ==
		(kWriteColor | kWriteProtectionMask | kWriteAoClass),
		"literal ordinary scene draw-buffer state");
	for (size_t i = 0; i < 5; ++i)
		Check(default_mrt.attachment_masks.rgba[i] == kChannelRgba,
			"default literal attachment channel masks");
	Check(IsLiteralMrtStateValid(default_mrt),
		"default literal MRT state is in the five-attachment/four-channel domain");
	LiteralMrtState invalid_literal_mrt = default_mrt;
	invalid_literal_mrt.selected_draw_buffers |= 1u << 7;
	Check(!IsLiteralMrtStateValid(invalid_literal_mrt),
		"literal MRT state rejects unknown attachment selections");

	LiteralMrtState post_mask_mrt = default_mrt;
	post = EvaluatePostMaskOnlyTransition(0, 1,
		post_mask_mrt.attachment_masks);
	post_mask_mrt.attachment_masks = post.masks;
	Check(DeriveLiteralMrtWriteMask(post_mask_mrt) == kWriteProtectionMask,
		"SetPostMaskOnly actual transition produces literal M-only state");
	AlphaTypeTransitionInput vertex_alpha = {};
	vertex_alpha.current_type = static_cast<uint32_t>(LegacyAlphaType::Vertex);
	vertex_alpha.requested_type = vertex_alpha.current_type;
	AlphaTypeTransitionDecision vertex_transition =
		EvaluateAlphaTypeTransition(vertex_alpha);
	Check(vertex_transition.redundant_early_return == 1 &&
		vertex_transition.configure_auxiliary_blend_masks == 0 &&
		DeriveLiteralMrtWriteMask(post_mask_mrt) == kWriteProtectionMask,
		"redundant AT_VERTEX preserves M-only history");
	vertex_alpha.current_type = static_cast<uint32_t>(LegacyAlphaType::Constant);
	vertex_transition = EvaluateAlphaTypeTransition(vertex_alpha);
	LiteralMrtState actual_vertex_mrt = post_mask_mrt;
	if (vertex_transition.configure_auxiliary_blend_masks != 0)
		actual_vertex_mrt.attachment_masks = ApplyConfigurePostMaskBlend(
			actual_vertex_mrt.attachment_masks);
	Check(DeriveLiteralMrtWriteMask(actual_vertex_mrt) ==
		(kWriteProtectionMask | kWriteAoClass),
		"actual AT_VERTEX transition re-enables AO-class while color stays masked");

	uint32_t seen_legal_masks = 0;
	auto record_mrt = [&seen_legal_masks](const MrtDrawRoutingDecision &value,
		uint32_t expected, const char *name)
	{
		Check(value.logical_write_mask == expected && value.legal_write_mask == 1,
			name);
		for (size_t i = 0; i < sizeof(kLegalMrtWriteMasks) /
			sizeof(kLegalMrtWriteMasks[0]); ++i)
			if (value.logical_write_mask == kLegalMrtWriteMasks[i])
				seen_legal_masks |= 1u << i;
	};
	MrtDrawRoutingInput mrt = {};
	mrt.literal_state = default_mrt;
	mrt.draw_kind = MrtDrawKind::Polygon;
	mrt.target = RenderTargetClass::PostPresent;
	record_mrt(EvaluateMrtDrawRouting(mrt), kWriteColor,
		"post-present/cockpit color-only route");
	mrt.target = RenderTargetClass::CockpitScene;
	mrt.drawing_to_scene_framebuffer = 1;
	MrtDrawRoutingDecision cockpit_route = EvaluateMrtDrawRouting(mrt);
	record_mrt(cockpit_route, kWriteColor,
		"cockpit-layer capture uses color only");
	Check(cockpit_route.override_draw_buffers == 1,
		"cockpit polygon repeats the GL4 color-only draw-buffer selection");
	mrt.target = RenderTargetClass::Scene;
	mrt.drawing_to_scene_framebuffer = 1;
	mrt.zbuffer_enabled = 0;
	record_mrt(EvaluateMrtDrawRouting(mrt),
		kWriteColor | kWriteProtectionMask,
		"Z-disabled polygon route is C|M");
	mrt.zbuffer_enabled = 1;
	record_mrt(EvaluateMrtDrawRouting(mrt),
		kWriteColor | kWriteProtectionMask | kWriteAoClass,
		"ordinary Z-enabled polygon route is C|M|A");
	mrt.pixel_motion_mode_enabled = 1;
	mrt.motion_write = 1;
	record_mrt(EvaluateMrtDrawRouting(mrt),
		kWriteColor | kWriteVelocity | kWriteProtectionMask | kWriteAoClass,
		"eligible motion route is C|V|M|A");
	mrt.object_id_write = 1;
	record_mrt(EvaluateMrtDrawRouting(mrt),
		kWriteColor | kWriteVelocity | kWriteProtectionMask | kWriteAoClass |
			kWriteObjectId,
		"eligible noncockpit motion object route is C|V|M|A|I");
	mrt = {};
	mrt.literal_state = post_mask_mrt;
	mrt.draw_kind = MrtDrawKind::Polygon;
	mrt.target = RenderTargetClass::Scene;
	mrt.drawing_to_scene_framebuffer = 1;
	mrt.zbuffer_enabled = 1;
	record_mrt(EvaluateMrtDrawRouting(mrt), kWriteProtectionMask,
		"redundant AT_VERTEX with no draw-buffer override stays M-only");
	mrt.literal_state = actual_vertex_mrt;
	record_mrt(EvaluateMrtDrawRouting(mrt),
		kWriteProtectionMask | kWriteAoClass,
		"actual AT_VERTEX transition produces M|A");
	mrt.literal_state = post_mask_mrt;
	mrt.pixel_motion_mode_enabled = 1;
	record_mrt(EvaluateMrtDrawRouting(mrt),
		kWriteProtectionMask | kWriteAoClass,
		"redundant AT_VERTEX plus polygon override produces M|A");
	mrt = {};
	mrt.literal_state = default_mrt;
	mrt.draw_kind = MrtDrawKind::SmallViewProtectionFill;
	mrt.target = RenderTargetClass::Scene;
	mrt.drawing_to_scene_framebuffer = 1;
	MrtDrawRoutingDecision small_view_route = EvaluateMrtDrawRouting(mrt);
	record_mrt(small_view_route,
		kWriteVelocity | kWriteObjectId,
		"small-view protection fill route is V|I");
	Check(small_view_route.state_after_draw.selected_draw_buffers ==
		kDefaultSceneSelectedDrawBuffers &&
		DeriveLiteralMrtWriteMask(small_view_route.state_after_draw) ==
		(kWriteColor | kWriteProtectionMask | kWriteAoClass),
		"small-view fill restores GL4 ordinary scene draw buffers afterward");
	Check(seen_legal_masks ==
		((1u << (sizeof(kLegalMrtWriteMasks) /
			sizeof(kLegalMrtWriteMasks[0]))) - 1u),
		"ordered GL4 cases reach all and only the exact eight legal MRT masks");

	mrt = {};
	mrt.literal_state = default_mrt;
	mrt.draw_kind = MrtDrawKind::Primitive;
	mrt.target = RenderTargetClass::Scene;
	mrt.drawing_to_scene_framebuffer = 1;
	mrt.zbuffer_enabled = 0;
	record_mrt(EvaluateMrtDrawRouting(mrt),
		kWriteColor | kWriteProtectionMask | kWriteAoClass,
		"SetPixel/line/special-line retain AO-class even with sticky Z off");
	mrt.draw_kind = MrtDrawKind::FontFlush;
	record_mrt(EvaluateMrtDrawRouting(mrt),
		kWriteColor | kWriteProtectionMask,
		"font flush selects C|M from sticky Z-off state before disabling glyph depth");
	mrt.zbuffer_enabled = 1;
	record_mrt(EvaluateMrtDrawRouting(mrt),
		kWriteColor | kWriteProtectionMask | kWriteAoClass,
		"font flush selects C|M|A from sticky Z-on state");
	mrt.draw_kind = MrtDrawKind::Polygon;
	mrt.pixel_motion_mode_enabled = 1;
	mrt.capture_locked = 1;
	mrt.cockpit_draw = 1;
	mrt.motion_write = 1;
	record_mrt(EvaluateMrtDrawRouting(mrt),
		kWriteColor | kWriteVelocity | kWriteProtectionMask | kWriteAoClass,
		"late cockpit capture-lock exception writes velocity but never object ID");
	Check(EvaluateMrtDrawRouting(mrt).late_cockpit_draw_buffer_override == 1,
		"late cockpit forces the scene draw-buffer override after capture lock");
	mrt.motion_write = 0;
	record_mrt(EvaluateMrtDrawRouting(mrt),
		kWriteColor | kWriteProtectionMask | kWriteAoClass,
		"ineligible late cockpit still forces exact nonmotion scene routing");

	ZBiasTransitionDecision z_bias = EvaluateZBiasTransition({ 1.25f, -3.5f });
	Check(z_bias.changed == 1 && z_bias.resulting == -3.5f &&
		z_bias.affects_depth_and_perspective_w == 1 &&
		z_bias.uses_polygon_offset == 0 && z_bias.marks_shader_state_dirty == 0,
		"SetZBias stores the exact float for depth/perspective without polygon offset");
	z_bias = EvaluateZBiasTransition({ 0.0f, -0.0f });
	Check(z_bias.changed == 0 && !signbit(z_bias.resulting),
		"SetZBias uses C++ float equality and preserves current +0 on -0 request");
	const float quiet_nan = std::numeric_limits<float>::quiet_NaN();
	z_bias = EvaluateZBiasTransition({ quiet_nan, quiet_nan });
	Check(z_bias.changed == 1 && isnan(z_bias.resulting),
		"SetZBias repeats assignment for NaN exactly as GL4 float inequality");
	Check(kSetterTransitionContract[static_cast<size_t>(LegacySetterFamily::ZBias)].
		equality_returns_before_side_effects == 1 &&
		kSetterTransitionContract[static_cast<size_t>(LegacySetterFamily::ZBias)].
		always_assigns == 0,
		"Z-bias setter inventory records equality-gated assignment");
}

static void TestDeterministicCorpus()
{
	Check(kCorpusAssetContractCount == static_cast<size_t>(CorpusAsset::Count),
		"concrete corpus asset count");
	for (size_t i=0;i<kCorpusAssetContractCount;++i)
	{
		Check(static_cast<size_t>(kCorpusAssetContract[i].asset)==i,
			"corpus asset enum order");
		Check(strlen(kCorpusAssetContract[i].sha256)==64,
			"corpus asset SHA-256 text");
		Check(kCorpusAssetContract[i].room_count>0 &&
			kCorpusAssetContract[i].face_count>0,
			"corpus asset parsed scene statistics");
	}
	Check(kCorpusInputScheduleContractCount ==
		static_cast<size_t>(CorpusInputSchedule::Count),
		"input schedule inventory");
	for(size_t i=0;i<kCorpusInputScheduleContractCount;++i)
	{
		const CorpusInputScheduleContract &schedule=kCorpusInputScheduleContract[i];
		Check(static_cast<size_t>(schedule.schedule)==i,
			"input schedule enum order");
		Check(schedule.first_action+schedule.action_count<=kCorpusInputActionContractCount,
			"input action span");
		Check(schedule.complete_zeroed_controls_when_unlisted==1 &&
			schedule.physical_input_polled==0,
			"input schedules fully replace physical controls");
		for(uint32_t a=0;a<schedule.action_count;++a)
		{
			const CorpusInputActionContract &action=
				kCorpusInputActionContract[schedule.first_action+a];
			Check(action.schedule==schedule.schedule &&
				action.first_tick<=action.last_tick_inclusive,
				"input action belongs to schedule");
		}
	}
	Check(kCorpusPreferenceContractCount ==
		static_cast<size_t>(CorpusPreference::Count),
		"concrete preference inventory");
	for(size_t i=0;i<kCorpusPreferenceContractCount;++i)
	{
		const CorpusPreferenceContract &preference=kCorpusPreferenceContract[i];
		Check(static_cast<size_t>(preference.preference)==i,
			"preference enum order");
		Check(preference.renderer.width==1280 && preference.renderer.height==720 &&
			preference.renderer.window_width==1280 &&
			preference.renderer.window_height==720 &&
			preference.renderer.bit_depth==32 && preference.renderer.gamma>0.0f,
			"fixed corpus resolution and valid gamma");
		Check(preference.engine.use_newrender==0 && preference.engine.face_probe==0 &&
			preference.record_requested_and_applied_msaa==1,
			"oracle path and sample signatures frozen");
	}
	Check(kCorpusPreferenceContract[static_cast<size_t>(CorpusPreference::FullPost720p)].
		renderer.gtao_enabled==1 &&
		kCorpusPreferenceContract[static_cast<size_t>(CorpusPreference::FullPost720p)].
		renderer.bloom_enabled==1 &&
		kCorpusPreferenceContract[static_cast<size_t>(CorpusPreference::FullPost720p)].
		renderer.motion_vector_mode==1,
		"full-post preference enables the complete post chain");
	Check(kCorpusPreferenceContract[static_cast<size_t>(CorpusPreference::FullPostSsaa2Msaa4)].
		renderer.supersampling_factor==2 &&
		kCorpusPreferenceContract[static_cast<size_t>(CorpusPreference::FullPostSsaa2Msaa4)].
		renderer.msaa_samples==4, "combined sampling preference");

	Check(kCorpusCaseContractCount == static_cast<size_t>(CorpusCase::Count),
		"concrete deterministic case inventory");
	for(size_t i=0;i<kCorpusCaseContractCount;++i)
	{
		const CorpusCaseContract &corpus=kCorpusCaseContract[i];
		Check(static_cast<size_t>(corpus.corpus_case)==i,
			"corpus case enum order");
		Check(static_cast<uint32_t>(corpus.asset)<static_cast<uint32_t>(CorpusAsset::Count) &&
			static_cast<uint32_t>(corpus.input)<static_cast<uint32_t>(CorpusInputSchedule::Count) &&
			static_cast<uint32_t>(corpus.preference)<static_cast<uint32_t>(CorpusPreference::Count),
			"corpus case table references");
		Check(corpus.first_frame_span+corpus.frame_span_count<=kCorpusFrameSpanCount &&
			corpus.frame_span_count>0, "corpus capture frame spans");
		Check(corpus.fixed_timestep_numerator==1 &&
			corpus.fixed_timestep_denominator==60 && corpus.fresh_process==1,
			"fresh-process fixed-timestep case");
		Check(((corpus.graph_node_mask | corpus.multisample_graph_node_mask) >>
			static_cast<uint32_t>(GraphNodeId::Count))==0,
			"corpus graph node mask range");
		Check(ExpectedCorpusGraphNodeMask(corpus,1)==corpus.graph_node_mask &&
			ExpectedCorpusGraphNodeMask(corpus,4)==
				(corpus.graph_node_mask|corpus.multisample_graph_node_mask),
			"corpus graph resolves follow applied, not requested, MSAA");
		for(uint32_t s=0;s<corpus.frame_span_count;++s)
		{
			const CorpusFrameSpan &span=kCorpusFrameSpan[corpus.first_frame_span+s];
			Check(span.first_frame<=span.last_frame_inclusive,
				"ordered corpus frame span");
		}
		for(size_t j=i+1;j<kCorpusCaseContractCount;++j)
			Check(strcmp(corpus.id,kCorpusCaseContract[j].id)!=0,
				"unique corpus case ID");
	}
	const CorpusCaseContract &authored=kCorpusCaseContract[
		static_cast<size_t>(CorpusCase::IndoorLevel1HoldAuthored)];
	Check((authored.graph_node_mask&CorpusGraphBit(GraphNodeId::CapDepthLogical))==0,
		"no-post authored case cannot capture logical depth");
	const CorpusCaseContract &cockpit=kCorpusCaseContract[
		static_cast<size_t>(CorpusCase::CockpitTheCoreFullPost)];
	Check((cockpit.graph_node_mask&(CorpusGraphBit(GraphNodeId::BloomThreshold)|
		CorpusGraphBit(GraphNodeId::BloomDown)|CorpusGraphBit(GraphNodeId::BloomUp)))==0 &&
		(cockpit.graph_node_mask&CorpusGraphBit(GraphNodeId::BloomDeferred))!=0,
		"cockpit case runs deferred, not normal-branch, bloom");
	Check(DeriveCorpusRngSeed(UINT64_C(0xd3449401),CorpusRngStream::PsRand) !=
		DeriveCorpusRngSeed(UINT64_C(0xd3449401),CorpusRngStream::CrtRand),
		"named RNG streams derive distinct seeds");
	const uint32_t expected_rng_seeds[] = {
		0xf6d0ce16u, 0x0fffc6d6u, 0xc5b20294u, 0xf8455256u };
	for(uint32_t i=0;i<static_cast<uint32_t>(CorpusRngStream::Count);++i)
		Check(DeriveCorpusRngSeed(UINT64_C(0xd3449401),
			static_cast<CorpusRngStream>(i))==expected_rng_seeds[i],
			"exact named RNG seed derivation");
	Check(kOptionalExternalCorpusAssetContract.required_for_repository_ci==0 &&
		strlen(kOptionalExternalCorpusAssetContract.sha256)==64,
		"licensed demo remains optional external corpus");
	for(size_t i=0;i<kCaptureArtifactContractCount;++i)
		Check(strchr(kCaptureArtifactContract[i].name_pattern,'*')==nullptr,
			"artifact semantic contracts contain no wildcards");
	Check(kPinnedRendererSpecSha256[0]==0x8e && kPinnedRendererSpecSha256[31]==0xfd,
		"authoritative renderer spec hash");
	Check(kTraceSerializationContract.canonical_little_endian==1 &&
		kTraceSerializationContract.field_serialized_without_native_padding==1 &&
		kTraceSerializationContract.sha256_payload_integrity==1,
		"portable trace serialization and integrity");
	Check(sizeof(CapturedDeviceIdentity)==192 && sizeof(AttachmentCaptureHeader)==176 &&
		sizeof(DeterministicTraceMetadata)==688,
		"trace reproduction metadata ABIs");
}

static void TestExecutableCorpusCoverage()
{
	Check(kCorpusEventKindContractCount==static_cast<size_t>(CorpusEventKind::Count),
		"typed corpus event inventory");
	for(size_t i=0;i<kCorpusEventKindContractCount;++i)
	{
		const CorpusEventKindContract &kind=kCorpusEventKindContract[i];
		Check(static_cast<size_t>(kind.event)==i && kind.id!=nullptr && kind.id[0]!=0 &&
			kind.argument_count>0 &&
			static_cast<uint32_t>(kind.argument_domain)<
				static_cast<uint32_t>(CorpusEventArgumentDomain::Count),
			"event kind has ordered, executable argument domain");
		Check(kind.fault_injection==
			(kind.event==CorpusEventKind::InjectFault ? 1u : 0u),
			"only the typed fault event may inject a fault");
		for(size_t j=i+1;j<kCorpusEventKindContractCount;++j)
			Check(strcmp(kind.id,kCorpusEventKindContract[j].id)!=0,
				"unique event kind ID");
	}

	Check(kCorpusEventScheduleContractCount==
		static_cast<size_t>(CorpusEventSchedule::Count),
		"fixed-tick event schedule inventory");
	uint32_t expected_first_event=0;
	uint32_t fault_mask=0;
	for(size_t i=0;i<kCorpusEventScheduleContractCount;++i)
	{
		const CorpusEventScheduleContract &schedule=kCorpusEventScheduleContract[i];
		Check(static_cast<size_t>(schedule.schedule)==i &&
			schedule.first_event==expected_first_event && schedule.event_count>0 &&
			schedule.first_event+schedule.event_count<=kCorpusEventActionContractCount,
			"event schedules are ordered, nonempty contiguous spans");
		uint32_t previous_tick=0;
		for(uint32_t e=0;e<schedule.event_count;++e)
		{
			const CorpusEventActionContract &action=
				kCorpusEventActionContract[schedule.first_event+e];
			Check(action.schedule==schedule.schedule && action.repeat_count>0 &&
				(e==0 || action.tick>=previous_tick),
				"event belongs to schedule and has fixed nondecreasing tick");
			previous_tick=action.tick;
			const uint32_t event_index=static_cast<uint32_t>(action.event);
			Check(event_index<kCorpusEventKindContractCount,
				"event kind reference is in range");
			if(event_index<kCorpusEventKindContractCount)
			{
				const CorpusEventKindContract &kind=kCorpusEventKindContract[event_index];
				Check(action.argument<kind.argument_count,
					"event argument belongs to its closed domain");
				if(action.event==CorpusEventKind::SweepPreferredField ||
					action.event==CorpusEventKind::SweepEngineField)
					Check(action.argument+action.repeat_count<=kind.argument_count,
						"field sweep is a valid contiguous enum range");
			}
			if(action.event==CorpusEventKind::InjectFault)
			{
				const uint32_t fault=static_cast<uint32_t>(action.fault);
				Check(fault>static_cast<uint32_t>(CorpusFault::None) &&
					fault<static_cast<uint32_t>(CorpusFault::Count) &&
					action.argument==fault && action.repeat_count==1,
					"fault event names one valid, non-repeated injected result");
				if(fault<32) fault_mask|=1u<<fault;
			}
			else
				Check(action.fault==CorpusFault::None,
					"ordinary event carries no latent fault");
		}
		expected_first_event+=schedule.event_count;
		for(size_t j=i+1;j<kCorpusEventScheduleContractCount;++j)
			Check(strcmp(schedule.id,kCorpusEventScheduleContract[j].id)!=0,
				"unique event schedule ID");
	}
	Check(expected_first_event==kCorpusEventActionContractCount,
		"event schedule spans account for every action exactly once");
	const uint32_t expected_fault_mask=
		((1u<<static_cast<uint32_t>(CorpusFault::Count))-1u)&~1u;
	Check(fault_mask==expected_fault_mask,
		"forced MSAA fallback and every required WSI/device fault are scheduled");

	uint32_t schedule_use[static_cast<size_t>(CorpusEventSchedule::Count)]={};
	for(size_t i=0;i<kCorpusCaseContractCount;++i)
	{
		const CorpusCaseContract &corpus=kCorpusCaseContract[i];
		const uint32_t schedule_index=static_cast<uint32_t>(corpus.event_schedule);
		Check(schedule_index<kCorpusEventScheduleContractCount,
			"case event schedule reference is in range");
		if(schedule_index>=kCorpusEventScheduleContractCount) continue;
		++schedule_use[schedule_index];
		uint32_t final_capture_frame=0;
		for(uint32_t s=0;s<corpus.frame_span_count;++s)
		{
			const CorpusFrameSpan &span=kCorpusFrameSpan[corpus.first_frame_span+s];
			if(span.last_frame_inclusive>final_capture_frame)
				final_capture_frame=span.last_frame_inclusive;
		}
		const CorpusEventScheduleContract &schedule=
			kCorpusEventScheduleContract[schedule_index];
		uint32_t final_event_tick=0;
		for(uint32_t e=0;e<schedule.event_count;++e)
		{
			const CorpusEventActionContract &action=
				kCorpusEventActionContract[schedule.first_event+e];
			const uint32_t end_tick=action.tick+action.repeat_count-1;
			Check(end_tick>=action.tick,"event repetition tick arithmetic");
			if(end_tick>final_event_tick) final_event_tick=end_tick;
		}
		Check(corpus.warmup_frames<=final_capture_frame &&
			final_event_tick<=final_capture_frame,
			"case warmup and complete event flow are covered by named capture frames");
	}
	for(size_t i=0;i<static_cast<size_t>(CorpusEventSchedule::Count);++i)
		Check(schedule_use[i]>0,"every fixed-tick event schedule is instantiated by a case");

	uint32_t coverage_use[40]={};
	uint32_t case_use[static_cast<size_t>(CorpusCase::Count)]={};
	uint32_t ssaa_mask=0;
	uint32_t requested_msaa_mask=0;
	uint32_t gamma_mask=0;
	uint32_t fallback_instantiation=0;
	for(size_t i=0;i<kCorpusCoverageInstantiationContractCount;++i)
	{
		const CorpusCoverageInstantiationContract &mapping=
			kCorpusCoverageInstantiationContract[i];
		const uint32_t case_index=static_cast<uint32_t>(mapping.corpus_case);
		Check(mapping.coverage_index<kCorpusCoverageContractCount &&
			case_index<kCorpusCaseContractCount && mapping.required_event_mask!=0 &&
			(mapping.required_event_mask>>static_cast<uint32_t>(CorpusEventKind::Count))==0,
			"coverage mapping references a case and typed events in range");
		if(mapping.coverage_index>=kCorpusCoverageContractCount ||
			case_index>=kCorpusCaseContractCount) continue;
		++coverage_use[mapping.coverage_index];
		++case_use[case_index];
		const CorpusCaseContract &corpus=kCorpusCaseContract[case_index];
		const CorpusEventScheduleContract &schedule=kCorpusEventScheduleContract[
			static_cast<size_t>(corpus.event_schedule)];
		uint64_t actual_event_mask=0;
		for(uint32_t e=0;e<schedule.event_count;++e)
			actual_event_mask|=CorpusEventBit(kCorpusEventActionContract[
				schedule.first_event+e].event);
		Check((mapping.required_event_mask&~actual_event_mask)==0,
			"coverage evidence is present in the selected case schedule");
		const CorpusPreferenceContract &preference=kCorpusPreferenceContract[
			static_cast<size_t>(corpus.preference)];
		if(mapping.coverage_index==22)
			ssaa_mask|=preference.renderer.supersampling_factor;
		if(mapping.coverage_index==23)
		{
			const uint32_t requested=preference.renderer.msaa_samples;
			if(requested==0) requested_msaa_mask|=1u<<0;
			else if(requested==2) requested_msaa_mask|=1u<<1;
			else if(requested==4) requested_msaa_mask|=1u<<2;
			else if(requested==8) requested_msaa_mask|=1u<<3;
			if(corpus.event_schedule==CorpusEventSchedule::Msaa2Fallback)
				fallback_instantiation=1;
		}
		if(mapping.coverage_index==24)
		{
			if(Near(preference.renderer.gamma,0.5f)) gamma_mask|=1u;
			if(Near(preference.renderer.gamma,2.0f)) gamma_mask|=2u;
		}
	}
	for(size_t i=0;i<kCorpusCoverageContractCount;++i)
		Check(coverage_use[i]>0,"every parity coverage row has an executable case mapping");
	for(size_t i=0;i<kCorpusCaseContractCount;++i)
		Check(case_use[i]>0,"every concrete case contributes to a coverage row");
	Check(ssaa_mask==(1u|2u|4u),"SSAA coverage instantiates immutable 1x, 2x, and 4x preferences");
	Check(requested_msaa_mask==0x0fu && fallback_instantiation==1,
		"MSAA coverage instantiates requested 0/2/4/8 and a forced 2-to-1 fallback");
	Check(gamma_mask==3u,"gamma coverage instantiates both 0.5 and 2.0 extremes");

	uint32_t preferred_field_use[static_cast<size_t>(CorpusPreferredField::Count)]={};
	uint32_t engine_field_use[static_cast<size_t>(CorpusEngineField::Count)]={};
	const CorpusEventScheduleContract &sweep=kCorpusEventScheduleContract[
		static_cast<size_t>(CorpusEventSchedule::PreferenceSweep)];
	for(uint32_t e=0;e<sweep.event_count;++e)
	{
		const CorpusEventActionContract &action=
			kCorpusEventActionContract[sweep.first_event+e];
		if(action.event==CorpusEventKind::SweepPreferredField)
			for(uint32_t f=0;f<action.repeat_count;++f)
				++preferred_field_use[action.argument+f];
		if(action.event==CorpusEventKind::SweepEngineField)
			for(uint32_t f=0;f<action.repeat_count;++f)
				++engine_field_use[action.argument+f];
	}
	for(size_t i=0;i<static_cast<size_t>(CorpusPreferredField::Count);++i)
		Check(preferred_field_use[i]==1,"each renderer preferred-state field has one sweep tick");
	for(size_t i=0;i<static_cast<size_t>(CorpusEngineField::Count);++i)
		Check(engine_field_use[i]==1,"each engine rendering/detail field has one sweep tick");
	Check(kOptionalExternalCorpusAssetContract.required_for_repository_ci==0,
		"licensed external corpus remains optional for repository CI");
}

static void TestTextureVersionRules()
{
	TextureVersionBindInput input = {};
	input.has_logical_mapping = 1;
	input.mapped_identity_matches = 1;
	TextureVersionBindDecision decision = EvaluateTextureVersionBind(input);
	Check(decision.reuse_mapped_logical_version == 1 &&
		decision.snapshot_cpu_pixels_now == 0, "clean texture bind reuses version");

	input.dirty_flags = kTextureBitmapChanged | kTextureBitmapBrandNew;
	input.mapped_version_referenced_earlier_in_segment = 1;
	decision = EvaluateTextureVersionBind(input);
	Check(decision.snapshot_cpu_pixels_now == 1 &&
		decision.create_new_logical_version == 1 &&
		decision.attach_new_version_to_subsequent_draws == 1,
		"dirty texture snapshots at bind");
	Check(decision.clear_dirty_flags_now == input.dirty_flags,
		"only active texture dirty flags clear at bind");
	Check(decision.copy_on_write_image_required == 1 &&
		decision.may_recycle_completed_image == 0,
		"earlier capture reference requires copy-on-write");

	input.mapped_version_referenced_earlier_in_segment = 0;
	input.mapped_version_last_use_timeline = 9;
	input.completed_timeline = 8;
	decision = EvaluateTextureVersionBind(input);
	Check(decision.copy_on_write_image_required == 1,
		"unfinished GPU texture use requires copy-on-write");

	input.mapped_version_last_use_timeline = 8;
	decision = EvaluateTextureVersionBind(input);
	Check(decision.may_recycle_completed_image == 1,
		"completed texture image may recycle");

	input.dirty_flags = 0;
	input.mapped_identity_matches = 0;
	decision = EvaluateTextureVersionBind(input);
	Check(decision.invalidate_stale_mapping == 1 &&
		decision.create_new_logical_version == 1,
		"handle identity change creates a new version");

	for (size_t i = 0; i < kTextureMappingInvalidationContractCount; ++i)
	{
		Check(kTextureMappingInvalidationContract[i].invalidate_logical_mapping_immediately == 1,
			"cache events invalidate texture mapping");
		Check(kTextureMappingInvalidationContract[i].destroy_in_flight_versions_immediately == 0,
			"cache events preserve in-flight texture versions");
	}
}

static PhysicalDeviceUuid TestDeviceUuid(uint8_t final_byte)
{
	PhysicalDeviceUuid uuid = {};
	uuid.bytes[kPhysicalDeviceUuidSize - 1] = final_byte;
	return uuid;
}

static DeviceProfileSupport FullDeviceProfile(uint32_t graphics_family = 0,
	uint32_t present_family = 0)
{
	DeviceProfileSupport profile = {};
	profile.api_major = kVulkanRequiredApiMajor;
	profile.api_minor = kVulkanRequiredApiMinor;
	profile.api_patch = kVulkanRequiredApiPatch;
	profile.required_feature_bits = kAllRequiredDeviceFeatureBits;
	profile.required_format_bits = kAllRequiredFormatBits;
	profile.all_required_limits_satisfied = 1;
	profile.surface_configuration_satisfied = 1;
	profile.requested_signature_supported = 1;
	profile.descriptor_page_tier = 256;
	profile.common_sample_count_mask = 1 | 2 | 4;
	profile.graphics_compute_queue_family = graphics_family;
	profile.present_queue_family = present_family;
	return profile;
}

static PhysicalDeviceCandidate TestDeviceCandidate(uint8_t uuid_byte,
	uint32_t enumeration_index, PhysicalDeviceType type,
	uint64_t budget, uint32_t graphics_family = 0, uint32_t present_family = 0)
{
	PhysicalDeviceCandidate candidate = {};
	candidate.uuid = TestDeviceUuid(uuid_byte);
	candidate.enumeration_index = enumeration_index;
	candidate.type = type;
	candidate.profile = FullDeviceProfile(graphics_family, present_family);
	candidate.device_local_budget_bytes = budget;
	return candidate;
}

static void TestDeviceSelectionAndTextureUploadContracts()
{
	DeviceProfileSupport profile = FullDeviceProfile();
	Check(SupportsRequiredDeviceProfile(profile), "complete Vulkan device profile passes");
	profile.api_minor = kVulkanRequiredApiMinor - 1;
	Check(!SupportsRequiredDeviceProfile(profile), "Vulkan API 1.2 is filtered");
	profile = FullDeviceProfile();
	profile.required_feature_bits &=
		~(uint64_t(1) << static_cast<uint32_t>(RequiredDeviceFeature::TimelineSemaphore));
	Check(!SupportsRequiredDeviceProfile(profile), "missing required feature is filtered");
	profile = FullDeviceProfile();
	profile.required_format_bits &=
		~(uint32_t(1) << static_cast<uint32_t>(FormatSemantic::Depth));
	Check(!SupportsRequiredDeviceProfile(profile), "missing required format usage is filtered");
	profile = FullDeviceProfile();
	profile.descriptor_page_tier = 48;
	Check(!SupportsRequiredDeviceProfile(profile), "noncanonical descriptor tier is filtered");
	profile = FullDeviceProfile();
	profile.requested_signature_supported = 0;
	Check(!SupportsRequiredDeviceProfile(profile), "unsupported requested signature is filtered");

	PhysicalDeviceCandidate rank_left = TestDeviceCandidate(10, 0,
		PhysicalDeviceType::Integrated, 1024, 0, 0);
	PhysicalDeviceCandidate rank_right = rank_left;
	rank_right.uuid = TestDeviceUuid(11);
	rank_right.profile.present_queue_family = 1;
	Check(ComparePhysicalDevicePreference(rank_left, rank_right) > 0,
		"unified graphics/present queue is first device rank key");
	rank_right = rank_left; rank_right.uuid = TestDeviceUuid(11);
	rank_right.type = PhysicalDeviceType::Virtual;
	Check(ComparePhysicalDevicePreference(rank_left, rank_right) > 0,
		"physical device type is second rank key");
	rank_right = rank_left; rank_right.uuid = TestDeviceUuid(11);
	rank_right.device_local_budget_bytes = 512;
	Check(ComparePhysicalDevicePreference(rank_left, rank_right) > 0,
		"larger device-local budget is third rank key");
	rank_right = rank_left; rank_right.uuid = TestDeviceUuid(11);
	rank_right.profile.descriptor_page_tier = 128;
	Check(ComparePhysicalDevicePreference(rank_left, rank_right) > 0,
		"descriptor page tier is fourth rank key");
	rank_right = rank_left; rank_right.uuid = TestDeviceUuid(11);
	rank_right.profile.common_sample_count_mask = 1 | 2;
	Check(ComparePhysicalDevicePreference(rank_left, rank_right) > 0,
		"common attachment sample count is fifth rank key");
	rank_right = rank_left; rank_right.uuid = TestDeviceUuid(11);
	Check(ComparePhysicalDevicePreference(rank_left, rank_right) > 0,
		"lexicographically smaller UUID is final stable tie-break");

	PhysicalDeviceCandidate devices[3] = {
		TestDeviceCandidate(1, 0, PhysicalDeviceType::Discrete, 16384),
		TestDeviceCandidate(2, 1, PhysicalDeviceType::Integrated, 4096),
		TestDeviceCandidate(3, 2, PhysicalDeviceType::Integrated, 2048),
	};
	devices[0].profile.all_required_limits_satisfied = 0;
	DeviceSelectionOverride no_override = {};
	no_override.kind = DeviceSelectionOverrideKind::None;
	PhysicalDeviceSelection selection =
		SelectPhysicalDevice(devices, 3, no_override);
	Check(selection.status == DeviceSelectionStatus::Success &&
		selection.enumeration_index == 1 &&
		PhysicalDeviceUuidEqual(selection.uuid, devices[1].uuid),
		"default selection filters unsupported devices before ranking");

	PhysicalDeviceCandidate reordered[3] = { devices[2], devices[0], devices[1] };
	selection = SelectPhysicalDevice(reordered, 3, no_override);
	Check(selection.status == DeviceSelectionStatus::Success &&
		selection.enumeration_index == 1 &&
		PhysicalDeviceUuidEqual(selection.uuid, devices[1].uuid),
		"default selection is independent of candidate array order");

	DeviceSelectionOverride selection_override = {};
	selection_override.kind = DeviceSelectionOverrideKind::EnumerationIndex;
	selection_override.enumeration_index = 0;
	selection = SelectPhysicalDevice(devices, 3, selection_override);
	Check(selection.status ==
			DeviceSelectionStatus::OverrideRejectedByRequiredProfile &&
		selection.enumeration_index == 0,
		"explicit index override reports profile rejection without fallback");
	selection_override = {};
	selection_override.kind = DeviceSelectionOverrideKind::Uuid;
	selection_override.uuid = devices[2].uuid;
	selection = SelectPhysicalDevice(devices, 3, selection_override);
	Check(selection.status == DeviceSelectionStatus::Success &&
		selection.enumeration_index == 2,
		"explicit UUID override selects the exact eligible device");
	selection_override.uuid = TestDeviceUuid(99);
	Check(SelectPhysicalDevice(devices, 3, selection_override).status ==
		DeviceSelectionStatus::OverrideNotFound,
		"missing UUID override is a hard diagnostic");
	selection_override.uuid = {};
	Check(SelectPhysicalDevice(devices, 3, selection_override).status ==
		DeviceSelectionStatus::InvalidOverride,
		"all-zero UUID override is rejected");

	PhysicalDeviceCandidate duplicate[3] = { devices[0], devices[1], devices[2] };
	duplicate[2].uuid = duplicate[1].uuid;
	Check(SelectPhysicalDevice(duplicate, 3, no_override).status ==
		DeviceSelectionStatus::DuplicateUuid,
		"duplicate stable device identity is rejected");
	duplicate[2] = devices[2]; duplicate[2].enumeration_index = 1;
	Check(SelectPhysicalDevice(duplicate, 3, no_override).status ==
		DeviceSelectionStatus::DuplicateEnumerationIndex,
		"duplicate enumeration index is rejected");
	Check(SelectPhysicalDevice(nullptr, 0, no_override).status ==
		DeviceSelectionStatus::NoRequiredProfileDevice,
		"empty inventory reports no required-profile device");

	CapturedTextureVersion captured_upload = {};
	captured_upload.format = RenderFormat::R8G8B8A8Unorm;
	captured_upload.width = 4; captured_upload.height = 2;
	captured_upload.depth_or_layers = 2; captured_upload.mip_count = 3;
	TextureUploadLayoutInput upload =
		MakeCapturedTextureUploadLayoutInput(captured_upload);
	Check(upload.layer_count == captured_upload.depth_or_layers &&
		upload.payload_alignment == kCapturedTextureUploadPayloadAlignment,
		"captured texture version maps directly to the canonical upload input");
	TextureUploadSubresourceLayout layouts[6] = {};
	TextureUploadLayoutResult built =
		BuildCapturedTextureUploadLayout(upload, layouts, 6);
	Check(built.error == TextureUploadLayoutError::None &&
		built.subresource_count == 6 && built.total_byte_size == 88,
		"captured texture upload manifest size");
	Check(layouts[0].mip_level == 0 && layouts[0].array_layer == 0 &&
		layouts[0].width == 4 && layouts[0].height == 2 &&
		layouts[0].byte_offset == 0 && layouts[0].row_pitch_bytes == 16 &&
		layouts[0].byte_size == 32 &&
		layouts[1].mip_level == 0 && layouts[1].array_layer == 1 &&
		layouts[1].byte_offset == 32 && layouts[2].mip_level == 1 &&
		layouts[2].array_layer == 0 && layouts[2].byte_offset == 64 &&
		layouts[2].width == 2 && layouts[2].height == 1 &&
		layouts[2].row_pitch_bytes == 8 && layouts[2].byte_size == 8 &&
		layouts[4].mip_level == 2 && layouts[4].array_layer == 0 &&
		layouts[4].byte_offset == 80 && layouts[4].width == 1 &&
		layouts[4].height == 1 && layouts[5].byte_offset == 84,
		"mip-major/layer-minor tight top-down texture layout");
	Check(kCapturedTextureUploadSubresourceOrder ==
			TextureUploadSubresourceOrder::MipMajorLayerMinor &&
		kCapturedTextureUploadRowOrder == TextureUploadRowOrder::TopDown,
		"captured upload ordering constants");
	Check(ValidateCapturedTextureUploadLayout(upload, layouts, 6, 88) ==
		TextureUploadLayoutError::None,
		"captured texture upload layout validates");

	TextureUploadSubresourceLayout corrupted[6];
	memcpy(corrupted, layouts, sizeof(corrupted));
	corrupted[3].row_pitch_bytes += 4;
	Check(ValidateCapturedTextureUploadLayout(upload, corrupted, 6, 88) ==
		TextureUploadLayoutError::NonCanonicalSubresource,
		"padded or corrupted row pitch is rejected");
	Check(ValidateCapturedTextureUploadLayout(upload, layouts, 6, 92) ==
		TextureUploadLayoutError::PayloadSizeMismatch,
		"captured payload byte size must equal the exact manifest");
	built = BuildCapturedTextureUploadLayout(upload, layouts, 5);
	Check(built.error == TextureUploadLayoutError::InsufficientOutputCapacity &&
		built.subresource_count == 6 && built.total_byte_size == 88,
		"upload builder reports required capacity without partial layout semantics");
	upload.payload_alignment = 8;
	Check(BuildCapturedTextureUploadLayout(upload, layouts, 6).error ==
		TextureUploadLayoutError::InvalidPayloadAlignment,
		"captured texture payload alignment is exactly four bytes");
	upload = {}; upload.format = RenderFormat::R8Unorm;
	upload.width = upload.height = upload.layer_count = upload.mip_count = 1;
	upload.payload_alignment = kCapturedTextureUploadPayloadAlignment;
	Check(BuildCapturedTextureUploadLayout(upload, layouts, 6).error ==
		TextureUploadLayoutError::PayloadSizeAlignmentMismatch,
		"tight payload size must preserve capture-record alignment");
	upload.format = RenderFormat::R16G16B16A16Sfloat;
	upload.width = UINT32_MAX; upload.height = UINT32_MAX;
	Check(BuildCapturedTextureUploadLayout(upload, layouts, 6).error ==
		TextureUploadLayoutError::ByteSizeOverflow,
		"texture upload byte arithmetic rejects overflow");
	upload.format = RenderFormat::R8G8B8A8Unorm;
	upload.width = 2; upload.height = 1; upload.layer_count = UINT32_MAX;
	upload.mip_count = 2;
	Check(BuildCapturedTextureUploadLayout(upload, layouts, 6).error ==
		TextureUploadLayoutError::SubresourceCountOverflow,
		"texture upload subresource-count arithmetic rejects overflow");
}

static void TestScalarAndCoordinateRules()
{
	struct MsaaCase { uint32_t input; bool aa; uint32_t expected; } cases[] = {
		{0,false,0},{1,false,0},{0,true,4},{1,true,4},{2,false,2},{3,true,2},
		{4,false,4},{7,false,4},{8,false,8},{UINT32_MAX,true,8}
	};
	for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i)
		Check(NormalizeRequestedMsaa(cases[i].input, cases[i].aa) == cases[i].expected, "MSAA normalization");
	Check(SelectSupportedMsaa(8, 1|2|4) == 4, "MSAA downward fallback");
	Check(SelectSupportedMsaa(2, 1|4|8) == 0, "MSAA never upgrades");
	Check(SelectDescriptorPageTier(31) == 0, "descriptor tier rejects below 32");
	Check(SelectDescriptorPageTier(300) == 256, "descriptor tier selection");

	Check(GL4DepthFromEyeZ(-1.0f) == 0.0f, "depth behind clamp");
	Check(GL4DepthFromEyeZ(1.0f) == 0.0f, "depth z=1");
	Check(Near(GL4DepthFromEyeZ(2.0f), 0.5f), "depth z=2");
	Check(Near(GL4DepthFromEyeZ(4.0f), 0.75f), "depth z=4");
	Check(GL4DepthFromEyeZ(std::numeric_limits<float>::infinity()) == 1.0f, "depth infinity");
	Check(GL4DepthFromEyeZ(std::numeric_limits<float>::quiet_NaN()) == 1.0f, "depth NaN oracle");

	const uint32_t expected_masks[] = {1,5,13,15,31,4,12,18};
	for (uint32_t mask = 0; mask < 64; ++mask)
	{
		bool expected = false;
		for (size_t i = 0; i < sizeof(expected_masks)/sizeof(expected_masks[0]); ++i)
			expected |= mask == expected_masks[i];
		Check(IsLegalMrtWriteMask(mask) == expected, "exhaustive MRT masks");
	}

	T1EligibilityInput eligible = {};
	eligible.source = T1SourceKind::OrdinaryRoomBase;
	eligible.all_z_finite_and_positive = 1;
	eligible.payload_representable = 1;
	eligible.retained_range_available = 1;
	Check(EvaluateT1Eligibility(eligible).eligible == 1, "T1 accepted predicate");
	eligible.cc_or = 1;
	Check(EvaluateT1Eligibility(eligible).use_legacy_t0 == 1, "T1 legacy clip fallback");
	eligible.cc_and = 1;
	Check(EvaluateT1Eligibility(eligible).whole_primitive_rejected == 1, "T1 whole rejection");
	eligible.cc_or = eligible.cc_and = 0;
	eligible.retained_range_available = 0;
	Check(EvaluateT1Eligibility(eligible).renderer_invariant_failure == 1, "missing retained range is fatal");

	T0ProjectionResult t0 = ProjectT0(50, 25, 2, 0, 100, 100);
	Check(Near(t0.x_ndc, 0) && Near(t0.y_ndc, -0.5f) && Near(t0.z_ndc, 0.5f), "T0 projection");
	Check(Near(t0.reciprocal_q, 0.5f), "T0 unguarded reciprocal");

	Check(kInterpolationContract[static_cast<size_t>(InterpolationSemantic::BaseUv)].t0 ==
		InterpolationMode::NoperspectiveQPackDivide &&
		kInterpolationContract[static_cast<size_t>(InterpolationSemantic::BaseUv)].t1 ==
		InterpolationMode::SmoothRaw, "T0/T1 UV interpolation distinction");
	Check(kInterpolationContract[static_cast<size_t>(InterpolationSemantic::RawPerPixelSpecularNormal)].t1 ==
		InterpolationMode::NoperspectiveRaw &&
		kInterpolationContract[static_cast<size_t>(InterpolationSemantic::PhongNormal)].t1 ==
		InterpolationMode::SmoothRaw, "raw specular and Phong normal remain distinct");
	const EntryPointCoordinateContract &line =
		kEntryPointCoordinateContract[static_cast<size_t>(LegacyEntryPoint::DrawLine)];
	Check(line.depth_interpretation == DepthInterpretation::AlreadyMapped &&
		line.logical_offset_x == 1 && line.logical_offset_y == 1,
		"ordinary line mapped depth and +1 offset");
	const EntryPointCoordinateContract &special =
		kEntryPointCoordinateContract[static_cast<size_t>(LegacyEntryPoint::DrawSpecialLine)];
	Check(special.depth_source == DepthValueSource::SpecialLineLiteralAlreadyMapped,
		"special-line unguarded depth source");
	Check(kEntryPointCoordinateContract[static_cast<size_t>(LegacyEntryPoint::DrawCircle)].emits_command == 0 &&
		kEntryPointCoordinateContract[static_cast<size_t>(LegacyEntryPoint::FillCircle)].emits_command == 0,
		"circle entry points remain no-ops");

	TerrainVertexMappingInput terrain = {};
	terrain.screen_x=50; terrain.screen_y=25; terrain.viewport_width=100;
	terrain.viewport_height=100; terrain.rotated_eye_z=2;
	terrain.world[0]=2; terrain.world[1]=4; terrain.world[2]=6;
	terrain.base_uv[0]=0.25f; terrain.base_uv[1]=0.5f;
	terrain.lightmap_uv[0]=0.75f; terrain.lightmap_uv[1]=1.0f;
	terrain.texture_page=0x1234; terrain.lightmap_page=3;
	TerrainVertexMappingOutput terrain_out=MapTerrainT2Vertex(terrain);
	Check(Near(terrain_out.base.position[0],0) && Near(terrain_out.base.position[1],-0.5f) &&
		Near(terrain_out.base.position[2],0.5f), "T2 top-left Vulkan projection/depth");
	Check(terrain_out.base.rgba8==0xffffffffu && Near(terrain_out.base.uv0[0],0.125f) &&
		Near(terrain_out.payload.world_q[2],3.0f) && Near(terrain_out.payload.world_q[3],0.5f),
		"T2 q-packed compact payload");
	Check(terrain_out.payload.packed_pages==0x123403u &&
		terrain_out.depth_interpretation==DepthInterpretation::AlreadyMapped,
		"T2 flat material pages and mapped depth");
	Check(kTerrainRotationMask==0xff && kTerrainLightmapPageShift==8 &&
		kTerrainTextureLayerShift==16 && kTerrainPackedPageTextureShift==8,
		"T2 packed field constants");
	Check(kTerrainCellDynamic==0x01 && kTerrainCellSpecialWater==0x04 &&
		kTerrainCellSpecialMine==0x08 && kTerrainCellInvisible==0x10 &&
		kTerrainCellRegionMask==0xe0, "source terrain flags");

	PixelCoordinate nearest=MapNearestPixelCenter({0,0},{2,2},{0,0},{4,4},{4,4});
	Check(nearest.x==1 && nearest.y==1, "nearest pixel-center mapping");
	UvCoordinate uv=MapFullscreenVisibleUv({0,0},{2,2},{2,4},{4,8},{8,16});
	Check(Near(uv.u,0.375f) && Near(uv.v,0.375f), "visible-rectangle center UV");
	WronskiTwoToOneMapping wronski=MapWronskiTwoToOne({0,0},{4,4},{0,0},{4,4});
	Check(Near(wronski.base_uv.u,0.25f) && Near(wronski.visible_uv_min.u,0.125f) &&
		Near(wronski.visible_uv_max.u,0.875f), "Wronski two-to-one centers and clamp");
	PixelCoordinate gtao=MapGtaoReductionOrigin({0,0},{4,4},{2,2});
	Check(gtao.x==0 && gtao.y==0, "GTAO reduction center mapping");
	UvCoordinate suppress=MapVisibleSuppressionUv({2,3},{2,2},{4,4});
	Check(Near(suppress.u,0.125f) && Near(suppress.v,0.375f),
		"visible suppression UV mapping");
	FogRoomAlphaInput fog = {};
	fog.vertex[2]=5; fog.room_plane[2]=1; fog.fog_depth=10;
	fog.room_light_value=0.8f; fog.viewer_inside=1;
	Check(Near(FogRoomVertexAlpha(fog),0.4f), "inside fog-room vertex alpha");
	Check(kFogRoomContract.alpha_type_vertex==4 &&
		kFogRoomContract.depth_write_enabled_during_draw==0 &&
		Near(kFogRoomContract.coplanar_factor,-1), "fog-room sticky state recipe");
}

static void TestSharedCoordinateConversions()
{
	// Exhaust every texel center for small rectangular extents.  These are the
	// boundary values most likely to acquire an off-by-one or half-texel flip.
	for (int32_t height = 1; height <= 8; ++height)
	{
		for (int32_t width = 1; width <= 8; ++width)
		{
			const PixelCoordinate extent = { width, height };
			for (int32_t y = 0; y < height; ++y)
			{
				for (int32_t x = 0; x < width; ++x)
				{
					const UvCoordinate uv = TopLeftPixelCenterToUv({x,y}, extent);
					const FloatPixelCoordinate pixel =
						TopLeftUvToPixelCenter(uv, extent);
					const PixelCoordinate texel =
						TopLeftUvToClampedTexel(uv, extent);
					Check(Near(pixel.x, static_cast<float>(x)) &&
						Near(pixel.y, static_cast<float>(y)),
						"top-left pixel-center UV round trip");
					Check(texel.x == x && texel.y == y,
						"top-left UV texel-fetch round trip");
					const UvCoordinate legacy = TopLeftUvToLegacyGlUv(uv);
					const UvCoordinate direct_legacy =
						TopLeftPixelCenterToLegacyGlUv({x,y}, extent);
					const UvCoordinate canonical = LegacyGlUvToTopLeftUv(legacy);
					Check(Near(canonical.u, uv.u) && Near(canonical.v, uv.v) &&
						Near(direct_legacy.u, legacy.u) &&
						Near(direct_legacy.v, legacy.v),
						"canonical/legacy UV conversion is involutive");
					Check(Near(legacy.v,
						LegacyEquivalentFragmentY(y + 0.5f, height) / height),
						"legacy fragment Y and legacy UV agree");
				}
			}
		}
	}

	Check(TopLeftUvToClampedTexel({-0.25f,-1.0f},{4,3}).x == 0 &&
		TopLeftUvToClampedTexel({-0.25f,-1.0f},{4,3}).y == 0,
		"negative UV texel fetch clamps to first texel");
	Check(TopLeftUvToClampedTexel({1.0f,2.0f},{4,3}).x == 3 &&
		TopLeftUvToClampedTexel({1.0f,2.0f},{4,3}).y == 2,
		"upper UV boundary texel fetch clamps to last texel");
	Check(Near(LegacyEquivalentFragmentY(0.5f, 9), 8.5f) &&
		Near(LegacyEquivalentFragmentY(8.5f, 9), 0.5f),
		"legacy fragment Y preserves both pixel-center boundaries");

	const uint32_t ssaa_factors[] = { 1, 2, 4 };
	for (int32_t target_height = 1; target_height <= 6; ++target_height)
	{
		for (int32_t target_width = 1; target_width <= 6; ++target_width)
		{
			for (int32_t y = 0; y < target_height; ++y)
			for (int32_t x = 0; x < target_width; ++x)
			for (int32_t rect_height = 1; rect_height <= target_height-y; ++rect_height)
			for (int32_t rect_width = 1; rect_width <= target_width-x; ++rect_width)
			{
				const LogicalRect visible = {x,y,rect_width,rect_height};
				for (size_t f = 0; f < sizeof(ssaa_factors)/sizeof(ssaa_factors[0]); ++f)
				{
					const int32_t scale = static_cast<int32_t>(ssaa_factors[f]);
					const VisibleRectCoordinateSet coordinates =
						BuildVisibleRectCoordinateSet(visible,
							{target_width,target_height}, ssaa_factors[f]);
					Check(coordinates.logical_top_left.x == x &&
						coordinates.logical_top_left.y == y &&
						coordinates.ssaa_top_left.x == x*scale &&
						coordinates.ssaa_top_left.y == y*scale &&
						coordinates.ssaa_top_left.width == rect_width*scale &&
						coordinates.ssaa_top_left.height == rect_height*scale,
						"visible rectangle scales in canonical SSAA space");
					Check(coordinates.legacy_gl_ssaa_bottom_left.y ==
						(target_height-y-rect_height)*scale &&
						coordinates.legacy_gl_ssaa_bottom_left.x == x*scale,
						"visible rectangle converts to pinned GL4 bottom origin");
					Check(coordinates.post_top_left.x == 0 &&
						coordinates.post_top_left.y == 0 &&
						coordinates.post_top_left.width == rect_width &&
						coordinates.post_top_left.height == rect_height,
						"visible rectangle becomes full logical post target");
					const LogicalRect flipped_back = TopLeftRectToLegacyGlBottomLeft(
						coordinates.legacy_gl_ssaa_bottom_left,
						{target_width*scale,target_height*scale});
					Check(flipped_back.y == coordinates.ssaa_top_left.y,
						"rectangle Y conversion is involutive");
				}

				const UvTransform transform = VisibleRectUvTransform(visible,
					{target_width,target_height});
				const UvCoordinate uv0 = ApplyUvTransform({0.0f,0.0f}, transform);
				const UvCoordinate uv1 = ApplyUvTransform({1.0f,1.0f}, transform);
				Check(Near(uv0.u, static_cast<float>(x)/target_width) &&
					Near(uv0.v, static_cast<float>(y)/target_height) &&
					Near(uv1.u, static_cast<float>(x+rect_width)/target_width) &&
					Near(uv1.v, static_cast<float>(y+rect_height)/target_height),
					"visible-rectangle UV transform endpoints");
				const LogicalRect legacy_visible =
					TopLeftRectToLegacyGlBottomLeft(visible,
						{target_width,target_height});
				for (int32_t local_y = 0; local_y < rect_height; ++local_y)
				for (int32_t local_x = 0; local_x < rect_width; ++local_x)
				{
					const PixelCoordinate canonical_pixel =
						{x+local_x,y+local_y};
					const PixelCoordinate legacy_pixel = {canonical_pixel.x,
						target_height-canonical_pixel.y-1};
					const UvCoordinate canonical_visible_uv =
						MapVisibleSuppressionUv(canonical_pixel,{x,y},
							{rect_width,rect_height});
					const UvCoordinate legacy_visible_uv = {
						(legacy_pixel.x-legacy_visible.x+0.5f)/rect_width,
						(legacy_pixel.y-legacy_visible.y+0.5f)/rect_height };
					const UvCoordinate converted_visible_uv =
						LegacyGlUvToTopLeftUv(legacy_visible_uv);
					Check(Near(canonical_visible_uv.u,converted_visible_uv.u) &&
						Near(canonical_visible_uv.v,converted_visible_uv.v),
						"canonical visible UV matches pinned GL4 rectangle mapping");
				}
			}
		}
	}

	// The odd vertical remainder is deliberately asymmetric in GL4: bottom
	// gets floor(remainder/2), so canonical top gets ceil(remainder/2).
	const VisibleRectCoordinateSet odd_overscan =
		BuildVisibleRectCoordinateSet({2,2,7,8},{11,11},2);
	Check(odd_overscan.ssaa_top_left.y == 4 &&
		odd_overscan.legacy_gl_ssaa_bottom_left.y == 2,
		"odd overscan retains distinct top and legacy-bottom origins");

	for (int32_t y = 0; y < 6; ++y)
	for (int32_t x = 0; x < 5; ++x)
	{
		const LogicalRect visible = {1,2,8,8};
		const UvCoordinate phase = MapLegacyGtaoNoisePosition({x,y},
			{5,6}, visible, {10,12});
		const float legacy_bottom = 12.0f-visible.y-visible.height;
		const float expected_x = ((x+0.5f)/5.0f)*5.0f-
			visible.x*5.0f/10.0f;
		const float expected_y = (1.0f-(y+0.5f)/6.0f)*6.0f-
			legacy_bottom*6.0f/12.0f;
		Check(Near(phase.u, expected_x) && Near(phase.v, expected_y),
			"GTAO noise phase matches pinned lower-left operations");
		const UvCoordinate noise_uv = MapLegacyGtaoNoiseUv({x,y},
			{5,6}, visible, {10,12});
		Check(Near(noise_uv.u, phase.u/4.0f) &&
			Near(noise_uv.v, phase.v/4.0f),
			"GTAO 4x4 repeat UV follows legacy phase");
	}

	const float velocity_values[] = {-0.75f,-0.25f,0.0f,0.25f,0.75f};
	for (size_t y = 0; y < sizeof(velocity_values)/sizeof(velocity_values[0]); ++y)
	for (size_t x = 0; x < sizeof(velocity_values)/sizeof(velocity_values[0]); ++x)
	{
		const UvCoordinate stored = {velocity_values[x],velocity_values[y]};
		const UvCoordinate canonical = LegacyStoredVelocityToCanonical(stored);
		const UvCoordinate round_trip = CanonicalVelocityToLegacyStored(canonical);
		Check(Near(canonical.u, stored.u) && Near(canonical.v, -stored.v) &&
			Near(round_trip.u, stored.u) && Near(round_trip.v, stored.v),
			"legacy velocity Y conversion and inverse");
	}

	HistoryReprojectionCoordinate history = ReprojectHistoryCanonical(
		{0.25f,0.75f},{0.10f,0.20f},{10,10});
	Check(Near(history.canonical_velocity.u,0.10f) &&
		Near(history.canonical_velocity.v,-0.20f) &&
		Near(history.previous_uv.u,0.15f) && Near(history.previous_uv.v,0.95f) &&
		history.nearest_texel.x == 1 && history.nearest_texel.y == 9 &&
		history.in_bounds == 1, "canonical history reprojection and texel fetch");
	history = ReprojectHistoryCanonical({0.0f,1.0f},{0.0f,0.0f},{8,8});
	Check(history.in_bounds == 1 && history.nearest_texel.x == 0 &&
		history.nearest_texel.y == 7, "history inclusive UV boundary");
	history = ReprojectHistoryCanonical({0.05f,0.95f},{0.10f,-0.10f},{8,8});
	Check(history.in_bounds == 0 && history.nearest_texel.x == 0,
		"history rejects an out-of-bounds reprojection before clamped fetch");

	LogicalRect present = ComputeCanonicalPresentRect({640,480},{1920,1080});
	Check(present.x == 240 && present.y == 0 && present.width == 1440 &&
		present.height == 1080, "GL4 parity pillarbox rectangle");
	present = ComputeCanonicalPresentRect({1600,900},{1001,600});
	Check(present.x == 0 && present.y == 19 && present.width == 1001 &&
		present.height == 563, "odd letterbox remainder converts to canonical top");
	const UvCoordinate first = MapPresentPixelToSourceUv(
		{present.x,present.y}, present);
	const UvCoordinate last = MapPresentPixelToSourceUv(
		{present.x+present.width-1,present.y+present.height-1}, present);
	Check(Near(first.u,0.5f/present.width) &&
		Near(first.v,0.5f/present.height) &&
		Near(last.u,1.0f-0.5f/present.width) &&
		Near(last.v,1.0f-0.5f/present.height),
		"present rectangle maps exact source texel-center boundaries");
	present = ComputeCanonicalPresentRect({1000,1000},{1000,1001});
	Check(present.x == 0 && present.y == 0 && present.width == 1000 &&
		present.height == 1001, "GL4 0.001 aspect tolerance keeps full present rect");
	present = ComputeCanonicalPresentRect({640,480},{0,0});
	Check(present.x == 0 && present.y == 0 && present.width == 0 &&
		present.height == 0, "zero drawable extent pauses with an empty present rect");
}

static CapturedTextureVersion DiagnosticTexture()
{
	CapturedTextureVersion version = {};
	version.id = 0;
	version.format = RenderFormat::R8G8B8A8Unorm;
	version.width = version.height = version.depth_or_layers = version.mip_count = 1;
	version.immutable_upload_payload = kInvalidId;
	return version;
}

static CapturedTargetLayout MakeTargetLayout(RenderTargetClass target,
	uint32_t width, uint32_t height, uint32_t attachment_mask)
{
	CapturedTargetLayout layout = {};
	layout.target = target;
	layout.logical_width = layout.internal_width = layout.drawable_width = width;
	layout.logical_height = layout.internal_height = layout.drawable_height = height;
	layout.ssaa_factor = 1;
	layout.msaa_samples = 1;
	layout.attachment_mask = attachment_mask;
	for (size_t i = 0; i < 6; ++i)
		layout.attachment_formats[i] = kSceneAttachmentContract[i].format;
	layout.overscan_percent = 100;
	layout.present_format = RenderFormat::R8G8B8A8Unorm;
	return layout;
}

static void TestCapture()
{
	RenderCaptureSegment segment;
	CaptureReserve reserve = {};
	reserve.commands=8; reserve.states=4; reserve.materials=4; reserve.texture_versions=2;
	reserve.transforms=4; reserve.views=2; reserve.viewports=2; reserve.target_layouts=3;
	reserve.target_signatures=2; reserve.target_versions=2; reserve.present_rects=2;
	reserve.wsi_signatures=2;
	reserve.payload_bindings=4; reserve.payload_records=8; reserve.payload_bytes=256;
	reserve.stream_vertices=8; reserve.stream_indices=12; reserve.stream_payload_words=16;
	segment.Reserve(reserve);
	Check(segment.Reset(7, 2, 100), "capture reset starts segment");
	Check(segment.RegisterTextureVersion(DiagnosticTexture()), "register diagnostic texture");

	CapturedViewport viewport = {};
	viewport.logical_rect = {0,0,640,480}; viewport.physical_rect = viewport.logical_rect;
	viewport.target_width=640; viewport.target_height=480; viewport.ssaa_factor=1;
	ViewportId viewport_id = segment.InternViewport(viewport);
	CapturedTargetLayout layout = MakeTargetLayout(RenderTargetClass::Scene, 640, 480,
		kTargetAttachmentAll);
	TargetLayoutId layout_id = segment.InternTargetLayout(layout);
	TargetLayoutId post_layout_id = segment.InternTargetLayout(
		MakeTargetLayout(RenderTargetClass::PostPresent, 640, 480,
			kTargetAttachmentColor));
	TargetLayoutId cockpit_layout_id = segment.InternTargetLayout(
		MakeTargetLayout(RenderTargetClass::CockpitScene, 640, 480,
			kTargetAttachmentMandatory));
	CapturedWorldView view = {};
	view.logical_clip = viewport.logical_rect;
	ViewStateId view_id = segment.InternView(view);
	CapturedWorldView updated_view = view;
	updated_view.projection[0] = 2.0f;
	Check(segment.ReplaceView(view_id, updated_view) &&
		segment.Views()[view_id].projection[0] == 2.0f,
		"capturing segment refreshes stable view identity");
	CapturedTargetSignature signature = {};
	signature.target_layout = layout_id;
	signature.post_present_layout = post_layout_id;
	signature.cockpit_scene_layout = cockpit_layout_id;
	signature.preferred.width=640; signature.preferred.height=480;
	signature.preferred.window_width=640; signature.preferred.window_height=480;
	signature.preferred.supersampling_factor=1;
	RenderTargetSignatureId signature_id = segment.InternTargetSignature(signature);
	CapturedTargetVersion target_version = {};
	target_version.target=RenderTargetClass::Scene; target_version.target_layout=layout_id;
	target_version.width=640; target_version.height=480; target_version.samples=1;
	TargetVersionId target_version_id = segment.InternTargetVersion(target_version);
	CapturedPresentRect present_rect = {};
	present_rect.drawable_width=640; present_rect.drawable_height=480;
	present_rect.rect={0,0,640,480};
	present_rect.surface_transform=1; present_rect.swapchain_generation=12;
	PresentRectId present_rect_id = segment.InternPresentRect(present_rect);
	CapturedWsiSignature wsi = {};
	wsi.swapchain_generation=12;
	wsi.format=SurfacePixelFormat::B8G8R8A8Unorm;
	wsi.color_space=SurfaceColorSpace::SrgbNonlinear;
	wsi.present_mode=PresentModeContract::Immediate;
	wsi.composite_alpha=CompositeAlphaContract::Opaque;
	wsi.surface_transform=1; wsi.drawable_width=640; wsi.drawable_height=480;
	wsi.image_count=3; wsi.graphics_queue_family=0; wsi.present_queue_family=0;
	wsi.safe_authored_unorm=1;
	WsiSignatureId wsi_id=segment.InternWsiSignature(wsi);

	CapturedShaderRasterState state = {};
	state.target_layout=layout_id; state.sample_count=1; state.mrt_write_mask=kWriteColor;
	state.shader.shader_flags=kShaderSoftParticle;
	state.shader.state_flags2=kStateSeparateSoftDepthScalar;
	state.raster_family=RasterFamily::Ordinary; state.viewport=viewport_id; state.scissor=viewport_id;
	StateId state0 = segment.InternState(state);
	Check(segment.InternState(state) == state0, "state interning");
	state.scissor = segment.InternViewport(CapturedViewport{});
	Check(segment.InternState(state) == 1, "tail state field changes ID");

	CapturedMaterial material = {};
	for (int i=0;i<4;++i) material.image2d[i]=material.image2d_array[i]=0;
	MaterialRef material0=segment.InternMaterial(material);
	Check(segment.InternMaterial(material)==material0, "material interning");
	material.uv_params[3]=1;
	Check(segment.InternMaterial(material)==1, "tail material field changes ID");
	CapturedTransform transform = {};
	TransformId transform0=segment.InternTransform(transform);
	transform.previous_model[15]=1;
	Check(segment.InternTransform(transform)==1, "tail transform field changes ID");

	float soft=0.25f;
	PayloadDataId soft_id=segment.CopyPayloadData(&soft,sizeof(soft),4,kPayloadSoftDepthScalar);
	MotionVertexPayload motion[3] = {};
	motion[0].current_q[0]=11;
	PayloadDataId motion_id=segment.CopyPayloadData(motion,sizeof(motion),16,kPayloadMotionVertices);
	Check(segment.PayloadRecords()[motion_id].byte_offset==16, "payload AlignUp padding");
	for (uint32_t i=sizeof(float);i<16;++i) Check(segment.PayloadBytes()[i]==0,"payload padding zeroed");
	motion[0].current_q[0]=99;
	Check(reinterpret_cast<const MotionVertexPayload*>(segment.PayloadBytes().data()+16)->current_q[0]==11,
		"payload deep copy");
	auto external_owner = std::make_shared<std::vector<uint8_t>>(4, 0);
	(*external_owner)[0] = 73;
	std::shared_ptr<const std::vector<uint8_t>> external_immutable = external_owner;
	std::weak_ptr<const std::vector<uint8_t>> external_lifetime = external_immutable;
	const size_t local_payload_bytes = segment.PayloadBytes().size();
	PayloadDataId external_id = segment.ReferencePayloadData(external_immutable, 4,
		kPayloadTextureUpload);
	Check(external_id != kInvalidId &&
		segment.PayloadBytes().size() == local_payload_bytes &&
		segment.PayloadData(external_id) && segment.PayloadData(external_id)[0] == 73,
		"capture references immutable resource payload without cloning it");
	external_owner.reset();
	external_immutable.reset();
	Check(!external_lifetime.expired(),
		"capture keeps referenced resource payload alive");
	CapturedCockpitBackingEffect backing = {};
	PayloadDataId backing_id=segment.CopyPayloadData(&backing,sizeof(backing),4,
		kPayloadCockpitBacking);
	Check(segment.CopyPayloadData(&soft,sizeof(soft),3,kPayloadSoftDepthScalar)==kInvalidId,
		"invalid payload alignment rejected");
	CapturedPayloadBinding binding=EmptyPayloadBinding();
	binding.motion_vertices=motion_id; binding.soft_depth_scalar=soft_id;
	binding.validity_flags=kPayloadHasMotionVertices|kPayloadHasSoftDepthScalar;
	PayloadRef payload=segment.InternPayloadBinding(binding);
	Check(payload==0 && segment.InternPayloadBinding(binding)==0,"payload binding interning");

	BaseVertex vertices[3] = {}; vertices[0].position[0]=5;
	uint32_t indices[3]={0,1,2}; uint32_t words[2]={41,42};
	StreamGeometryRef geometry=segment.CopyStreamGeometry(vertices,3,indices,3,words,2,
		DepthInterpretation::EyeZLegacyMapped);
	vertices[0].position[0]=100; indices[0]=2; words[0]=0;
	Check(segment.StreamVertices()[0].position[0]==5 && segment.StreamIndices()[0]==0 &&
		segment.StreamPayloadWords()[0]==41,"geometry deep copy");
	StreamGeometryRef geometry2=segment.CopyStreamGeometry(vertices,3,indices,3,words,2,
		DepthInterpretation::AlreadyMapped);
	Check(geometry2.vertices.offset==3 && geometry2.indices.offset==3 &&
		geometry2.optional_payload_words.offset==2,"second geometry offsets");

	CaptureCommand command = {};
	command.schema_version=kCaptureSchemaVersion; command.type=CaptureCommandType::BeginFrameTarget;
	command.serial=999; command.payload.begin_frame_target.target=RenderTargetClass::Scene;
	command.payload.begin_frame_target.logical_clip=viewport.logical_rect;
	command.payload.begin_frame_target.physical_viewport=viewport_id;
	command.payload.begin_frame_target.view_state=view_id;
	command.payload.begin_frame_target.active_target_version=target_version_id;
	Check(segment.AppendCopy(command),"append begin frame");
	Check(segment.Commands().back().serial==100,"caller serial replaced");
	command = {}; command.schema_version=kCaptureSchemaVersion; command.type=CaptureCommandType::DrawStream;
	command.payload.draw_stream.geometry=geometry; command.payload.draw_stream.state=state0;
	command.payload.draw_stream.transform=transform0; command.payload.draw_stream.material=material0;
	command.payload.draw_stream.optional_payload=payload; command.payload.draw_stream.classification.source_kind=PrimitiveSourceKind::PolygonFan;
	Check(segment.AppendCopy(command),"append stream draw");
	command = {}; command.schema_version=kCaptureSchemaVersion; command.type=CaptureCommandType::CaptureBloomSource;
	command.payload.capture_bloom_source.scene_target_version=target_version_id;
	command.payload.capture_bloom_source.projection=view_id;
	command.payload.capture_bloom_source.view_projection=view_id;
	command.payload.capture_bloom_source.inverse_modelview=view_id;
	command.payload.capture_bloom_source.visible_rect=viewport.logical_rect;
	Check(segment.AppendCopy(command),"append capture boundary");
	command = {}; command.schema_version=kCaptureSchemaVersion; command.type=CaptureCommandType::EndFrame;
	command.payload.end_frame.view_interval_serial=1;
	Check(segment.AppendCopy(command),"append end frame");
	command = {}; command.schema_version=kCaptureSchemaVersion; command.type=CaptureCommandType::BeginPostPresent;
	command.payload.begin_post_present.signature=signature_id;
	Check(segment.AppendCopy(command),"append post begin");
	command = {}; command.schema_version=kCaptureSchemaVersion; command.type=CaptureCommandType::BeginCockpitScene;
	command.payload.begin_cockpit_scene.logical_rect=viewport.logical_rect;
	command.payload.begin_cockpit_scene.backing_effect_state=backing_id;
	command.payload.begin_cockpit_scene.capture_serial=55;
	Check(segment.AppendCopy(command),"append cockpit begin with matching identity");
	command = {}; command.schema_version=kCaptureSchemaVersion; command.type=CaptureCommandType::EndCockpitScene;
	command.payload.end_cockpit_scene.capture_serial=55;
	Check(segment.AppendCopy(command),"append matching cockpit end");
	command = {}; command.schema_version=kCaptureSchemaVersion; command.type=CaptureCommandType::Present;
	command.payload.present.presented_frame_serial=7;
	command.payload.present.window_swapchain_signature=wsi_id;
	command.payload.present.present_rect=present_rect_id;
	Check(segment.AppendCopy(command),"append present");
	CaptureCommand invalid={}; invalid.schema_version=kCaptureSchemaVersion+1; invalid.type=CaptureCommandType::Present;
	Check(!segment.AppendCopy(invalid),"invalid schema rejected");

	CaptureValidationResult validation={};
	Check(segment.Validate(&validation) && validation.errors==0,"complete segment validates");
	Check(segment.Freeze(&validation),"validated freeze");
	Check(!segment.Reset(8,0,0),"frozen segment cannot reset");
	Check(segment.MarkCompiled(),"compiled handoff");
	Check(segment.Reset(8,0,0),"compiled segment reusable");
	Check(segment.Commands().empty() && segment.States().empty() && segment.Materials().empty() &&
		segment.TextureVersions().empty() && segment.Views().empty() && segment.Viewports().empty() &&
		segment.TargetLayouts().empty() && segment.TargetSignatures().empty() &&
		segment.TargetVersions().empty() && segment.PresentRects().empty() &&
		segment.WsiSignatures().empty() &&
		segment.PayloadBindings().empty() && segment.PayloadRecords().empty() &&
		segment.PayloadBytes().empty() && segment.StreamVertices().empty() &&
		segment.StreamIndices().empty() && segment.StreamPayloadWords().empty(),"reset clears all tables");
	Check(external_lifetime.expired(),
		"capture reset releases referenced resource payload");
}

static void TestCaptureRejectsTargetMismatch()
{
	RenderCaptureSegment segment;
	Check(segment.Reset(1, 0, 0), "target-mismatch capture reset");
	CapturedViewport viewport = {};
	viewport.logical_rect={0,0,64,64}; viewport.physical_rect=viewport.logical_rect;
	viewport.target_width=64; viewport.target_height=64; viewport.ssaa_factor=1;
	ViewportId viewport_id=segment.InternViewport(viewport);
	CapturedTargetLayout layout = MakeTargetLayout(RenderTargetClass::Scene, 64, 64,
		kTargetAttachmentMandatory);
	TargetLayoutId layout_id=segment.InternTargetLayout(layout);
	CapturedShaderRasterState state = {};
	state.target_layout=layout_id; state.sample_count=1;
	state.mrt_write_mask=kWriteColor|kWriteVelocity|kWriteProtectionMask|kWriteAoClass;
	state.raster_family=RasterFamily::Ordinary; state.viewport=viewport_id; state.scissor=viewport_id;
	segment.InternState(state);
	CaptureValidationResult validation = {};
	Check(!segment.Validate(&validation) &&
		(validation.errors & kCaptureInvalidTargetRelation) != 0,
		"capture rejects writes to omitted physical attachment");

	RenderCaptureSegment missing_mandatory;
	Check(missing_mandatory.Reset(1,0,0), "missing-attachment capture reset");
	layout.attachment_mask=0;
	missing_mandatory.InternTargetLayout(layout);
	Check(!missing_mandatory.Validate(&validation) &&
		(validation.errors & kCaptureInvalidTargetRelation) != 0,
		"capture rejects target without mandatory color/depth");
}

static void TestCaptureRejectsGrammarGeometryAndRects()
{
	RenderCaptureSegment empty;
	CaptureValidationResult validation = {};
	Check(empty.Reset(1,0,0), "empty capture reset");
	Check(!empty.Freeze(&validation) &&
		(validation.errors & kCaptureInvalidCommandGrammar) != 0,
		"empty capture cannot freeze");

	RenderCaptureSegment segment;
	Check(segment.Reset(3,0,0), "invalid-geometry capture reset");
	segment.RegisterTextureVersion(DiagnosticTexture());
	CapturedViewport viewport = {};
	viewport.logical_rect={0,0,64,64}; viewport.physical_rect=viewport.logical_rect;
	viewport.target_width=64; viewport.target_height=64; viewport.ssaa_factor=1;
	ViewportId viewport_id=segment.InternViewport(viewport);
	CapturedWorldView view = {}; view.logical_clip=viewport.logical_rect;
	ViewStateId view_id=segment.InternView(view);
	CapturedTargetLayout layout = MakeTargetLayout(RenderTargetClass::Scene, 64, 64,
		kTargetAttachmentAll);
	TargetLayoutId layout_id=segment.InternTargetLayout(layout);
	TargetLayoutId post_layout_id=segment.InternTargetLayout(
		MakeTargetLayout(RenderTargetClass::PostPresent,64,64,kTargetAttachmentColor));
	TargetLayoutId cockpit_layout_id=segment.InternTargetLayout(
		MakeTargetLayout(RenderTargetClass::CockpitScene,64,64,kTargetAttachmentMandatory));
	CapturedTargetSignature signature = {}; signature.target_layout=layout_id;
	signature.post_present_layout=post_layout_id;
	signature.cockpit_scene_layout=cockpit_layout_id;
	signature.preferred.width=signature.preferred.window_width=64;
	signature.preferred.height=signature.preferred.window_height=64;
	signature.preferred.supersampling_factor=1;
	RenderTargetSignatureId signature_id=segment.InternTargetSignature(signature);
	CapturedTargetVersion scene_version = {};
	scene_version.target=RenderTargetClass::Scene; scene_version.target_layout=layout_id;
	scene_version.width=scene_version.height=64; scene_version.samples=1;
	TargetVersionId scene_version_id=segment.InternTargetVersion(scene_version);
	CapturedShaderRasterState state = {};
	state.target_layout=layout_id; state.sample_count=1; state.mrt_write_mask=kWriteColor;
	state.raster_family=RasterFamily::Ordinary; state.viewport=viewport_id; state.scissor=viewport_id;
	StateId state_id=segment.InternState(state);
	CapturedMaterial material = {};
	MaterialRef material_id=segment.InternMaterial(material);
	TransformId transform_id=segment.InternTransform(CapturedTransform{});
	BaseVertex vertices[3] = {}; uint32_t bad_indices[3]={0,1,3};
	StreamGeometryRef geometry=segment.CopyStreamGeometry(vertices,3,bad_indices,3,nullptr,0,
		DepthInterpretation::AlreadyMapped);
	CapturedPresentRect rect = {}; rect.drawable_width=64; rect.drawable_height=64;
	rect.rect={0,0,64,64}; rect.surface_transform=1; rect.swapchain_generation=4;
	PresentRectId rect_id=segment.InternPresentRect(rect);
	CapturedWsiSignature wsi = {}; wsi.format=SurfacePixelFormat::B8G8R8A8Unorm;
	wsi.swapchain_generation=4;
	wsi.color_space=SurfaceColorSpace::SrgbNonlinear; wsi.present_mode=PresentModeContract::Immediate;
	wsi.composite_alpha=CompositeAlphaContract::Opaque; wsi.surface_transform=1;
	wsi.drawable_width=64; wsi.drawable_height=64; wsi.image_count=3;
	wsi.graphics_queue_family=0; wsi.present_queue_family=0; wsi.safe_authored_unorm=1;
	WsiSignatureId wsi_id=segment.InternWsiSignature(wsi);

	CaptureCommand command = {}; command.schema_version=kCaptureSchemaVersion;
	command.type=CaptureCommandType::BeginFrameTarget;
	command.payload.begin_frame_target.target=RenderTargetClass::Scene;
	command.payload.begin_frame_target.logical_clip=viewport.logical_rect;
	command.payload.begin_frame_target.physical_viewport=viewport_id;
	command.payload.begin_frame_target.view_state=view_id;
	command.payload.begin_frame_target.active_target_version=scene_version_id;
	segment.AppendCopy(command);
	command={}; command.schema_version=kCaptureSchemaVersion; command.type=CaptureCommandType::ClearColor;
	command.payload.clear_color.target=RenderTargetClass::Scene;
	command.payload.clear_color.rect={60,60,8,8};
	command.payload.clear_color.selected_attachments=kWriteColor;
	command.payload.clear_color.attachment_channel_masks[0]=kChannelRgba;
	segment.AppendCopy(command);
	command={}; command.schema_version=kCaptureSchemaVersion; command.type=CaptureCommandType::DrawStream;
	command.payload.draw_stream.geometry=geometry; command.payload.draw_stream.state=state_id;
	command.payload.draw_stream.transform=transform_id; command.payload.draw_stream.material=material_id;
	command.payload.draw_stream.optional_payload=kInvalidId;
	command.payload.draw_stream.classification.source_kind=PrimitiveSourceKind::PolygonFan;
	segment.AppendCopy(command);
	command={}; command.schema_version=kCaptureSchemaVersion; command.type=CaptureCommandType::EndFrame;
	command.payload.end_frame.view_interval_serial=1; segment.AppendCopy(command);
	command={}; command.schema_version=kCaptureSchemaVersion; command.type=CaptureCommandType::BeginPostPresent;
	command.payload.begin_post_present.signature=signature_id; segment.AppendCopy(command);
	command={}; command.schema_version=kCaptureSchemaVersion; command.type=CaptureCommandType::Present;
	command.payload.present.presented_frame_serial=3;
	command.payload.present.window_swapchain_signature=wsi_id;
	command.payload.present.present_rect=rect_id; segment.AppendCopy(command);
	Check(!segment.Validate(&validation) &&
		(validation.errors & kCaptureInvalidGeometry) != 0 &&
		(validation.errors & kCaptureInvalidRect) != 0,
		"capture rejects out-of-range stream index and clear rect");
}

static CaptureContinuationState SceneContinuationState()
{
	CaptureContinuationState state = {};
	state.schema_version = kCaptureContinuationSchemaVersion;
	state.active_target = RenderTargetClass::Scene;
	state.logical_clip = {0,0,64,64};
	state.active_attachment_mask = kTargetAttachmentMandatory;
	state.load_attachment_mask = state.active_attachment_mask;
	state.active_target_version = 0;
	state.color_epoch = 3;
	state.depth_epoch = 5;
	state.prior_submitted_timeline = 11;
	state.resource_state_snapshot_serial = 23;
	return state;
}

static TargetVersionId InstallContinuationTarget(RenderCaptureSegment &segment,
	const CaptureContinuationState &state)
{
	const CapturedTargetLayout layout=MakeTargetLayout(state.active_target,64,64,
		state.active_attachment_mask);
	const TargetLayoutId layout_id=segment.InternTargetLayout(layout);
	CapturedTargetVersion version = {};
	version.target=state.active_target; version.version=1;
	version.target_layout=layout_id; version.width=64; version.height=64;
	version.samples=1; version.color_epoch=state.color_epoch;
	version.depth_epoch=state.depth_epoch;
	const TargetVersionId id=segment.InternTargetVersion(version);
	Check(id==state.active_target_version,
		"continuation TargetVersionId resolves the exact inherited target");
	return id;
}

static void TestTraceContinuationReplayContract()
{
	RenderCaptureSegment continuation;
	const CaptureContinuationState continuation_state=SceneContinuationState();
	Check(continuation.ResetContinuation(91,3,700,continuation_state),
		"trace source continuation reset");
	InstallContinuationTarget(continuation,continuation_state);
	TraceSegmentStartRecord start=MakeTraceSegmentStartRecord(continuation);
	Check(start.start_kind==CaptureSegmentStartKind::ContinuationAfterReadback &&
		start.continuation_state.prior_submitted_timeline==11 &&
		start.continuation_state.resource_state_snapshot_serial==23 &&
		TraceSegmentStartMatchesCapture(start,continuation),
		"trace record copies the complete continuation handoff");

	TraceFileHeader header = {};
	const uint8_t magic[8]={'P','I','C','C','U','R','T','R'};
	memcpy(header.magic,magic,sizeof(magic));
	header.trace_file_version=kTraceFileVersion;
	header.renderer_capabilities_abi_version=RENDERER_CAPABILITIES_ABI_VERSION;
	header.render_contract_version=kRenderContractVersion;
	header.capture_schema_version=kCaptureSchemaVersion;
	header.shader_abi_version=kShaderAbiVersion;
	memcpy(header.oracle_commit,kPinnedOracleCommit,sizeof(kPinnedOracleCommit));
	header.backend=RENDERER_BACKEND_VULKAN;
	header.presented_frame_serial=continuation.PresentedFrameSerial();
	header.segment_serial=continuation.SegmentSerial();
	header.segment_start_kind=continuation.StartKind();
	header.segment_start_table_index=
		static_cast<uint32_t>(TraceTableKind::SegmentStartStates);
	header.continuation_state_schema_version=
		start.continuation_state.schema_version;
	header.command_count=0;
	header.table_directory_count=static_cast<uint32_t>(TraceTableKind::Count);
	header.table_directory_offset=kTraceFileHeaderSerializedSize;
	const uint64_t table_data_offset=header.table_directory_offset+
		uint64_t(header.table_directory_count)*
		kTraceTableDirectoryEntrySerializedSize;
	header.file_size=table_data_offset+sizeof(CapturedTargetVersion)+
		kTraceSegmentStartRecordSerializedSize;

	TraceTableDirectoryEntry directory[
		static_cast<size_t>(TraceTableKind::Count)] = {};
	for(size_t i=0;i<static_cast<size_t>(TraceTableKind::Count);++i)
	{
		directory[i].kind=static_cast<TraceTableKind>(i);
		directory[i].file_offset=table_data_offset;
	}
	directory[static_cast<size_t>(TraceTableKind::Commands)].element_stride=
		sizeof(CaptureCommand);
	TraceTableDirectoryEntry &target_versions=directory[
		static_cast<size_t>(TraceTableKind::TargetVersions)];
	target_versions.element_stride=sizeof(CapturedTargetVersion);
	target_versions.element_count=1;
	target_versions.byte_size=sizeof(CapturedTargetVersion);
	TraceTableDirectoryEntry &start_table=directory[
		static_cast<size_t>(TraceTableKind::SegmentStartStates)];
	start_table.file_offset=table_data_offset+sizeof(CapturedTargetVersion);
	start_table.element_stride=kTraceSegmentStartRecordSerializedSize;
	start_table.element_count=1;
	start_table.byte_size=kTraceSegmentStartRecordSerializedSize;

	TraceValidationResult validation = {};
	Check(ValidateTraceFileContract(header,directory,
		static_cast<size_t>(TraceTableKind::Count),&start,1,
		&continuation,&validation),
		"continuation trace is independently replayable");

	TraceSegmentStartRecord altered=start;
	++altered.continuation_state.color_epoch;
	Check(!ValidateTraceFileContract(header,directory,
		static_cast<size_t>(TraceTableKind::Count),&altered,1,
		&continuation,&validation) &&
		(validation.errors&kTraceSegmentStartMismatch)!=0,
		"trace rejects a continuation state different from capture");

	TraceFileHeader malformed=header;
	malformed.segment_start_table_index=
		static_cast<uint32_t>(TraceTableKind::Count);
	Check(!ValidateTraceFileContract(malformed,directory,
		static_cast<size_t>(TraceTableKind::Count),&start,1,
		&continuation,&validation) &&
		(validation.errors&kTraceInvalidSegmentStart)!=0,
		"trace rejects an out-of-range segment-start table index");

	TraceTableDirectoryEntry malformed_directory[
		static_cast<size_t>(TraceTableKind::Count)] = {};
	memcpy(malformed_directory,directory,sizeof(directory));
	malformed_directory[static_cast<size_t>(TraceTableKind::SegmentStartStates)].
		element_count=2;
	Check(!ValidateTraceFileContract(header,malformed_directory,
		static_cast<size_t>(TraceTableKind::Count),&start,1,
		&continuation,&validation) &&
		(validation.errors&(kTraceInvalidSegmentStart|kTraceInvalidTableShape))!=0,
		"trace requires exactly one correctly sized segment-start record");

	memcpy(malformed_directory,directory,sizeof(directory));
	malformed_directory[static_cast<size_t>(TraceTableKind::TargetVersions)].
		element_count=0;
	malformed_directory[static_cast<size_t>(TraceTableKind::TargetVersions)].
		byte_size=0;
	Check(!ValidateTraceFileContract(header,malformed_directory,
		static_cast<size_t>(TraceTableKind::Count),&start,1,
		&continuation,&validation) &&
		(validation.errors&(kTraceInvalidSegmentStart|
			kTraceSegmentStartMismatch))!=0,
		"continuation target-version ID is in bounds in the trace table");

	altered=start;
	altered.schema_version=kTraceSegmentStartRecordSchemaVersion+1;
	Check(!ValidateTraceFileContract(header,directory,
		static_cast<size_t>(TraceTableKind::Count),&altered,1,
		&continuation,&validation) &&
		(validation.errors&kTraceInvalidSegmentStart)!=0,
		"trace rejects an unknown segment-start record schema");

	RenderCaptureSegment fresh;
	Check(fresh.Reset(92,0,800),"fresh trace source reset");
	start=MakeTraceSegmentStartRecord(fresh);
	header.presented_frame_serial=fresh.PresentedFrameSerial();
	header.segment_serial=fresh.SegmentSerial();
	header.segment_start_kind=fresh.StartKind();
	header.continuation_state_schema_version=0;
	target_versions.element_count=0;
	target_versions.byte_size=0;
	start_table.file_offset=table_data_offset;
	header.file_size=table_data_offset+kTraceSegmentStartRecordSerializedSize;
	Check(ValidateTraceFileContract(header,directory,
		static_cast<size_t>(TraceTableKind::Count),&start,1,&fresh,&validation) &&
		start.start_kind==CaptureSegmentStartKind::Fresh &&
		start.continuation_state.schema_version==0,
		"fresh trace carries one canonical zero segment-start state");
}

static CaptureCommand PixelReadCommand(int32_t x, int32_t y, uint32_t request)
{
	CaptureCommand command = {};
	command.schema_version = kCaptureSchemaVersion;
	command.type = CaptureCommandType::ReadPixel;
	command.payload.read_pixel.source = ImageSemantic::SceneColor;
	command.payload.read_pixel.x = x;
	command.payload.read_pixel.y = y;
	command.payload.read_pixel.format = ReadbackFormat::RawRgba8;
	command.payload.read_pixel.request = request;
	return command;
}

static void TestCaptureReadbackContinuationAndPayloadShapes()
{
	RenderCaptureSegment prefix;
	Check(prefix.Reset(9,0,1000), "readback prefix reset");
	CapturedViewport viewport = {};
	viewport.logical_rect={0,0,64,64}; viewport.physical_rect=viewport.logical_rect;
	viewport.target_width=64; viewport.target_height=64; viewport.ssaa_factor=1;
	ViewportId viewport_id=prefix.InternViewport(viewport);
	CapturedWorldView view = {}; view.logical_clip=viewport.logical_rect;
	ViewStateId view_id=prefix.InternView(view);
	CapturedTargetLayout prefix_layout=MakeTargetLayout(RenderTargetClass::Scene,64,64,
		kTargetAttachmentMandatory);
	TargetLayoutId prefix_layout_id=prefix.InternTargetLayout(prefix_layout);
	CapturedTargetVersion prefix_version = {};
	prefix_version.target=RenderTargetClass::Scene; prefix_version.version=1;
	prefix_version.target_layout=prefix_layout_id;
	prefix_version.width=prefix_version.height=64; prefix_version.samples=1;
	prefix_version.color_epoch=3; prefix_version.depth_epoch=5;
	TargetVersionId prefix_version_id=prefix.InternTargetVersion(prefix_version);
	CaptureCommand command = {}; command.schema_version=kCaptureSchemaVersion;
	command.type=CaptureCommandType::BeginFrameTarget;
	command.payload.begin_frame_target.target=RenderTargetClass::Scene;
	command.payload.begin_frame_target.logical_clip=viewport.logical_rect;
	command.payload.begin_frame_target.physical_viewport=viewport_id;
	command.payload.begin_frame_target.view_state=view_id;
	command.payload.begin_frame_target.active_target_version=prefix_version_id;
	Check(prefix.AppendCopy(command), "readback prefix begins active interval");
	Check(prefix.AppendCopy(PixelReadCommand(4,5,1)), "readback prefix terminates at pixel read");
	CaptureValidationResult validation = {};
	Check(prefix.Validate(&validation), "synchronous prefix validates with open interval");

	RenderCaptureSegment mismatched_begin;
	Check(mismatched_begin.Reset(9,9,1015),"mismatched target begin reset");
	ViewportId mismatched_viewport=mismatched_begin.InternViewport(viewport);
	ViewStateId mismatched_view=mismatched_begin.InternView(view);
	TargetLayoutId mismatched_layout=mismatched_begin.InternTargetLayout(
		MakeTargetLayout(RenderTargetClass::PostPresent,64,64,kTargetAttachmentColor));
	CapturedTargetVersion mismatched_version = {};
	mismatched_version.target=RenderTargetClass::PostPresent;
	mismatched_version.version=1; mismatched_version.target_layout=mismatched_layout;
	mismatched_version.width=mismatched_version.height=64; mismatched_version.samples=1;
	TargetVersionId mismatched_version_id=
		mismatched_begin.InternTargetVersion(mismatched_version);
	command={}; command.schema_version=kCaptureSchemaVersion;
	command.type=CaptureCommandType::BeginFrameTarget;
	command.payload.begin_frame_target.target=RenderTargetClass::Scene;
	command.payload.begin_frame_target.logical_clip=viewport.logical_rect;
	command.payload.begin_frame_target.physical_viewport=mismatched_viewport;
	command.payload.begin_frame_target.view_state=mismatched_view;
	command.payload.begin_frame_target.active_target_version=mismatched_version_id;
	mismatched_begin.AppendCopy(command);
	mismatched_begin.AppendCopy(PixelReadCommand(1,1,10));
	Check(!mismatched_begin.Validate(&validation) &&
		(validation.errors&kCaptureInvalidTargetRelation)!=0,
		"BeginFrameTarget target class must match its exact TargetVersionId layout");

	RenderCaptureSegment continuation;
	CaptureContinuationState continuation_state=SceneContinuationState();
	Check(continuation.ResetContinuation(9,1,1002,continuation_state),
		"continuation starts after submitted prefix");
	InstallContinuationTarget(continuation,continuation_state);
	Check(continuation.StartKind()==CaptureSegmentStartKind::ContinuationAfterReadback &&
		continuation.ContinuationState().load_attachment_mask==kTargetAttachmentMandatory,
		"continuation freezes LOAD and resource-state handoff");
	Check(continuation.AppendCopy(PixelReadCommand(6,7,2)),
		"repeated editor readback needs no synthetic BeginFrameTarget");
	Check(continuation.Validate(&validation), "readback continuation validates");
	Check(continuation.Freeze(&validation),
		"submitted prefix freezes before its continuation is constructed");
	RenderCaptureSegment inherited_continuation;
	CaptureContinuationState inherited_tables_state=continuation_state;
	inherited_tables_state.prior_submitted_timeline=12;
	inherited_tables_state.resource_state_snapshot_serial=24;
	Check(inherited_continuation.ResetContinuationFrom(continuation,2,1003,
		inherited_tables_state) &&
		inherited_continuation.TargetLayouts().size()==
			continuation.TargetLayouts().size() &&
		inherited_continuation.TargetVersions().size()==
			continuation.TargetVersions().size() &&
		inherited_continuation.ContinuationState().active_target_version==
			continuation_state.active_target_version,
		"continuation inherits intern tables at the exact submitted indices");
	Check(inherited_continuation.AppendCopy(PixelReadCommand(8,9,4)) &&
		inherited_continuation.Validate(&validation),
		"inherited continuation is independently replayable without table reinstall");

	RenderCaptureSegment invalid_continuation;
	continuation_state.load_attachment_mask=kTargetAttachmentColor;
	Check(invalid_continuation.ResetContinuation(9,2,1003,continuation_state),
		"malformed continuation is captured for validation");
	InstallContinuationTarget(invalid_continuation,continuation_state);
	invalid_continuation.AppendCopy(PixelReadCommand(1,1,3));
	Check(!invalid_continuation.Validate(&validation) &&
		(validation.errors&kCaptureInvalidTargetRelation)!=0,
		"continuation cannot drop submitted depth state instead of LOAD");

	RenderCaptureSegment mismatched_epoch;
	CaptureContinuationState inherited_state=SceneContinuationState();
	CaptureContinuationState wrong_epoch_state=inherited_state;
	wrong_epoch_state.depth_epoch++;
	Check(mismatched_epoch.ResetContinuation(9,8,1014,wrong_epoch_state),
		"mismatched-epoch continuation reset");
	InstallContinuationTarget(mismatched_epoch,inherited_state);
	mismatched_epoch.AppendCopy(PixelReadCommand(1,1,9));
	Check(!mismatched_epoch.Validate(&validation) &&
		(validation.errors&kCaptureInvalidTargetRelation)!=0,
		"continuation epochs must exactly match the inherited TargetVersionId");

	RenderCaptureSegment bad_cockpit;
	CaptureContinuationState post_state=SceneContinuationState();
	post_state.active_target=RenderTargetClass::PostPresent;
	post_state.active_attachment_mask=kTargetAttachmentColor;
	post_state.load_attachment_mask=kTargetAttachmentColor;
	post_state.depth_epoch=0;
	post_state.post_present_begun=1;
	Check(bad_cockpit.ResetContinuation(9,3,1004,post_state),
		"cockpit-matching continuation reset");
	TargetVersionId post_version_id=InstallContinuationTarget(bad_cockpit,post_state);
	ViewportId post_viewport=bad_cockpit.InternViewport(viewport);
	ViewStateId post_view=bad_cockpit.InternView(view);
	CapturedCockpitBackingEffect cockpit_backing = {};
	PayloadDataId cockpit_backing_id=bad_cockpit.CopyPayloadData(&cockpit_backing,
		sizeof(cockpit_backing),4,kPayloadCockpitBacking);
	command={}; command.schema_version=kCaptureSchemaVersion; command.type=CaptureCommandType::EndFrame;
	command.payload.end_frame.view_interval_serial=1; bad_cockpit.AppendCopy(command);
	command={}; command.schema_version=kCaptureSchemaVersion; command.type=CaptureCommandType::BeginCockpitScene;
	command.payload.begin_cockpit_scene.logical_rect={0,0,64,64};
	command.payload.begin_cockpit_scene.backing_effect_state=cockpit_backing_id;
	command.payload.begin_cockpit_scene.capture_serial=71; bad_cockpit.AppendCopy(command);
	command={}; command.schema_version=kCaptureSchemaVersion; command.type=CaptureCommandType::EndCockpitScene;
	command.payload.end_cockpit_scene.capture_serial=72; bad_cockpit.AppendCopy(command);
	command={}; command.schema_version=kCaptureSchemaVersion; command.type=CaptureCommandType::BeginFrameTarget;
	command.payload.begin_frame_target.target=RenderTargetClass::PostPresent;
	command.payload.begin_frame_target.logical_clip={0,0,64,64};
	command.payload.begin_frame_target.physical_viewport=post_viewport;
	command.payload.begin_frame_target.view_state=post_view;
	command.payload.begin_frame_target.active_target_version=post_version_id;
	bad_cockpit.AppendCopy(command);
	bad_cockpit.AppendCopy(PixelReadCommand(1,1,4));
	Check(!bad_cockpit.Validate(&validation) &&
		(validation.errors&kCaptureInvalidCommandGrammar)!=0,
		"EndCockpitScene must match the frozen begin capture serial");

	RenderCaptureSegment bad_payload;
	CaptureContinuationState bad_payload_state=SceneContinuationState();
	Check(bad_payload.ResetContinuation(9,4,1009,bad_payload_state),
		"payload-shape continuation reset");
	InstallContinuationTarget(bad_payload,bad_payload_state);
	GpuSpecularBlock bad_specular = {}; bad_specular.count=kMaxSpecularSources+1;
	bad_payload.CopyPayloadData(&bad_specular,sizeof(bad_specular),16,kPayloadSpecularBlock);
	bad_payload.AppendCopy(PixelReadCommand(1,1,5));
	Check(!bad_payload.Validate(&validation) &&
		(validation.errors&kCaptureInvalidPayloadShape)!=0,
		"specular payload count is bounded");

	RenderCaptureSegment bad_upload;
	CaptureContinuationState bad_upload_state=SceneContinuationState();
	Check(bad_upload.ResetContinuation(9,5,1010,bad_upload_state),
		"texture-upload continuation reset");
	InstallContinuationTarget(bad_upload,bad_upload_state);
	uint32_t one_texel=0;
	PayloadDataId upload=bad_upload.CopyPayloadData(&one_texel,sizeof(one_texel),4,
		kPayloadTextureUpload);
	CapturedTextureVersion texture=DiagnosticTexture();
	texture.width=texture.height=2; texture.immutable_upload_payload=upload;
	bad_upload.RegisterTextureVersion(texture);
	bad_upload.AppendCopy(PixelReadCommand(1,1,6));
	Check(!bad_upload.Validate(&validation) &&
		(validation.errors&kCaptureInvalidPayloadShape)!=0,
		"texture upload bytes cover every declared mip texel");

	RenderCaptureSegment retained;
	CaptureContinuationState retained_continuation=SceneContinuationState();
	Check(retained.ResetContinuation(9,6,1011,retained_continuation),
		"retained continuation reset");
	InstallContinuationTarget(retained,retained_continuation);
	retained.RegisterTextureVersion(DiagnosticTexture());
	TargetLayoutId layout_id=retained.InternTargetLayout(
		MakeTargetLayout(RenderTargetClass::Scene,64,64,kTargetAttachmentMandatory));
	ViewportId retained_viewport=retained.InternViewport(viewport);
	CapturedShaderRasterState state = {}; state.target_layout=layout_id;
	state.sample_count=1; state.mrt_write_mask=kWriteColor;
	state.raster_family=RasterFamily::Ordinary;
	state.viewport=state.scissor=retained_viewport;
	StateId state_id=retained.InternState(state);
	CapturedMaterial material = {};
	for(uint32_t i=0;i<4;++i) material.image2d[i]=material.image2d_array[i]=0;
	MaterialRef material_id=retained.InternMaterial(material);
	TransformId transform_id=retained.InternTransform(CapturedTransform{});
	command={}; command.schema_version=kCaptureSchemaVersion;
	command.type=CaptureCommandType::DrawRetained;
	command.payload.draw_retained.mesh={4,2};
	command.payload.draw_retained.geometry_mode=GeometryMode::T1Retained;
	command.payload.draw_retained.first_index=6;
	command.payload.draw_retained.index_count=3;
	command.payload.draw_retained.perspective_payload={20,3};
	command.payload.draw_retained.motion_payload={kInvalidId,0};
	command.payload.draw_retained.specular_payload={kInvalidId,0};
	command.payload.draw_retained.state=state_id;
	command.payload.draw_retained.transform=transform_id;
	command.payload.draw_retained.material=material_id;
	command.payload.draw_retained.optional_payload=kInvalidId;
	command.payload.draw_retained.classification.source_kind=PrimitiveSourceKind::ExplicitTriangles;
	retained.AppendCopy(command);
	retained.AppendCopy(PixelReadCommand(1,1,7));
	Check(retained.Validate(&validation),
		"retained draw freezes independent typed face payload spans");

	RenderCaptureSegment bad_stream;
	CaptureContinuationState stream_continuation=SceneContinuationState();
	Check(bad_stream.ResetContinuation(9,7,1013,stream_continuation),
		"stream-payload continuation reset");
	InstallContinuationTarget(bad_stream,stream_continuation);
	bad_stream.RegisterTextureVersion(DiagnosticTexture());
	layout_id=bad_stream.InternTargetLayout(
		MakeTargetLayout(RenderTargetClass::Scene,64,64,kTargetAttachmentMandatory));
	ViewportId stream_viewport=bad_stream.InternViewport(viewport);
	state={}; state.target_layout=layout_id; state.sample_count=1;
	state.mrt_write_mask=kWriteColor; state.raster_family=RasterFamily::Ordinary;
	state.viewport=state.scissor=stream_viewport;
	state_id=bad_stream.InternState(state);
	material={};
	for(uint32_t i=0;i<4;++i) material.image2d[i]=material.image2d_array[i]=0;
	material_id=bad_stream.InternMaterial(material);
	transform_id=bad_stream.InternTransform(CapturedTransform{});
	MotionVertexPayload only_one_motion = {};
	PayloadDataId one_motion=bad_stream.CopyPayloadData(&only_one_motion,
		sizeof(only_one_motion),16,kPayloadMotionVertices);
	CapturedPayloadBinding one_motion_binding=EmptyPayloadBinding();
	one_motion_binding.motion_vertices=one_motion;
	one_motion_binding.validity_flags=kPayloadHasMotionVertices;
	PayloadRef one_motion_ref=bad_stream.InternPayloadBinding(one_motion_binding);
	BaseVertex stream_vertices[3] = {}; uint32_t stream_indices[3]={0,1,2};
	StreamGeometryRef stream_geometry=bad_stream.CopyStreamGeometry(stream_vertices,3,
		stream_indices,3,nullptr,0,DepthInterpretation::AlreadyMapped);
	command={}; command.schema_version=kCaptureSchemaVersion;
	command.type=CaptureCommandType::DrawStream;
	command.payload.draw_stream.geometry=stream_geometry;
	command.payload.draw_stream.state=state_id;
	command.payload.draw_stream.transform=transform_id;
	command.payload.draw_stream.material=material_id;
	command.payload.draw_stream.optional_payload=one_motion_ref;
	command.payload.draw_stream.classification.source_kind=PrimitiveSourceKind::ExplicitTriangles;
	bad_stream.AppendCopy(command);
	bad_stream.AppendCopy(PixelReadCommand(1,1,8));
	Check(!bad_stream.Validate(&validation) &&
		(validation.errors&kCaptureInvalidPayloadShape)!=0,
		"per-vertex optional payload count matches stream vertex count");
}

static void TestT2RetainedCaptureAbi()
{
	auto prepare_draw = [](RenderCaptureSegment &segment, uint32_t serial,
		uint32_t dynamic_light_count = 0) {
		CaptureContinuationState continuation=SceneContinuationState();
		Check(segment.ResetContinuation(10,serial,2000+serial,continuation),
			"T2 continuation reset");
		InstallContinuationTarget(segment,continuation);
		Check(segment.RegisterTextureVersion(DiagnosticTexture()),
			"T2 diagnostic texture registration");
		const TargetLayoutId layout=segment.InternTargetLayout(
			MakeTargetLayout(RenderTargetClass::Scene,64,64,kTargetAttachmentMandatory));
		CapturedViewport viewport = {};
		viewport.logical_rect=viewport.physical_rect={0,0,64,64};
		viewport.target_width=viewport.target_height=64; viewport.ssaa_factor=1;
		const ViewportId viewport_id=segment.InternViewport(viewport);
		CapturedShaderRasterState state = {};
		state.target_layout=layout; state.sample_count=1; state.mrt_write_mask=kWriteColor;
		state.raster_family=RasterFamily::Ordinary;
		state.viewport=state.scissor=viewport_id;
		state.shader.shader_flags=kShaderTerrain |
			(dynamic_light_count ? kShaderDynamicLights : 0u);
		state.shader.dynamic_light_count=dynamic_light_count;
		const StateId state_id=segment.InternState(state);
		CapturedMaterial material = {};
		for(uint32_t i=0;i<4;++i) material.image2d[i]=material.image2d_array[i]=0;
		const MaterialRef material_id=segment.InternMaterial(material);
		const TransformId transform_id=segment.InternTransform(CapturedTransform{});
		DrawRetainedCommand draw = {};
		draw.mesh={17,3}; draw.geometry_mode=GeometryMode::T2Terrain;
		draw.perspective_payload={kInvalidId,0};
		draw.motion_payload={kInvalidId,0};
		draw.specular_payload={kInvalidId,0};
		draw.state=state_id; draw.transform=transform_id; draw.material=material_id;
		draw.optional_payload=kInvalidId;
		draw.classification.source_kind=PrimitiveSourceKind::TerrainEmitter;
		return draw;
	};
	auto append_draw_and_read = [](RenderCaptureSegment &segment,
		const DrawRetainedCommand &draw, uint32_t request) {
		CaptureCommand command = {};
		command.schema_version=kCaptureSchemaVersion;
		command.type=CaptureCommandType::DrawRetained;
		command.payload.draw_retained=draw;
		Check(segment.AppendCopy(command),"append T2 retained draw");
		Check(segment.AppendCopy(PixelReadCommand(1,1,request)),
			"append T2 terminal readback");
	};
	auto make_inputs = [](TerrainEmitterCell (&cells)[2], TerrainWorkItem (&work)[2],
		TerrainBatchInput &batch, TerrainViewInput &view) {
		memset(cells,0,sizeof(cells)); memset(work,0,sizeof(work));
		memset(&batch,0,sizeof(batch)); memset(&view,0,sizeof(view));
		cells[0].packed[0]=1;
		cells[0].packed[1]=(0u<<kTerrainLightmapPageShift) |
			(0u<<kTerrainTextureLayerShift);
		cells[0].packed[2]=0; cells[0].packed[3]=0;
		cells[1].packed[0]=2;
		cells[1].packed[1]=(1u<<kTerrainLightmapPageShift) |
			(0u<<kTerrainTextureLayerShift);
		cells[1].packed[2]=0; cells[1].packed[3]=0;
		for(uint32_t cell=0;cell<2;++cell)
			for(uint32_t corner=0;corner<4;++corner)
				cells[cell].height[corner]=float(cell+corner);
		work[0]={0,7,kTerrainCellDynamic,1};
		work[1]={1,7,0,0};
		batch.source_texture=7; batch.texture_layer=0;
		batch.first_work_item=0; batch.work_item_count=2;
		batch.first_output_vertex=0;
		batch.output_vertex_capacity=2*kTerrainMaximumOutputVerticesPerCell;
		batch.indirect_command_index=0; batch.reserved0=0;
		view.terrain_x_step[0]=1.0f; view.terrain_z_step[2]=1.0f;
		view.terrain_y_step[1]=1.0f;
		view.projection_center_half_size[0]=32.0f;
		view.projection_center_half_size[1]=24.0f;
		view.projection_center_half_size[2]=32.0f;
		view.projection_center_half_size[3]=24.0f;
		view.viewport_size_inv_size[0]=64.0f;
		view.viewport_size_inv_size[1]=48.0f;
		view.viewport_size_inv_size[2]=1.0f/64.0f;
		view.viewport_size_inv_size[3]=1.0f/48.0f;
		view.clip_scale[0]=-1.0f; view.clip_scale[1]=1.0f;
		view.clip_scale[2]=-1.0f; view.clip_scale[3]=1.0f;
	};
	auto bind_inputs = [](RenderCaptureSegment &segment, const TerrainEmitterCell (&cells)[2],
		const TerrainWorkItem (&work)[2], const TerrainBatchInput &batch,
		const TerrainViewInput &view, uint32_t dynamic_light_count = 0) {
		CapturedPayloadBinding binding=EmptyPayloadBinding();
		binding.terrain_cells=segment.CopyPayloadData(cells,sizeof(cells),16,
			kPayloadTerrainCells);
		binding.terrain_work_items=segment.CopyPayloadData(work,sizeof(work),16,
			kPayloadTerrainWorkList);
		binding.terrain_batches=segment.CopyPayloadData(&batch,sizeof(batch),16,
			kPayloadTerrainBatches);
		binding.terrain_view_input=segment.CopyPayloadData(&view,sizeof(view),16,
			kPayloadTerrainViewInput);
		GpuWorldAux terrain_aux = {};
		GpuDynamicLight lights[kMaxTerrainDynamicLights] = {};
		uint32_t first_light = 0;
		for(uint32_t page=0;page<kTerrainLightmapPageCount;++page)
		{
			const uint32_t count=std::min<uint32_t>(kMaxDynamicLights,
				dynamic_light_count-first_light);
			terrain_aux.indices[page]=(first_light<<8)|count;
			first_light+=count;
		}
		if(dynamic_light_count)
		{
			binding.dynamic_lights=segment.CopyPayloadData(lights,
				dynamic_light_count*sizeof(GpuDynamicLight),16,kPayloadDynamicLights);
			binding.validity_flags|=kPayloadHasDynamicLights;
		}
		binding.world_aux=segment.CopyPayloadData(&terrain_aux,sizeof(terrain_aux),16,
			kPayloadWorldAux);
		binding.validity_flags|=kPayloadHasTerrainCells|kPayloadHasTerrainWorkItems|
			kPayloadHasTerrainBatches|kPayloadHasTerrainViewInput|kPayloadHasWorldAux;
		return segment.InternPayloadBinding(binding);
	};

	TerrainEmitterCell cells[2]; TerrainWorkItem work[2];
	TerrainBatchInput batch; TerrainViewInput view;
	make_inputs(cells,work,batch,view);
	RenderCaptureSegment valid;
	DrawRetainedCommand valid_draw=prepare_draw(valid,20);
	valid_draw.optional_payload=bind_inputs(valid,cells,work,batch,view);
	const PayloadDataId copied_cells=valid.PayloadBindings()[valid_draw.optional_payload].terrain_cells;
	cells[0].packed[0]=99;
	TerrainEmitterCell copied_cell = {};
	memcpy(&copied_cell,valid.PayloadBytes().data()+
		valid.PayloadRecords()[copied_cells].byte_offset,sizeof(copied_cell));
	Check(copied_cell.packed[0]==1,"T2 cell input is synchronously deep-copied");
	TerrainViewInput two_views[2] = {view,view};
	Check(valid.CopyPayloadData(two_views,sizeof(two_views),16,kPayloadTerrainViewInput)==kInvalidId,
		"T2 view payload has the exact one-record shape");
	append_draw_and_read(valid,valid_draw,20);
	CaptureValidationResult validation = {};
	Check(valid.Validate(&validation),"typed nonindexed T2 retained draw validates");

	make_inputs(cells,work,batch,view);
	RenderCaptureSegment lit;
	DrawRetainedCommand lit_draw=prepare_draw(lit,24,kMaxTerrainDynamicLights);
	lit_draw.optional_payload=bind_inputs(lit,cells,work,batch,view,
		kMaxTerrainDynamicLights);
	append_draw_and_read(lit,lit_draw,24);
	const bool lit_valid=lit.Validate(&validation);
	Check(lit_valid,
		"T2 accepts four page-local light ranges totaling 32 lights");

	RenderCaptureSegment untyped;
	DrawRetainedCommand untyped_draw=prepare_draw(untyped,21);
	uint32_t geometry_aux[4]={1,2,3,4};
	CapturedPayloadBinding untyped_binding=EmptyPayloadBinding();
	untyped_binding.geometry_aux=untyped.CopyPayloadData(geometry_aux,sizeof(geometry_aux),4,
		kPayloadGeometryAux);
	untyped_binding.validity_flags=kPayloadHasGeometryAux;
	untyped_draw.optional_payload=untyped.InternPayloadBinding(untyped_binding);
	append_draw_and_read(untyped,untyped_draw,21);
	Check(!untyped.Validate(&validation) &&
		(validation.errors&kCaptureInvalidPayloadBinding)!=0,
		"untyped geometry_aux cannot substitute for T2 records");

	make_inputs(cells,work,batch,view);
	batch.output_vertex_capacity--;
	batch.reserved0=1;
	RenderCaptureSegment malformed;
	DrawRetainedCommand malformed_draw=prepare_draw(malformed,22);
	malformed_draw.optional_payload=bind_inputs(malformed,cells,work,batch,view);
	append_draw_and_read(malformed,malformed_draw,22);
	Check(!malformed.Validate(&validation) &&
		(validation.errors&kCaptureInvalidPayloadShape)!=0 &&
		(validation.errors&kCaptureInvalidReservedBits)!=0,
		"T2 cross-counts and reserved batch fields are validated");

	make_inputs(cells,work,batch,view);
	RenderCaptureSegment indexed;
	DrawRetainedCommand indexed_draw=prepare_draw(indexed,23);
	indexed_draw.optional_payload=bind_inputs(indexed,cells,work,batch,view);
	indexed_draw.index_count=3;
	append_draw_and_read(indexed,indexed_draw,23);
	Check(!indexed.Validate(&validation) &&
		(validation.errors&kCaptureInvalidGeometry)!=0,
		"T2 rejects the T1 indexed command form");
}

static void TestDescriptorNoiseAndLifetimeGate()
{
	Check(ValidatePostPassDescriptorContract(), "post descriptor ABI validates");
	Check(sizeof(PostPassUniforms)==640 && alignof(PostPassUniforms)==16 &&
		offsetof(PostPassUniforms,source_extent_inv_extent)==192 &&
		offsetof(PostPassUniforms,frame_branch)==624,
		"post UBO size/alignment/offset ABI");
	Check(kPostPassUniformUsageContractCount==static_cast<size_t>(GraphNodeId::Count),
		"every graph node has an exact post-uniform field map");
	for(size_t i=0;i<kPostPassUniformUsageContractCount;++i)
		Check(static_cast<size_t>(kPostPassUniformUsageContract[i].node)==i &&
			(kPostPassUniformUsageContract[i].required_fields&
			 kPostPassUniformUsageContract[i].conditional_fields)==0,
			"post uniform usage maps are ordered and nonoverlapping");
	Check(kCompilerGraphPhaseContractCount==15 &&
		kCompilerGraphPhaseContract[0].node==GraphNodeId::CapWorld &&
		kCompilerGraphPhaseContract[3].kind==CompilerGraphPhaseKind::SsaaDownsample &&
		kCompilerGraphPhaseContract[4].kind==CompilerGraphPhaseKind::SsaaDownsample &&
		kCompilerGraphPhaseContract[5].kind==CompilerGraphPhaseKind::AttachmentAlphaOnlyClear &&
		kCompilerGraphPhaseContract[5].color_channel_mask==kChannelAlpha &&
		kCompilerGraphPhaseContract[11].kind==CompilerGraphPhaseKind::ResourceChannelAlias &&
		kCompilerGraphPhaseContract[12].kind==CompilerGraphPhaseKind::BloomThreshold &&
		kCompilerGraphPhaseContract[13].kind==CompilerGraphPhaseKind::BloomDownsample &&
		kCompilerGraphPhaseContract[14].kind==CompilerGraphPhaseKind::BloomMerge,
		"capture/cockpit/deferred-bloom compiler phases are explicit and ordered");
	GraphEvaluationContext source_selection = {};
	source_selection.msaa_samples = 4;
	source_selection.ssaa_factor = 4;
	Check(static_cast<uint32_t>(PostUniformSourceSelector::Primary2D)==2 &&
		static_cast<uint32_t>(PostUniformSourceSelector::MsaaResolved2D)==3 &&
		static_cast<uint32_t>(PostUniformSourceSelector::SsaaIntermediate2x2D)==4 &&
		SelectCompilerGraphPhaseSource(kCompilerGraphPhaseContract[3],
			source_selection)==PostUniformSourceSelector::MsaaResolved2D &&
		SelectCompilerGraphPhaseSource(kCompilerGraphPhaseContract[4],
			source_selection)==PostUniformSourceSelector::SsaaIntermediate2x2D &&
		SelectCompilerGraphPhaseSource(kCompilerGraphPhaseContract[9],
			source_selection)==PostUniformSourceSelector::MsaaResolved2D &&
		SelectCompilerGraphPhaseSource(kCompilerGraphPhaseContract[10],
			source_selection)==PostUniformSourceSelector::SsaaIntermediate2x2D,
		"frame_branch.y freezes the live CAP/cockpit image binding number");
	source_selection.msaa_samples = 1;
	source_selection.ssaa_factor = 2;
	Check(SelectCompilerGraphPhaseSource(kCompilerGraphPhaseContract[4],
		source_selection)==PostUniformSourceSelector::Primary2D &&
		SelectCompilerGraphPhaseSource(kCompilerGraphPhaseContract[10],
		source_selection)==PostUniformSourceSelector::Primary2D,
		"direct 2x SSAA selects binding 2 without fallback-image heuristics");
	Check(kPostPassDescriptorSetCount ==
		static_cast<size_t>(GraphNodeId::Count) + 9,
		"every graph node and nine exact sample/expansion variants have descriptor contracts");
	Check(FindPostPassDescriptorSet(GraphNodeId::CapWorld,
		PostPassVariant::SingleSample) != nullptr &&
		FindPostPassDescriptorSet(GraphNodeId::CapWorld,
		PostPassVariant::Multisample) != nullptr &&
		FindPostPassDescriptorSet(GraphNodeId::CapWorld,
		PostPassVariant::SsaaFourToTwo) != nullptr &&
		FindPostPassDescriptorSet(GraphNodeId::CapWorld,
		PostPassVariant::SsaaTwoToOne) != nullptr &&
		FindPostPassDescriptorSet(GraphNodeId::CapWorld,
		PostPassVariant::Only) == nullptr,
		"capture-world sample and two-stage SSAA variants are explicit");
	Check(FindPostPassDescriptorSet(GraphNodeId::CockpitResolve,
		PostPassVariant::SsaaFourToTwo) != nullptr &&
		FindPostPassDescriptorSet(GraphNodeId::CockpitResolve,
		PostPassVariant::SsaaTwoToOne) != nullptr &&
		FindPostPassDescriptorSet(GraphNodeId::BloomDeferred,
		PostPassVariant::BloomThresholdPhase) != nullptr &&
		FindPostPassDescriptorSet(GraphNodeId::BloomDeferred,
		PostPassVariant::BloomDownsamplePhase) != nullptr &&
		FindPostPassDescriptorSet(GraphNodeId::BloomDeferred,
		PostPassVariant::BloomMergePhase) != nullptr,
		"cockpit resolve and deferred bloom expansion descriptor sets are explicit");
	Check(FindPostPassDescriptorSet(GraphNodeId::CapWorld,
		PostPassVariant::SingleSample)->attachment_load_inputs==0 &&
		FindPostPassDescriptorSet(GraphNodeId::CapWorld,
		PostPassVariant::SingleSample)->uniform_record_size==sizeof(PostPassUniforms),
		"CAP_WORLD never samples and writes SceneColor in one rendering instance");
	Check(FindPostPassDescriptorSet(GraphNodeId::NormalUi,
		PostPassVariant::Only)->use == PostPassDescriptorUse::WorldDescriptorAbi &&
		FindPostPassDescriptorSet(GraphNodeId::PostAlphaClear,
		PostPassVariant::Only)->use == PostPassDescriptorUse::AttachmentOperation,
		"non-post-shader graph nodes explicitly select their descriptor policy");
	const PostPassDescriptorSetContract *motion_debug = FindPostPassDescriptorSet(
		GraphNodeId::MotionDebugNormal, PostPassVariant::Only);
	const PostPassDescriptorSetContract *cockpit_over = FindPostPassDescriptorSet(
		GraphNodeId::CockpitOver, PostPassVariant::Only);
	Check(motion_debug != nullptr && motion_debug->binding_count == 5 &&
		motion_debug->attachment_load_inputs == GraphResourceBit(GraphResource::PostPresent),
		"motion debug LOADs post-present and has velocity/depth/ID descriptors");
	Check(cockpit_over != nullptr && cockpit_over->binding_count == 3 &&
		cockpit_over->attachment_load_inputs == GraphResourceBit(GraphResource::PostPresent),
		"cockpit-over LOADs post-present and samples only cockpit composite");
	bool debug_uniform_carries_conditional_matrices = false;
	bool debug_optional_depth = false;
	bool debug_optional_id = false;
	bool debug_samples_post_present = false;
	bool cockpit_samples_post_present = false;
	bool temporal_optional_depth = false;
	bool suppression_has_authored_color = false;
	bool cockpit_bloom_has_protection = false;
	for (size_t i = 0; i < kPostPassDescriptorBindingCount; ++i)
	{
		const PostPassDescriptorBindingContract &binding = kPostPassDescriptorBindings[i];
		if (binding.node == GraphNodeId::MotionDebugNormal)
		{
			debug_uniform_carries_conditional_matrices |= binding.semantic ==
				PostDescriptorResourceSemantic::PassUniforms &&
				binding.resource == GraphResource::CapturedMatrices &&
				binding.requirement == PostDescriptorRequirement::Required;
			debug_optional_depth |= binding.resource == GraphResource::PostLogicalDepth &&
				binding.requirement == PostDescriptorRequirement::Optional;
			debug_optional_id |= binding.selected_input == GraphInputSemantic::ObjectIdSource &&
				binding.requirement == PostDescriptorRequirement::Optional;
			debug_samples_post_present |= binding.resource == GraphResource::PostPresent;
		}
		if (binding.node == GraphNodeId::CockpitOver)
			cockpit_samples_post_present |= binding.resource == GraphResource::PostPresent;
		if (binding.node == GraphNodeId::AoTemporal)
			temporal_optional_depth |= binding.resource == GraphResource::PostLogicalDepth &&
				binding.kind == PostDescriptorKind::SampledDepth2D &&
				binding.requirement == PostDescriptorRequirement::Optional;
		if (binding.node == GraphNodeId::AoSuppress)
			suppression_has_authored_color |=
				binding.selected_input == GraphInputSemantic::AuthoredBaseColor;
		if (binding.node == GraphNodeId::CockpitBloomGamma)
			cockpit_bloom_has_protection |=
				binding.selected_input == GraphInputSemantic::ProtectionMaskSource;
	}
	Check(debug_uniform_carries_conditional_matrices && debug_optional_depth && debug_optional_id &&
		!debug_samples_post_present,
		"motion debug required UBO carries conditional matrices and attachment-only destination");
	Check(!cockpit_samples_post_present,
		"cockpit-over destination is not misdeclared as a sampled image");
	Check(temporal_optional_depth && suppression_has_authored_color &&
		cockpit_bloom_has_protection,
		"temporal reconstruction, bloom suppression, and deferred double protection inputs exist");

	static const uint8_t expected_noise[32] = {
		141, 1, 180, 148, 60, 253, 250, 30,
		130, 142, 245, 142, 137, 84, 106, 234,
		186, 19, 2, 168, 109, 130, 149, 231,
		111, 158, 15, 30, 60, 202, 11, 157,
	};
	Check(kGtaoNoiseRg8Size == sizeof(expected_noise) &&
		memcmp(kGtaoNoiseRg8, expected_noise, sizeof(expected_noise)) == 0,
		"exact GL4 GTAO RG8 noise bytes");
	Check(kGtaoNoiseTextureContract.width == 4 &&
		kGtaoNoiseTextureContract.height == 4 &&
		kGtaoNoiseTextureContract.channels == 2 &&
		kGtaoNoiseTextureContract.format == RenderFormat::R8G8Unorm &&
		kGtaoNoiseTextureContract.sampler == SamplerSemantic::GtaoNoise &&
		kGtaoNoiseTextureContract.row_major_interleaved_rg == 1,
		"GTAO noise upload metadata");

	for (size_t i = 0; i < static_cast<size_t>(DependencyEdge::Count); ++i)
	{
		const DependencyEdgeContract &edge = kDependencyEdgeContract[i];
		Check(static_cast<size_t>(edge.edge) == i, "dependency edge order");
		Check(edge.producer_stages != 0 && edge.consumer_stages != 0,
			"dependency stages are explicit");
		Check((edge.producer_stages & ~kTrackedPipelineStageMask) == 0 &&
			(edge.consumer_stages & ~kTrackedPipelineStageMask) == 0,
			"dependency stage domain");
		Check((edge.producer_access & ~kTrackedResourceAccessMask) == 0 &&
			(edge.consumer_access & ~kTrackedResourceAccessMask) == 0,
			"dependency access domain");
		Check(edge.producer_layouts != 0 && edge.consumer_layouts != 0 &&
			(edge.producer_layouts & ~kTrackedResourceLayoutMask) == 0 &&
			(edge.consumer_layouts & ~kTrackedResourceLayoutMask) == 0,
			"dependency layout domain");
		Check(edge.tracked_subresources != 0 &&
			(edge.tracked_subresources & ~kTrackAllResourceRanges) == 0,
			"dependency range/subresource domain");
		Check(static_cast<uint32_t>(edge.timeline_rule) <
			static_cast<uint32_t>(DependencyTimelineRule::Count),
			"dependency timeline rule");
		Check(edge.queue_family_tracked == 1 &&
			edge.barrier_from_actual_state == 1,
			"actual-state and ownership tracking required");
	}
	const DependencyEdgeContract &readback =
		kDependencyEdgeContract[static_cast<size_t>(DependencyEdge::ReadbackCopyToHost)];
	Check(readback.timeline_rule ==
		DependencyTimelineRule::HostWaitForExactSubmittedTimeline &&
		readback.exact_timeline_completion_required == 1 &&
		(readback.tracked_subresources & kTrackHostMappedAtomRange) != 0,
		"readback exact timeline and host atom ownership");
	const DependencyEdgeContract &wsi_acquire = kDependencyEdgeContract[
		static_cast<size_t>(DependencyEdge::SwapchainAcquireToColorRender)];
	const DependencyEdgeContract &wsi_present = kDependencyEdgeContract[
		static_cast<size_t>(DependencyEdge::SwapchainColorRenderToPresent)];
	Check(wsi_acquire.timeline_rule == DependencyTimelineRule::WsiBinaryAcquireRenderPresent &&
		wsi_present.timeline_rule == DependencyTimelineRule::WsiBinaryAcquireRenderPresent &&
		wsi_acquire.wsi_binary_chain == 1 && wsi_present.wsi_binary_chain == 1 &&
		(wsi_acquire.tracked_subresources & kTrackSwapchainImage) != 0 &&
		(wsi_present.tracked_subresources & kTrackSwapchainImage) != 0,
		"split swapchain binary semaphore ownership chain");
	const DependencyEdgeContract &continuation = kDependencyEdgeContract[
		static_cast<size_t>(DependencyEdge::ContinuationAfterSynchronousReadback)];
	Check(continuation.timeline_rule ==
		DependencyTimelineRule::ContinuationWaitAndInheritPrefix &&
		continuation.continuation_inherits_exact_state == 1 &&
		continuation.exact_timeline_completion_required == 1 &&
		continuation.tracked_subresources == kTrackAllResourceRanges,
		"readback continuation inherits the exact prefix state");
}

static void TestWsiDecisionContract()
{
	Check(sizeof(kWsiResultDecisionContract)/sizeof(kWsiResultDecisionContract[0])==14,
		"WSI acquire/submit/present result inventory");
	for(size_t i=0;i<sizeof(kWsiResultDecisionContract)/
		sizeof(kWsiResultDecisionContract[0]);++i)
	{
		const WsiResultDecisionContract &decision=kWsiResultDecisionContract[i];
		Check(static_cast<uint32_t>(decision.phase)<static_cast<uint32_t>(WsiCallPhase::Count),
			"WSI decision phase domain");
		if((decision.actions&kWsiNoTimelineValueWasSubmitted)!=0)
		{
			Check((decision.actions&kWsiSubmittedTimelineIsValid)==0,
				"failed submission never creates an unsignalable timeline wait");
			Check((decision.actions&kWsiConsumeAcquireSignal)==0 &&
				(decision.actions&kWsiAcquireSignalOutstanding)!=0,
				"failed submission does not pretend it consumed the acquire wait");
		}
		if((decision.actions&kWsiAcceptedPresentation)!=0)
			Check((decision.actions&kWsiAdvancePresentedHistories)!=0,
				"only accepted presentation advances histories");
	}
	WsiAcquireResultDecision acquire = ResolveWsiAcquireResult(WsiResultClass::NotReady);
	Check(acquire.valid == 1 && acquire.retry_without_frame_context == 1 &&
		acquire.frame_context_claimed == 0 && acquire.acquire_signal_outstanding == 0,
		"zero-timeout acquire retries NOT_READY without consuming a frame context");
	acquire = ResolveWsiAcquireResult(WsiResultClass::Success);
	Check(acquire.valid == 1 && acquire.retry_without_frame_context == 0 &&
		acquire.frame_context_claimed == 1 && acquire.acquire_signal_outstanding == 1,
		"successful acquire claims the frame context and owns a signal");
	Check(FindWsiResultDecision(WsiCallPhase::Present, WsiResultClass::NotReady) == nullptr,
		"NOT_READY is legal only for test/failure-injection acquire");

	WsiAcquireSemaphoreInput acquire_semaphore = { 1, 1, 0, 0, 0 };
	WsiAcquireSemaphoreDecision acquire_signal =
		EvaluateAcquireSemaphore(acquire_semaphore);
	Check(acquire_signal.valid == 1 && acquire_signal.signal_still_outstanding == 1 &&
		acquire_signal.drain_submit_required == 1 && acquire_signal.reusable == 0,
		"abandoned successful acquire requires an explicit drain submission");
	acquire_semaphore.drain_submit_consumed_signal = 1;
	acquire_signal = EvaluateAcquireSemaphore(acquire_semaphore);
	Check(acquire_signal.valid == 1 && acquire_signal.signal_still_outstanding == 0 &&
		acquire_signal.drain_submit_required == 0 && acquire_signal.reusable == 0,
		"drain consumption alone does not prove acquire semaphore reuse safety");
	acquire_semaphore.consumer_submission_timeline_complete = 1;
	acquire_signal = EvaluateAcquireSemaphore(acquire_semaphore);
	Check(acquire_signal.reusable == 1,
		"acquire semaphore reuse waits for its consuming submission timeline");

	WsiPresentSemaphoreInput present_input = {
		WsiResultClass::OutOfDate, 0, 0, 0, 0
	};
	WsiPresentSemaphoreDecision present=ResolvePresentSemaphoreDecision(present_input);
	Check(present.valid == 1 && present.poisoned==1 && present.reusable==0 &&
		present.replacement_required == 1,
		"unconsumed failed-present semaphore is poisoned");
	present_input.present_wait_was_consumed = 1;
	present=ResolvePresentSemaphoreDecision(present_input);
	Check(present.valid == 1 && present.consumed==1 && present.poisoned==0 &&
		present.reusable==0 && present.replacement_required == 0,
		"consumed failed-present semaphore is not reused as a successful image semaphore");
	present_input.result = WsiResultClass::Success;
	present=ResolvePresentSemaphoreDecision(present_input);
	Check(present.valid == 1 && present.consumed==1 && present.reusable==0,
		"successful present does not permit immediate render-finished reuse");
	present_input.image_reacquired_from_same_generation = 1;
	present=ResolvePresentSemaphoreDecision(present_input);
	Check(present.consumed==1 && present.reusable==1,
		"successful present permits per-image reuse only after reacquire");
	present_input.producing_graphics_timeline_complete = 1;
	present_input.generation_presentation_complete = 1;
	present=ResolvePresentSemaphoreDecision(present_input);
	Check(present.destroyable == 1,
		"generation and graphics completion jointly permit semaphore destruction");

	const DependencyEdgeContract &acquire_edge = kDependencyEdgeContract[
		static_cast<size_t>(DependencyEdge::SwapchainAcquireToColorRender)];
	const DependencyEdgeContract &present_edge = kDependencyEdgeContract[
		static_cast<size_t>(DependencyEdge::SwapchainColorRenderToPresent)];
	Check(acquire_edge.producer_access == 0 &&
		acquire_edge.producer_layouts == (ResourceLayoutBit(ResourceLayout::Undefined) |
			ResourceLayoutBit(ResourceLayout::Present)) &&
		acquire_edge.consumer_stages == kPipelineStageColorAttachment &&
		acquire_edge.consumer_access == kAccessColorAttachmentWrite &&
		acquire_edge.consumer_layouts == ResourceLayoutBit(ResourceLayout::ColorAttachment) &&
		acquire_edge.wsi_binary_operation ==
			WsiBinarySemaphoreOperation::WaitAcquireBeforeColorRender,
		"acquire-to-color edge has exact stage/access/layout and binary wait");
	Check(present_edge.producer_stages == kPipelineStageColorAttachment &&
		present_edge.producer_access == kAccessColorAttachmentWrite &&
		present_edge.producer_layouts == ResourceLayoutBit(ResourceLayout::ColorAttachment) &&
		present_edge.consumer_access == 0 &&
		present_edge.consumer_layouts == ResourceLayoutBit(ResourceLayout::Present) &&
		present_edge.wsi_binary_operation ==
			WsiBinarySemaphoreOperation::SignalRenderFinishedBeforePresent,
		"color-to-present edge has exact stage/access/layout and binary signal/wait");
	WsiQueueOwnershipDecision ownership = ResolveWsiQueueOwnership(
		acquire_edge.wsi_queue_ownership, 3, 7, 0);
	Check(ownership.valid == 1 && ownership.emit_ownership_transfer == 1 &&
		ownership.source_queue_family == 7 && ownership.destination_queue_family == 3,
		"exclusive separate-family acquire transfers present to graphics");
	ownership = ResolveWsiQueueOwnership(present_edge.wsi_queue_ownership, 3, 7, 0);
	Check(ownership.valid == 1 && ownership.emit_ownership_transfer == 1 &&
		ownership.source_queue_family == 3 && ownership.destination_queue_family == 7,
		"exclusive separate-family present transfers graphics to present");
	ownership = ResolveWsiQueueOwnership(acquire_edge.wsi_queue_ownership, 3, 7, 1);
	Check(ownership.valid == 1 && ownership.emit_ownership_transfer == 0 &&
		ownership.source_queue_family == kQueueFamilyIgnored &&
		ownership.destination_queue_family == kQueueFamilyIgnored,
		"concurrent separate-family sharing emits no ownership transfer");

	WsiGenerationRetirementInput retirement = {};
	retirement.maximum_graphics_timeline_complete=1;
	WsiGenerationRetirementDecision retire=EvaluateWsiGenerationRetirement(retirement);
	Check(retire.proof==SwapchainRetirementProof::ScopedPresentQueueIdle &&
		retire.destroy_generation_objects==0,
		"graphics timeline alone cannot retire WSI generation");
	retirement.scoped_present_queue_idle_complete=1;
	retire=EvaluateWsiGenerationRetirement(retirement);
	Check(retire.destroy_generation_objects==1 && retire.destroy_generation_semaphores==1,
		"scoped present queue idle fallback proves complete generation retirement");
	retirement.present_wait_supported=1; retirement.present_id_complete=0;
	retire=EvaluateWsiGenerationRetirement(retirement);
	Check(retire.proof==SwapchainRetirementProof::CompletedPresentId &&
		retire.destroy_generation_objects==0,
		"present-wait support requires the exact completed present ID");
	retirement.present_id_complete=1; retirement.present_fence_supported=1;
	retirement.present_fence_complete=0;
	retire=EvaluateWsiGenerationRetirement(retirement);
	Check(retire.proof==SwapchainRetirementProof::PresentFenceMaintenance1 &&
		retire.destroy_generation_objects==0,
		"maintenance1 present fence is the selected proof when supported");

	auto validates_hidpi_capture = [](uint32_t target_drawable_width,
		uint32_t target_drawable_height, uint32_t wsi_drawable_width,
		uint32_t wsi_drawable_height) {
		RenderCaptureSegment segment;
		if (!segment.Reset(42, 0, 0)) return false;
		CapturedViewport viewport = {};
		viewport.logical_rect = { 0, 0, 640, 480 };
		viewport.physical_rect = viewport.logical_rect;
		viewport.target_width = 640; viewport.target_height = 480;
		viewport.ssaa_factor = 1;
		const ViewportId viewport_id = segment.InternViewport(viewport);
		CapturedWorldView view = {}; view.logical_clip = viewport.logical_rect;
		const ViewStateId view_id = segment.InternView(view);
		CapturedTargetLayout scene = MakeTargetLayout(RenderTargetClass::Scene,
			640, 480, kTargetAttachmentMandatory);
		CapturedTargetLayout post = MakeTargetLayout(RenderTargetClass::PostPresent,
			640, 480, kTargetAttachmentColor);
		CapturedTargetLayout cockpit = MakeTargetLayout(RenderTargetClass::CockpitScene,
			640, 480, kTargetAttachmentMandatory);
		scene.drawable_width = post.drawable_width = cockpit.drawable_width =
			target_drawable_width;
		scene.drawable_height = post.drawable_height = cockpit.drawable_height =
			target_drawable_height;
		CapturedTargetSignature targets = {};
		targets.target_layout = segment.InternTargetLayout(scene);
		targets.post_present_layout = segment.InternTargetLayout(post);
		targets.cockpit_scene_layout = segment.InternTargetLayout(cockpit);
		targets.preferred.width = 640; targets.preferred.height = 480;
		targets.preferred.window_width = 640; targets.preferred.window_height = 480;
		targets.preferred.supersampling_factor = 1;
		const RenderTargetSignatureId target_signature =
			segment.InternTargetSignature(targets);
		CapturedTargetVersion scene_version = {};
		scene_version.target=RenderTargetClass::Scene; scene_version.version=1;
		scene_version.target_layout=targets.target_layout;
		scene_version.width=640; scene_version.height=480; scene_version.samples=1;
		const TargetVersionId scene_version_id=segment.InternTargetVersion(scene_version);
		CapturedPresentRect rect = {};
		rect.drawable_width = wsi_drawable_width;
		rect.drawable_height = wsi_drawable_height;
		rect.rect = { 0, 0, static_cast<int32_t>(wsi_drawable_width),
			static_cast<int32_t>(wsi_drawable_height) };
		rect.surface_transform = 1; rect.swapchain_generation = 99;
		const PresentRectId rect_id = segment.InternPresentRect(rect);
		CapturedWsiSignature wsi = {};
		wsi.swapchain_generation = 99;
		wsi.format = SurfacePixelFormat::B8G8R8A8Unorm;
		wsi.color_space = SurfaceColorSpace::SrgbNonlinear;
		wsi.present_mode = PresentModeContract::Immediate;
		wsi.composite_alpha = CompositeAlphaContract::Opaque;
		wsi.surface_transform = 1;
		wsi.drawable_width = wsi_drawable_width;
		wsi.drawable_height = wsi_drawable_height;
		wsi.image_count = 3; wsi.graphics_queue_family = 0;
		wsi.present_queue_family = 0; wsi.safe_authored_unorm = 1;
		const WsiSignatureId wsi_id = segment.InternWsiSignature(wsi);
		CaptureCommand command = {}; command.schema_version = kCaptureSchemaVersion;
		command.type = CaptureCommandType::BeginFrameTarget;
		command.payload.begin_frame_target.target = RenderTargetClass::Scene;
		command.payload.begin_frame_target.logical_clip = viewport.logical_rect;
		command.payload.begin_frame_target.physical_viewport = viewport_id;
		command.payload.begin_frame_target.view_state = view_id;
		command.payload.begin_frame_target.active_target_version = scene_version_id;
		segment.AppendCopy(command);
		command = {}; command.schema_version = kCaptureSchemaVersion;
		command.type = CaptureCommandType::EndFrame;
		command.payload.end_frame.view_interval_serial = 1;
		segment.AppendCopy(command);
		command = {}; command.schema_version = kCaptureSchemaVersion;
		command.type = CaptureCommandType::BeginPostPresent;
		command.payload.begin_post_present.signature = target_signature;
		segment.AppendCopy(command);
		command = {}; command.schema_version = kCaptureSchemaVersion;
		command.type = CaptureCommandType::Present;
		command.payload.present.presented_frame_serial = 42;
		command.payload.present.window_swapchain_signature = wsi_id;
		command.payload.present.present_rect = rect_id;
		segment.AppendCopy(command);
		return segment.Validate();
	};
	Check(validates_hidpi_capture(1280, 960, 1280, 960),
		"HiDPI target signature uses frozen WSI-selected drawable pixels, not requested window size");
	Check(!validates_hidpi_capture(1280, 960, 1024, 768),
		"present links target signature drawable identity to the selected WSI generation");
}

int main()
{
	TestFrozenTables();
	TestGraphSelections();
	TestExecutableGraphEvaluation();
	TestStickyStateRules();
	TestDeterministicCorpus();
	TestExecutableCorpusCoverage();
	TestScalarAndCoordinateRules();
	TestSharedCoordinateConversions();
	TestTextureVersionRules();
	TestDeviceSelectionAndTextureUploadContracts();
	TestCapture();
	TestCaptureRejectsTargetMismatch();
	TestCaptureRejectsGrammarGeometryAndRects();
	TestTraceContinuationReplayContract();
	TestCaptureReadbackContinuationAndPayloadShapes();
	TestT2RetainedCaptureAbi();
	TestDescriptorNoiseAndLifetimeGate();
	TestWsiDecisionContract();
	if (failures) { fprintf(stderr,"%d render contract test(s) failed\n",failures); return 1; }
	printf("render contract tests passed\n");
	return 0;
}
