/* Frozen scene/post graph dependencies and resource domains. */
#pragma once

#include "render_contract.h"

namespace piccu
{
namespace render
{

enum class GraphResource : uint32_t
{
	SceneColor = 0,
	SceneDepth,
	SceneVelocity,
	SceneProtectionMask,
	SceneAoClass,
	SceneObjectId,
	CapturedMatrices,
	CapturedWorldColor,
	CapturedWorldDepth,
	ResolvedColor,
	ResolvedDepth,
	ResolvedVelocity,
	ResolvedProtectionMask,
	ResolvedAoClass,
	ResolvedObjectId,
	SsaaIntermediate2x,
	LogicalAuthoredColor,
	PostLogicalDepth,
	GtaoDepthWeight,
	GtaoRaw,
	GtaoBlurTemporary,
	GtaoCurrent,
	GtaoHistoryPrevious,
	GtaoHistoryNext,
	GtaoSuppression,
	GtaoApplied,
	GtaoDeferredComposite,
	BloomCurrentLevel,
	BloomSmallerLevel,
	BloomMerged,
	PostPresent,
	VelocityDebugReadback,
	CockpitScene,
	CockpitResolved,
	CockpitAlpha,
	SoftDepthSnapshot,
	GtaoNoise,
	Swapchain,
	CapturedWorldMsaaResolved,
	CapturedWorldIntermediate2x,
	CockpitMsaaResolved,
	CockpitSsaaIntermediate2x,
	Count,
};

using GraphResourceMask = uint64_t;
constexpr GraphResourceMask GraphResourceBit(GraphResource resource)
{
	return GraphResourceMask(1) << static_cast<uint32_t>(resource);
}

enum class GraphPredicate : uint32_t
{
	ExactCaptureCall = 0,
	LatePostAtCapture,
	MsaaColorConsumer,
	MsaaDepthConsumer,
	MsaaVelocityConsumer,
	MsaaObjectIdConsumer,
	MsaaProtectionConsumer,
	MsaaAoConsumer,
	SsaaFour,
	SsaaAtLeastTwo,
	LatePost,
	Gtao,
	GtaoWithBlur,
	GtaoTemporalOrDebug,
	GtaoDeferred,
	Bloom,
	BloomPyramidLevel,
	BloomMergeLevel,
	NormalBranchBloom,
	NormalBranchNoBloom,
	NormalBranchMotion,
	NormalBranchMotionDebug,
	NormalBranchUi,
	CockpitBranch,
	CockpitBranchMotion,
	CockpitBranchMotionDebug,
	CockpitUiPre,
	CockpitSceneActive,
	CockpitDeferredBloom,
	CockpitBloomResult,
	CockpitNoBloomResult,
	CockpitUiPost,
	Present,
	Count,
};

// Fully normalized inputs used by the compiler to turn the frozen manifest
// into an executable graph.  Counts describe ordered capture spans; boolean
// fields are restricted to zero or one.  No predicate is inferred from a
// diagnostic label or from the mere existence of an allocated image.
struct GraphEvaluationContext
{
	uint32_t capture_call_count;
	uint32_t world_color_capture_call_count;
	uint32_t depth_capture_call_count;
	uint32_t post_frame_active;
	uint32_t present_count;
	uint32_t msaa_samples;
	uint32_t ssaa_factor;
	uint32_t resolve_consumer_mask; // TargetAttachmentBits.
	uint32_t late_post_active;
	uint32_t gtao_enabled;
	uint32_t gtao_blur_radius;
	uint32_t gtao_temporal_active;
	uint32_t gtao_debug_active;
	uint32_t gtao_deferred_active;
	uint32_t bloom_enabled;
	uint32_t bloom_source_width;
	uint32_t bloom_source_height;
	uint32_t cockpit_deferral_active;
	uint32_t cockpit_frame_count;
	uint32_t motion_consumer_active;
	uint32_t motion_debug_active;
	uint32_t motion_debug_readback_active;
	uint32_t normal_ui_span_count;
	uint32_t cockpit_ui_pre_span_count;
	uint32_t cockpit_ui_post_span_count;
};

enum GraphEvaluationErrorBits : uint32_t
{
	kGraphEvaluationInvalidBoolean = 1u << 0,
	kGraphEvaluationInvalidSamples = 1u << 1,
	kGraphEvaluationInvalidResolveMask = 1u << 2,
	kGraphEvaluationInvalidCaptureCounts = 1u << 3,
	kGraphEvaluationInvalidBranch = 1u << 4,
	kGraphEvaluationInvalidFeatureDependency = 1u << 5,
};

// Returns the exact number of RGBA8 pyramid levels, including the threshold
// level.  Zero means the source is too small to produce a bloom result.
uint32_t ComputeBloomPyramidLevelCount(uint32_t source_width,
	uint32_t source_height);
uint32_t ValidateGraphEvaluationContext(const GraphEvaluationContext &context);
bool EvaluateGraphPredicate(GraphPredicate predicate,
	const GraphEvaluationContext &context);
uint32_t EvaluateGraphNodeInvocationCount(GraphNodeId node,
	const GraphEvaluationContext &context);
GraphResourceMask EvaluateGraphNodeOutputs(GraphNodeId node,
	const GraphEvaluationContext &context);

enum class GraphExtentRule : uint32_t
{
	SceneSsaa = 0,
	CapturedLogical,
	ResolvedScene,
	SsaaTwoX,
	Logical,
	GtaoConfigured,
	BloomHalfThenPyramid,
	BloomCurrentLevel,
	WindowLogical,
	DrawableSwapchain,
};

enum class GraphHistoryRule : uint32_t
{
	None = 0,
	ReadsPreviousWritesNext,
	AdvanceAfterAcceptedPresentation,
	PreserveAlpha,
	AlphaOnlyClear,
};

struct GraphNodeContract
{
	GraphNodeId id;
	GraphPredicate predicate;
	GraphResourceMask inputs;
	GraphResourceMask outputs;
	GraphExtentRule extent;
	GraphDomain domain;
	RenderFormat primary_output_format;
	GraphHistoryRule history;
	uint32_t load_or_composite;
};

extern const GraphNodeContract kFrozenGraphNodeContract[];
extern const size_t kFrozenGraphNodeContractCount;

enum class GraphInputSemantic : uint32_t
{
	None = 0,
	SceneColorAfterMsaa,
	SsaaTwoToOneColor,
	AuthoredBaseColor,
	FinalAuthoredColor,
	PostDepthSource,
	VelocitySource,
	ObjectIdSource,
	ProtectionMaskSource,
	AoClassSource,
	AoSceneColorSource,
	AoPreTemporalSource,
	AoFinalSource,
	TemporalHistorySource,
	Count,
};

struct GraphSourceSelectionContext
{
	uint32_t msaa_samples;
	uint32_t ssaa_factor;
	uint32_t captured_logical_depth_valid;
	uint32_t gtao_blur_output_valid;
	uint32_t gtao_temporal_output_valid;
	uint32_t gtao_history_required;
	uint32_t gtao_applied_output_valid;
	uint32_t gtao_deferred_output_valid;
};

GraphResource SelectGraphInputSource(GraphInputSemantic semantic,
	const GraphSourceSelectionContext &context);

// Each selected_inputs entry resolves through SelectGraphInputSource. This
// freezes source priority even when several old resource versions coexist.
// GraphNodeContract::inputs is the possible-input inventory, not an any-of
// license. Nodes absent from this table require every listed input.
struct GraphInputRuleContract
{
	GraphNodeId id;
	GraphResourceMask required_inputs;
	GraphResourceMask optional_inputs;
	GraphInputSemantic selected_inputs[4];
};

extern const GraphInputRuleContract kGraphInputRuleContract[];
extern const size_t kGraphInputRuleContractCount;

enum class InsertedGraphNodeId : uint32_t
{
	AcquireSoftDepth = 0,
	Count,
};

struct InsertedGraphNodeContract
{
	InsertedGraphNodeId id;
	const char *diagnostic_name;
	GraphResourceMask required_inputs;
	GraphResourceMask outputs;
	uint32_t inserted_at_exact_capture_serial;
	uint32_t invalidated_only_by_gl4_notify_depth_write_paths;
};

extern const InsertedGraphNodeContract kInsertedGraphNodeContract[];
extern const size_t kInsertedGraphNodeContractCount;

enum HistoryBits : uint32_t
{
	kHistoryGtao = 1u << 0,
	kHistoryMotionMatrices = 1u << 1,
	kHistoryCockpitMotion = 1u << 2,
	kHistoryFrozenStaticMotion = 1u << 3,
	kHistoryCapturedWorld = 1u << 4,
};

enum class HistoryEvent : uint32_t
{
	TargetOrWindowExtentChange = 0,
	SsaaOrMsaaSignatureChange,
	GtaoResolutionOrTemporalModeChange,
	RelevantFeatureToggle,
	DeviceOrSurfaceRecreation,
	CameraCutOrEngineInvalidation,
	PauseOrFreeze,
	SkippedOrFailedPresent,
	AcceptedPresent,
	Count,
};

enum HistoryActionBits : uint32_t
{
	kHistoryInvalidate = 1u << 0,
	kHistoryFreeze = 1u << 1,
	kHistoryDoNotAdvance = 1u << 2,
	kHistoryAdvance = 1u << 3,
};

struct HistoryEventContract
{
	HistoryEvent event;
	uint32_t affected_histories;
	uint32_t actions;
};

extern const HistoryEventContract kHistoryEventContract[];
extern const size_t kHistoryEventContractCount;

} // namespace render
} // namespace piccu
