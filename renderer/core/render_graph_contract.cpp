#include "render_graph_contract.h"

namespace piccu
{
namespace render
{

#define R(resource) GraphResourceBit(GraphResource::resource)

const GraphNodeContract kFrozenGraphNodeContract[] = {
	{ GraphNodeId::CapWorld, GraphPredicate::ExactCaptureCall,
		R(SceneColor) | R(CapturedMatrices) | R(CapturedWorldMsaaResolved) |
		R(CapturedWorldIntermediate2x),
		R(CapturedWorldColor) | R(CapturedWorldMsaaResolved) |
		R(CapturedWorldIntermediate2x) | R(SceneColor),
		GraphExtentRule::CapturedLogical, GraphDomain::Scene,
		RenderFormat::R8G8B8A8Unorm, GraphHistoryRule::AlphaOnlyClear, 1 },
	{ GraphNodeId::CapDepthLogical, GraphPredicate::LatePostAtCapture,
		R(SceneDepth), R(CapturedWorldDepth), GraphExtentRule::CapturedLogical,
		GraphDomain::Scene, RenderFormat::D32Sfloat, GraphHistoryRule::None, 0 },
	{ GraphNodeId::ResolveColor, GraphPredicate::MsaaColorConsumer,
		R(SceneColor), R(ResolvedColor), GraphExtentRule::ResolvedScene,
		GraphDomain::Resolve, RenderFormat::R8G8B8A8Unorm, GraphHistoryRule::None, 0 },
	{ GraphNodeId::ResolveDepth, GraphPredicate::MsaaDepthConsumer,
		R(SceneDepth), R(ResolvedDepth), GraphExtentRule::ResolvedScene,
		GraphDomain::Resolve, RenderFormat::D32Sfloat, GraphHistoryRule::None, 0 },
	{ GraphNodeId::ResolveVelocity, GraphPredicate::MsaaVelocityConsumer,
		R(SceneVelocity), R(ResolvedVelocity), GraphExtentRule::ResolvedScene,
		GraphDomain::Resolve, RenderFormat::R16G16Sfloat, GraphHistoryRule::None, 0 },
	{ GraphNodeId::ResolveObjectId, GraphPredicate::MsaaObjectIdConsumer,
		R(SceneObjectId), R(ResolvedObjectId), GraphExtentRule::ResolvedScene,
		GraphDomain::Resolve, RenderFormat::R32Uint, GraphHistoryRule::None, 0 },
	{ GraphNodeId::ResolveProtectionMask, GraphPredicate::MsaaProtectionConsumer,
		R(SceneProtectionMask), R(ResolvedProtectionMask), GraphExtentRule::ResolvedScene,
		GraphDomain::Resolve, RenderFormat::R8G8Unorm, GraphHistoryRule::None, 0 },
	{ GraphNodeId::ResolveAoClass, GraphPredicate::MsaaAoConsumer,
		R(SceneAoClass), R(ResolvedAoClass), GraphExtentRule::ResolvedScene,
		GraphDomain::Resolve, RenderFormat::R8Unorm, GraphHistoryRule::None, 0 },
	{ GraphNodeId::Ssaa4To2, GraphPredicate::SsaaFour,
		R(SceneColor) | R(ResolvedColor), R(SsaaIntermediate2x), GraphExtentRule::SsaaTwoX,
		GraphDomain::Resolve, RenderFormat::R8G8B8A8Unorm, GraphHistoryRule::None, 0 },
	{ GraphNodeId::Ssaa2To1, GraphPredicate::SsaaAtLeastTwo,
		R(SceneColor) | R(ResolvedColor) | R(SsaaIntermediate2x), R(LogicalAuthoredColor),
		GraphExtentRule::Logical, GraphDomain::Resolve,
		RenderFormat::R8G8B8A8Unorm, GraphHistoryRule::None, 0 },
	{ GraphNodeId::PrepareDepthLogical, GraphPredicate::LatePost,
		R(CapturedWorldDepth) | R(SceneDepth) | R(ResolvedDepth), R(PostLogicalDepth),
		GraphExtentRule::Logical, GraphDomain::Resolve,
		RenderFormat::D32Sfloat, GraphHistoryRule::None, 0 },
	{ GraphNodeId::AoDepth, GraphPredicate::Gtao,
		R(PostLogicalDepth) | R(ResolvedAoClass) | R(SceneAoClass), R(GtaoDepthWeight),
		GraphExtentRule::GtaoConfigured, GraphDomain::PostAuthored,
		RenderFormat::R32G32Sfloat, GraphHistoryRule::None, 0 },
	{ GraphNodeId::AoRaw, GraphPredicate::Gtao,
		R(GtaoDepthWeight) | R(GtaoNoise), R(GtaoRaw), GraphExtentRule::GtaoConfigured,
		GraphDomain::PostAuthored, RenderFormat::R16G16Sfloat, GraphHistoryRule::None, 0 },
	{ GraphNodeId::AoBlurX, GraphPredicate::GtaoWithBlur,
		R(GtaoRaw), R(GtaoBlurTemporary), GraphExtentRule::GtaoConfigured,
		GraphDomain::PostAuthored, RenderFormat::R16G16Sfloat, GraphHistoryRule::None, 0 },
	{ GraphNodeId::AoBlurY, GraphPredicate::GtaoWithBlur,
		R(GtaoBlurTemporary), R(GtaoCurrent), GraphExtentRule::GtaoConfigured,
		GraphDomain::PostAuthored, RenderFormat::R16G16Sfloat, GraphHistoryRule::None, 0 },
	{ GraphNodeId::AoTemporal, GraphPredicate::GtaoTemporalOrDebug,
		R(GtaoCurrent) | R(GtaoRaw) | R(GtaoHistoryPrevious) |
		R(SceneVelocity) | R(ResolvedVelocity) | R(SceneObjectId) |
		R(ResolvedObjectId) | R(CapturedMatrices) | R(PostLogicalDepth),
		R(GtaoHistoryNext) | R(GtaoCurrent),
		GraphExtentRule::GtaoConfigured, GraphDomain::PostAuthored,
		RenderFormat::R16G16B16A16Sfloat, GraphHistoryRule::ReadsPreviousWritesNext, 0 },
	{ GraphNodeId::AoSuppress, GraphPredicate::Gtao,
		R(ResolvedProtectionMask) | R(SceneProtectionMask) | R(SceneColor) |
		R(ResolvedColor) | R(LogicalAuthoredColor), R(GtaoSuppression),
		GraphExtentRule::GtaoConfigured, GraphDomain::PostAuthored,
		RenderFormat::R8Unorm, GraphHistoryRule::None, 0 },
	{ GraphNodeId::AoApply, GraphPredicate::Gtao,
		R(SceneColor) | R(ResolvedColor) | R(LogicalAuthoredColor) |
		R(GtaoCurrent) | R(GtaoRaw) | R(GtaoSuppression), R(GtaoApplied),
		GraphExtentRule::Logical, GraphDomain::PostAuthored,
		RenderFormat::R8G8B8A8Unorm, GraphHistoryRule::PreserveAlpha, 1 },
	{ GraphNodeId::AoDeferredComposite, GraphPredicate::GtaoDeferred,
		R(GtaoApplied) | R(CapturedWorldColor) | R(SceneColor) | R(ResolvedColor) |
		R(LogicalAuthoredColor) | R(ResolvedProtectionMask) | R(SceneProtectionMask),
		R(GtaoDeferredComposite), GraphExtentRule::Logical,
		GraphDomain::PostAuthored, RenderFormat::R8G8B8A8Unorm,
		GraphHistoryRule::PreserveAlpha, 1 },
	{ GraphNodeId::BloomThreshold, GraphPredicate::Bloom,
		R(GtaoDeferredComposite) | R(GtaoApplied) | R(LogicalAuthoredColor) |
		R(SceneColor) | R(ResolvedColor) |
		R(PostLogicalDepth) | R(ResolvedProtectionMask) | R(SceneProtectionMask) |
		R(CockpitAlpha), R(BloomCurrentLevel),
		GraphExtentRule::BloomHalfThenPyramid, GraphDomain::PostAuthored,
		RenderFormat::R8G8B8A8Unorm, GraphHistoryRule::None, 0 },
	{ GraphNodeId::BloomDown, GraphPredicate::BloomPyramidLevel,
		R(BloomCurrentLevel), R(BloomSmallerLevel), GraphExtentRule::BloomHalfThenPyramid,
		GraphDomain::PostAuthored, RenderFormat::R8G8B8A8Unorm, GraphHistoryRule::None, 0 },
	{ GraphNodeId::BloomUp, GraphPredicate::BloomMergeLevel,
		R(BloomCurrentLevel) | R(BloomSmallerLevel), R(BloomMerged),
		GraphExtentRule::BloomCurrentLevel, GraphDomain::PostAuthored,
		RenderFormat::R8G8B8A8Unorm, GraphHistoryRule::None, 1 },
	{ GraphNodeId::NormalComposite, GraphPredicate::NormalBranchBloom,
		R(GtaoDeferredComposite) | R(GtaoApplied) | R(LogicalAuthoredColor) |
		R(SceneColor) | R(ResolvedColor) |
		R(BloomMerged) | R(ResolvedProtectionMask) | R(SceneProtectionMask), R(PostPresent),
		GraphExtentRule::Logical, GraphDomain::PostGammaEncoded,
		RenderFormat::R8G8B8A8Unorm, GraphHistoryRule::PreserveAlpha, 1 },
	{ GraphNodeId::NormalBlit, GraphPredicate::NormalBranchNoBloom,
		R(GtaoDeferredComposite) | R(GtaoApplied) | R(LogicalAuthoredColor) |
		R(SceneColor) | R(ResolvedColor), R(PostPresent),
		GraphExtentRule::Logical, GraphDomain::PostGammaEncoded,
		RenderFormat::R8G8B8A8Unorm, GraphHistoryRule::PreserveAlpha, 0 },
	{ GraphNodeId::MotionNormal, GraphPredicate::NormalBranchMotion,
		R(PostPresent) | R(PostLogicalDepth) | R(SceneVelocity) |
		R(ResolvedVelocity) | R(SceneObjectId) | R(ResolvedObjectId) |
		R(CapturedMatrices),
		R(PostPresent), GraphExtentRule::Logical, GraphDomain::PostGammaEncoded,
		RenderFormat::R8G8B8A8Unorm, GraphHistoryRule::PreserveAlpha, 1 },
	{ GraphNodeId::MotionDebugNormal, GraphPredicate::NormalBranchMotionDebug,
		R(PostPresent) | R(PostLogicalDepth) | R(SceneVelocity) |
		R(ResolvedVelocity) | R(SceneObjectId) | R(ResolvedObjectId) |
		R(CapturedMatrices),
		R(PostPresent) | R(VelocityDebugReadback),
		GraphExtentRule::Logical, GraphDomain::PostGammaEncoded,
		RenderFormat::R8G8B8A8Unorm, GraphHistoryRule::None, 1 },
	{ GraphNodeId::NormalUi, GraphPredicate::NormalBranchUi,
		R(PostPresent), R(PostPresent), GraphExtentRule::Logical,
		GraphDomain::PostGammaEncoded, RenderFormat::R8G8B8A8Unorm,
		GraphHistoryRule::PreserveAlpha, 1 },
	{ GraphNodeId::CockpitLinearCopy, GraphPredicate::CockpitBranch,
		R(GtaoDeferredComposite) | R(GtaoApplied) | R(LogicalAuthoredColor) |
		R(SceneColor) | R(ResolvedColor), R(PostPresent),
		GraphExtentRule::Logical, GraphDomain::PostAuthored,
		RenderFormat::R8G8B8A8Unorm, GraphHistoryRule::PreserveAlpha, 0 },
	{ GraphNodeId::MotionCockpitPre, GraphPredicate::CockpitBranchMotion,
		R(PostPresent) | R(PostLogicalDepth) | R(SceneVelocity) |
		R(ResolvedVelocity) | R(SceneObjectId) | R(ResolvedObjectId) |
		R(CapturedMatrices),
		R(PostPresent), GraphExtentRule::Logical, GraphDomain::PostAuthored,
		RenderFormat::R8G8B8A8Unorm, GraphHistoryRule::PreserveAlpha, 1 },
	{ GraphNodeId::MotionDebugCockpitPre, GraphPredicate::CockpitBranchMotionDebug,
		R(PostPresent) | R(PostLogicalDepth) | R(SceneVelocity) |
		R(ResolvedVelocity) | R(SceneObjectId) | R(ResolvedObjectId) |
		R(CapturedMatrices),
		R(PostPresent) | R(VelocityDebugReadback),
		GraphExtentRule::Logical, GraphDomain::PostAuthored,
		RenderFormat::R8G8B8A8Unorm, GraphHistoryRule::None, 1 },
	{ GraphNodeId::CockpitUiPre, GraphPredicate::CockpitUiPre,
		R(PostPresent), R(PostPresent), GraphExtentRule::Logical,
		GraphDomain::PostAuthored, RenderFormat::R8G8B8A8Unorm,
		GraphHistoryRule::PreserveAlpha, 1 },
	{ GraphNodeId::CockpitScene, GraphPredicate::CockpitSceneActive,
		0, R(CockpitScene), GraphExtentRule::SceneSsaa,
		GraphDomain::Cockpit, RenderFormat::R8G8B8A8Unorm,
		GraphHistoryRule::None, 1 },
	{ GraphNodeId::PostAlphaClear, GraphPredicate::CockpitSceneActive,
		R(PostPresent), R(PostPresent), GraphExtentRule::Logical,
		GraphDomain::PostAuthored, RenderFormat::R8G8B8A8Unorm,
		GraphHistoryRule::AlphaOnlyClear, 1 },
	{ GraphNodeId::CockpitResolve, GraphPredicate::CockpitSceneActive,
		R(CockpitScene) | R(CockpitMsaaResolved) | R(CockpitSsaaIntermediate2x) |
		R(CockpitResolved),
		R(CockpitResolved) | R(CockpitAlpha) | R(CockpitMsaaResolved) |
		R(CockpitSsaaIntermediate2x), GraphExtentRule::Logical,
		GraphDomain::Cockpit, RenderFormat::R8G8B8A8Unorm,
		GraphHistoryRule::None, 0 },
	{ GraphNodeId::BloomDeferred, GraphPredicate::CockpitDeferredBloom,
		R(PostPresent) | R(PostLogicalDepth) | R(ResolvedProtectionMask) |
		R(SceneProtectionMask) | R(CockpitAlpha) | R(BloomCurrentLevel) |
		R(BloomSmallerLevel),
		R(BloomCurrentLevel) | R(BloomSmallerLevel) | R(BloomMerged),
		GraphExtentRule::BloomHalfThenPyramid,
		GraphDomain::Cockpit, RenderFormat::R8G8B8A8Unorm, GraphHistoryRule::None, 0 },
	{ GraphNodeId::CockpitOver, GraphPredicate::CockpitSceneActive,
		R(PostPresent) | R(CockpitResolved), R(PostPresent), GraphExtentRule::Logical,
		GraphDomain::PostAuthored, RenderFormat::R8G8B8A8Unorm,
		GraphHistoryRule::PreserveAlpha, 1 },
	{ GraphNodeId::CockpitBloomGamma, GraphPredicate::CockpitBloomResult,
		R(PostPresent) | R(BloomMerged) | R(SceneProtectionMask) |
		R(ResolvedProtectionMask), R(PostPresent), GraphExtentRule::Logical,
		GraphDomain::PostGammaEncoded, RenderFormat::R8G8B8A8Unorm,
		GraphHistoryRule::PreserveAlpha, 1 },
	{ GraphNodeId::CockpitGammaOnly, GraphPredicate::CockpitNoBloomResult,
		R(PostPresent), R(PostPresent), GraphExtentRule::Logical,
		GraphDomain::PostGammaEncoded, RenderFormat::R8G8B8A8Unorm,
		GraphHistoryRule::PreserveAlpha, 0 },
	{ GraphNodeId::CockpitUiPost, GraphPredicate::CockpitUiPost,
		R(PostPresent), R(PostPresent), GraphExtentRule::Logical,
		GraphDomain::PostGammaEncoded, RenderFormat::R8G8B8A8Unorm,
		GraphHistoryRule::PreserveAlpha, 1 },
	{ GraphNodeId::Present, GraphPredicate::Present,
		R(PostPresent), R(Swapchain), GraphExtentRule::DrawableSwapchain,
		GraphDomain::Present, RenderFormat::R8G8B8A8Unorm,
		GraphHistoryRule::AdvanceAfterAcceptedPresentation, 1 },
};

const size_t kFrozenGraphNodeContractCount =
	sizeof(kFrozenGraphNodeContract) / sizeof(kFrozenGraphNodeContract[0]);

uint32_t ComputeBloomPyramidLevelCount(uint32_t source_width,
	uint32_t source_height)
{
	if (source_width < 16 || source_height < 16)
		return 0;

	uint32_t width = source_width / 2;
	uint32_t height = source_height / 2;
	uint32_t level_count = 0;
	while (level_count < 8 && width >= 8 && height >= 8)
	{
		++level_count;
		width /= 2;
		height /= 2;
	}
	return level_count;
}

static bool IsNormalizedBoolean(uint32_t value)
{
	return value <= 1;
}

uint32_t ValidateGraphEvaluationContext(const GraphEvaluationContext &context)
{
	uint32_t errors = 0;
	const uint32_t booleans[] = {
		context.post_frame_active,
		context.late_post_active,
		context.gtao_enabled,
		context.gtao_temporal_active,
		context.gtao_debug_active,
		context.gtao_deferred_active,
		context.bloom_enabled,
		context.cockpit_deferral_active,
		context.motion_consumer_active,
		context.motion_debug_active,
		context.motion_debug_readback_active,
	};
	for (size_t i = 0; i < sizeof(booleans) / sizeof(booleans[0]); ++i)
		if (!IsNormalizedBoolean(booleans[i]))
			errors |= kGraphEvaluationInvalidBoolean;

	if ((context.msaa_samples != 1 && context.msaa_samples != 2 &&
		 context.msaa_samples != 4 && context.msaa_samples != 8) ||
		(context.ssaa_factor != 1 && context.ssaa_factor != 2 &&
		 context.ssaa_factor != 4))
		errors |= kGraphEvaluationInvalidSamples;
	if ((context.resolve_consumer_mask & ~kTargetAttachmentAll) != 0)
		errors |= kGraphEvaluationInvalidResolveMask;
	if (context.world_color_capture_call_count > context.capture_call_count ||
		context.depth_capture_call_count > context.capture_call_count ||
		context.present_count > 1 ||
		context.cockpit_frame_count > UINT32_MAX / 15u)
		errors |= kGraphEvaluationInvalidCaptureCounts;

	const bool normal_branch = context.cockpit_deferral_active == 0;
	if ((context.cockpit_frame_count != 0 ||
		 context.cockpit_ui_pre_span_count != 0 ||
		 context.cockpit_ui_post_span_count != 0) &&
		context.cockpit_deferral_active == 0)
		errors |= kGraphEvaluationInvalidBranch;
	if (context.normal_ui_span_count != 0 && !normal_branch)
		errors |= kGraphEvaluationInvalidBranch;
	if (context.present_count != 0 && context.post_frame_active == 0)
		errors |= kGraphEvaluationInvalidBranch;

	const bool any_late_post_consumer = context.gtao_enabled != 0 ||
		context.bloom_enabled != 0 || context.motion_consumer_active != 0 ||
		context.cockpit_deferral_active != 0;
	if ((context.late_post_active != 0) != any_late_post_consumer ||
		(context.gtao_temporal_active != 0 && context.gtao_enabled == 0) ||
		(context.gtao_debug_active != 0 && context.gtao_enabled == 0) ||
		(context.gtao_deferred_active != 0 &&
		 (context.gtao_enabled == 0 ||
		  context.world_color_capture_call_count == 0)) ||
		(context.motion_debug_active != 0 &&
		 context.motion_consumer_active == 0) ||
		(context.motion_debug_readback_active != 0 &&
		 context.motion_debug_active == 0) ||
		((context.world_color_capture_call_count != 0 ||
		  context.depth_capture_call_count != 0) &&
		 context.late_post_active == 0))
		errors |= kGraphEvaluationInvalidFeatureDependency;
	return errors;
}

static bool HasResolveConsumer(const GraphEvaluationContext &context,
	uint32_t attachment)
{
	return context.msaa_samples > 1 &&
		(context.resolve_consumer_mask & attachment) != 0;
}

static bool HasBloomResult(const GraphEvaluationContext &context)
{
	return context.bloom_enabled != 0 &&
		ComputeBloomPyramidLevelCount(context.bloom_source_width,
			context.bloom_source_height) != 0;
}

bool EvaluateGraphPredicate(GraphPredicate predicate,
	const GraphEvaluationContext &context)
{
	const bool post = context.post_frame_active != 0;
	const bool cockpit = post && context.cockpit_deferral_active != 0;
	const bool normal = post && !cockpit;
	const bool bloom_result = HasBloomResult(context);
	switch (predicate)
	{
	case GraphPredicate::ExactCaptureCall:
		return context.capture_call_count != 0;
	case GraphPredicate::LatePostAtCapture:
		return context.depth_capture_call_count != 0;
	case GraphPredicate::MsaaColorConsumer:
		return HasResolveConsumer(context, kTargetAttachmentColor);
	case GraphPredicate::MsaaDepthConsumer:
		return HasResolveConsumer(context, kTargetAttachmentDepth);
	case GraphPredicate::MsaaVelocityConsumer:
		return HasResolveConsumer(context, kTargetAttachmentVelocity);
	case GraphPredicate::MsaaObjectIdConsumer:
		return HasResolveConsumer(context, kTargetAttachmentObjectId);
	case GraphPredicate::MsaaProtectionConsumer:
		return HasResolveConsumer(context, kTargetAttachmentProtectionMask);
	case GraphPredicate::MsaaAoConsumer:
		return HasResolveConsumer(context, kTargetAttachmentAoClass);
	case GraphPredicate::SsaaFour:
		return post && context.ssaa_factor == 4;
	case GraphPredicate::SsaaAtLeastTwo:
		return post && context.ssaa_factor >= 2;
	case GraphPredicate::LatePost:
		return post && context.late_post_active != 0;
	case GraphPredicate::Gtao:
		return post && context.gtao_enabled != 0;
	case GraphPredicate::GtaoWithBlur:
		return post && context.gtao_enabled != 0 &&
			context.gtao_blur_radius != 0;
	case GraphPredicate::GtaoTemporalOrDebug:
		return post && context.gtao_enabled != 0 &&
			(context.gtao_temporal_active != 0 ||
			 context.gtao_debug_active != 0);
	case GraphPredicate::GtaoDeferred:
		return post && context.gtao_enabled != 0 &&
			context.gtao_deferred_active != 0;
	case GraphPredicate::Bloom:
		return normal && bloom_result;
	case GraphPredicate::BloomPyramidLevel:
	case GraphPredicate::BloomMergeLevel:
		return normal && bloom_result &&
			ComputeBloomPyramidLevelCount(context.bloom_source_width,
				context.bloom_source_height) > 1;
	case GraphPredicate::NormalBranchBloom:
		return normal && bloom_result;
	case GraphPredicate::NormalBranchNoBloom:
		return normal && !bloom_result;
	case GraphPredicate::NormalBranchMotion:
		return normal && context.motion_consumer_active != 0;
	case GraphPredicate::NormalBranchMotionDebug:
		return normal && context.motion_debug_active != 0;
	case GraphPredicate::NormalBranchUi:
		return normal && context.normal_ui_span_count != 0;
	case GraphPredicate::CockpitBranch:
		return cockpit;
	case GraphPredicate::CockpitBranchMotion:
		return cockpit && context.motion_consumer_active != 0;
	case GraphPredicate::CockpitBranchMotionDebug:
		return cockpit && context.motion_debug_active != 0;
	case GraphPredicate::CockpitUiPre:
		return cockpit && context.cockpit_ui_pre_span_count != 0;
	case GraphPredicate::CockpitSceneActive:
		return cockpit && context.cockpit_frame_count != 0;
	case GraphPredicate::CockpitDeferredBloom:
	case GraphPredicate::CockpitBloomResult:
		return cockpit && context.cockpit_frame_count != 0 && bloom_result;
	case GraphPredicate::CockpitNoBloomResult:
		return cockpit && context.cockpit_frame_count != 0 && !bloom_result;
	case GraphPredicate::CockpitUiPost:
		return cockpit && context.cockpit_ui_post_span_count != 0;
	case GraphPredicate::Present:
		return context.present_count != 0;
	case GraphPredicate::Count:
		break;
	}
	return false;
}

uint32_t EvaluateGraphNodeInvocationCount(GraphNodeId node,
	const GraphEvaluationContext &context)
{
	if (static_cast<uint32_t>(node) >= static_cast<uint32_t>(GraphNodeId::Count))
		return 0;
	if (!EvaluateGraphPredicate(kFrozenGraphNodeContract[static_cast<size_t>(node)].predicate,
		context))
		return 0;

	const uint32_t bloom_levels = ComputeBloomPyramidLevelCount(
		context.bloom_source_width, context.bloom_source_height);
	switch (node)
	{
	case GraphNodeId::CapWorld:
		return context.capture_call_count;
	case GraphNodeId::CapDepthLogical:
		return context.depth_capture_call_count;
	case GraphNodeId::BloomDown:
	case GraphNodeId::BloomUp:
		return bloom_levels - 1;
	case GraphNodeId::BloomDeferred:
		// One threshold, N-1 downsample, and N-1 reverse merge invocations.
		return context.cockpit_frame_count * (bloom_levels * 2 - 1);
	case GraphNodeId::NormalUi:
		return context.normal_ui_span_count;
	case GraphNodeId::CockpitUiPre:
		return context.cockpit_ui_pre_span_count;
	case GraphNodeId::CockpitUiPost:
		return context.cockpit_ui_post_span_count;
	case GraphNodeId::CockpitScene:
	case GraphNodeId::PostAlphaClear:
	case GraphNodeId::CockpitOver:
	case GraphNodeId::CockpitBloomGamma:
	case GraphNodeId::CockpitGammaOnly:
		return context.cockpit_frame_count;
	case GraphNodeId::CockpitResolve:
	{
		uint32_t phase_count = context.msaa_samples > 1 ? 1u : 0u;
		if (context.ssaa_factor == 4)
			phase_count += 2;
		else if (context.ssaa_factor == 2)
			phase_count += 1;
		else if (context.msaa_samples == 1)
			phase_count += 1;
		return context.cockpit_frame_count * phase_count;
	}
	case GraphNodeId::Present:
		return context.present_count;
	default:
		return 1;
	}
}

GraphResourceMask EvaluateGraphNodeOutputs(GraphNodeId node,
	const GraphEvaluationContext &context)
{
	if (static_cast<uint32_t>(node) >= static_cast<uint32_t>(GraphNodeId::Count) ||
		EvaluateGraphNodeInvocationCount(node, context) == 0)
		return 0;
	GraphResourceMask outputs =
		kFrozenGraphNodeContract[static_cast<size_t>(node)].outputs;
	switch (node)
	{
	case GraphNodeId::CapWorld:
		if (context.world_color_capture_call_count == 0)
		{
			outputs &= ~(GraphResourceBit(GraphResource::CapturedWorldColor) |
				GraphResourceBit(GraphResource::CapturedWorldMsaaResolved) |
				GraphResourceBit(GraphResource::CapturedWorldIntermediate2x));
		}
		else
		{
			if (context.msaa_samples == 1 || context.ssaa_factor == 1)
				outputs &= ~GraphResourceBit(GraphResource::CapturedWorldMsaaResolved);
			if (context.ssaa_factor != 4)
				outputs &= ~GraphResourceBit(GraphResource::CapturedWorldIntermediate2x);
		}
		break;
	case GraphNodeId::AoTemporal:
		if (context.gtao_temporal_active == 0)
			outputs &= ~GraphResourceBit(GraphResource::GtaoHistoryNext);
		break;
	case GraphNodeId::MotionDebugNormal:
	case GraphNodeId::MotionDebugCockpitPre:
		if (context.motion_debug_readback_active == 0)
			outputs &= ~GraphResourceBit(GraphResource::VelocityDebugReadback);
		break;
	case GraphNodeId::CockpitResolve:
		if (context.msaa_samples == 1 || context.ssaa_factor == 1)
			outputs &= ~GraphResourceBit(GraphResource::CockpitMsaaResolved);
		if (context.ssaa_factor != 4)
			outputs &= ~GraphResourceBit(GraphResource::CockpitSsaaIntermediate2x);
		if (!HasBloomResult(context))
			outputs &= ~GraphResourceBit(GraphResource::CockpitAlpha);
		break;
	case GraphNodeId::BloomDeferred:
		if (ComputeBloomPyramidLevelCount(context.bloom_source_width,
			context.bloom_source_height) <= 1)
			outputs &= ~GraphResourceBit(GraphResource::BloomSmallerLevel);
		break;
	default:
		break;
	}
	return outputs;
}

const GraphInputRuleContract kGraphInputRuleContract[] = {
	{ GraphNodeId::CapWorld, R(SceneColor) | R(CapturedMatrices),
		R(CapturedWorldMsaaResolved) | R(CapturedWorldIntermediate2x),
		{ GraphInputSemantic::None, GraphInputSemantic::None,
		  GraphInputSemantic::None, GraphInputSemantic::None } },
	{ GraphNodeId::Ssaa4To2, 0, 0,
		{ GraphInputSemantic::SceneColorAfterMsaa, GraphInputSemantic::None,
		  GraphInputSemantic::None, GraphInputSemantic::None } },
	{ GraphNodeId::Ssaa2To1, 0, 0,
		{ GraphInputSemantic::SsaaTwoToOneColor, GraphInputSemantic::None,
		  GraphInputSemantic::None, GraphInputSemantic::None } },
	{ GraphNodeId::PrepareDepthLogical, 0, 0,
		{ GraphInputSemantic::PostDepthSource, GraphInputSemantic::None,
		  GraphInputSemantic::None, GraphInputSemantic::None } },
	{ GraphNodeId::AoDepth, R(PostLogicalDepth), 0,
		{ GraphInputSemantic::AoClassSource, GraphInputSemantic::None,
		  GraphInputSemantic::None, GraphInputSemantic::None } },
	{ GraphNodeId::AoRaw, R(GtaoDepthWeight) | R(GtaoNoise), 0,
		{ GraphInputSemantic::None, GraphInputSemantic::None,
		  GraphInputSemantic::None, GraphInputSemantic::None } },
	{ GraphNodeId::AoTemporal, R(CapturedMatrices), R(PostLogicalDepth),
		{ GraphInputSemantic::AoPreTemporalSource, GraphInputSemantic::VelocitySource,
		  GraphInputSemantic::ObjectIdSource,
		  GraphInputSemantic::TemporalHistorySource } },
	{ GraphNodeId::AoSuppress, 0, 0,
		{ GraphInputSemantic::ProtectionMaskSource, GraphInputSemantic::AuthoredBaseColor,
		  GraphInputSemantic::None, GraphInputSemantic::None } },
	{ GraphNodeId::AoApply, R(GtaoSuppression), 0,
		{ GraphInputSemantic::AuthoredBaseColor,
		  GraphInputSemantic::AoFinalSource,
		  GraphInputSemantic::None, GraphInputSemantic::None } },
	{ GraphNodeId::AoDeferredComposite,
		R(CapturedWorldColor) | R(GtaoApplied), 0,
		{ GraphInputSemantic::AuthoredBaseColor,
		  GraphInputSemantic::ProtectionMaskSource,
		  GraphInputSemantic::None, GraphInputSemantic::None } },
	{ GraphNodeId::BloomThreshold, R(PostLogicalDepth), R(CockpitAlpha),
		{ GraphInputSemantic::FinalAuthoredColor,
		  GraphInputSemantic::ProtectionMaskSource,
		  GraphInputSemantic::None, GraphInputSemantic::None } },
	{ GraphNodeId::NormalComposite, R(BloomMerged), 0,
		{ GraphInputSemantic::FinalAuthoredColor,
		  GraphInputSemantic::ProtectionMaskSource,
		  GraphInputSemantic::None, GraphInputSemantic::None } },
	{ GraphNodeId::NormalBlit, 0, 0,
		{ GraphInputSemantic::FinalAuthoredColor, GraphInputSemantic::None,
		  GraphInputSemantic::None, GraphInputSemantic::None } },
	{ GraphNodeId::MotionNormal,
		R(PostPresent) | R(PostLogicalDepth) | R(CapturedMatrices), 0,
		{ GraphInputSemantic::VelocitySource, GraphInputSemantic::ObjectIdSource,
		  GraphInputSemantic::None, GraphInputSemantic::None } },
	{ GraphNodeId::MotionDebugNormal, R(PostPresent),
		R(PostLogicalDepth) | R(CapturedMatrices),
		{ GraphInputSemantic::VelocitySource, GraphInputSemantic::ObjectIdSource,
		  GraphInputSemantic::None, GraphInputSemantic::None } },
	{ GraphNodeId::CockpitLinearCopy, 0, 0,
		{ GraphInputSemantic::FinalAuthoredColor, GraphInputSemantic::None,
		  GraphInputSemantic::None, GraphInputSemantic::None } },
	{ GraphNodeId::MotionCockpitPre,
		R(PostPresent) | R(PostLogicalDepth) | R(CapturedMatrices), 0,
		{ GraphInputSemantic::VelocitySource, GraphInputSemantic::ObjectIdSource,
		  GraphInputSemantic::None, GraphInputSemantic::None } },
	{ GraphNodeId::MotionDebugCockpitPre, R(PostPresent),
		R(PostLogicalDepth) | R(CapturedMatrices),
		{ GraphInputSemantic::VelocitySource, GraphInputSemantic::ObjectIdSource,
		  GraphInputSemantic::None, GraphInputSemantic::None } },
	{ GraphNodeId::BloomDeferred,
		R(PostPresent) | R(PostLogicalDepth) | R(CockpitAlpha),
		R(BloomCurrentLevel) | R(BloomSmallerLevel),
		{ GraphInputSemantic::ProtectionMaskSource, GraphInputSemantic::None,
		  GraphInputSemantic::None, GraphInputSemantic::None } },
	{ GraphNodeId::CockpitResolve, R(CockpitScene),
		R(CockpitMsaaResolved) | R(CockpitSsaaIntermediate2x) | R(CockpitResolved),
		{ GraphInputSemantic::None, GraphInputSemantic::None,
		  GraphInputSemantic::None, GraphInputSemantic::None } },
	{ GraphNodeId::CockpitBloomGamma, R(PostPresent) | R(BloomMerged), 0,
		{ GraphInputSemantic::ProtectionMaskSource, GraphInputSemantic::None,
		  GraphInputSemantic::None, GraphInputSemantic::None } },
};

const size_t kGraphInputRuleContractCount =
	sizeof(kGraphInputRuleContract) / sizeof(kGraphInputRuleContract[0]);

const InsertedGraphNodeContract kInsertedGraphNodeContract[] = {
	{ InsertedGraphNodeId::AcquireSoftDepth, "scene.soft_depth_snapshot",
		R(SceneDepth), R(SoftDepthSnapshot), 1, 1 },
};

const size_t kInsertedGraphNodeContractCount =
	sizeof(kInsertedGraphNodeContract) / sizeof(kInsertedGraphNodeContract[0]);

#undef R

GraphResource SelectGraphInputSource(GraphInputSemantic semantic,
	const GraphSourceSelectionContext &context)
{
	const bool multisampled = context.msaa_samples > 1;
	switch (semantic)
	{
	case GraphInputSemantic::None:
		return GraphResource::Count;
	case GraphInputSemantic::SceneColorAfterMsaa:
		return multisampled ? GraphResource::ResolvedColor : GraphResource::SceneColor;
	case GraphInputSemantic::SsaaTwoToOneColor:
		if (context.ssaa_factor == 4)
			return GraphResource::SsaaIntermediate2x;
		return multisampled ? GraphResource::ResolvedColor : GraphResource::SceneColor;
	case GraphInputSemantic::AuthoredBaseColor:
		if (context.ssaa_factor > 1)
			return GraphResource::LogicalAuthoredColor;
		return multisampled ? GraphResource::ResolvedColor : GraphResource::SceneColor;
	case GraphInputSemantic::FinalAuthoredColor:
		if (context.gtao_deferred_output_valid != 0)
			return GraphResource::GtaoDeferredComposite;
		if (context.gtao_applied_output_valid != 0)
			return GraphResource::GtaoApplied;
		if (context.ssaa_factor > 1)
			return GraphResource::LogicalAuthoredColor;
		return multisampled ? GraphResource::ResolvedColor : GraphResource::SceneColor;
	case GraphInputSemantic::PostDepthSource:
		if (context.captured_logical_depth_valid != 0)
			return GraphResource::CapturedWorldDepth;
		return multisampled ? GraphResource::ResolvedDepth : GraphResource::SceneDepth;
	case GraphInputSemantic::VelocitySource:
		return multisampled ? GraphResource::ResolvedVelocity : GraphResource::SceneVelocity;
	case GraphInputSemantic::ObjectIdSource:
		return multisampled ? GraphResource::ResolvedObjectId : GraphResource::SceneObjectId;
	case GraphInputSemantic::ProtectionMaskSource:
		return multisampled ? GraphResource::ResolvedProtectionMask :
			GraphResource::SceneProtectionMask;
	case GraphInputSemantic::AoClassSource:
		return multisampled ? GraphResource::ResolvedAoClass : GraphResource::SceneAoClass;
	case GraphInputSemantic::AoPreTemporalSource:
		return context.gtao_blur_output_valid != 0 ?
			GraphResource::GtaoCurrent : GraphResource::GtaoRaw;
	case GraphInputSemantic::AoFinalSource:
		return context.gtao_blur_output_valid != 0 ||
			context.gtao_temporal_output_valid != 0 ?
			GraphResource::GtaoCurrent : GraphResource::GtaoRaw;
	case GraphInputSemantic::TemporalHistorySource:
		return context.gtao_history_required != 0 ?
			GraphResource::GtaoHistoryPrevious : GraphResource::Count;
	case GraphInputSemantic::Count:
		break;
	}
	return GraphResource::Count;
}

const HistoryEventContract kHistoryEventContract[] = {
	{ HistoryEvent::TargetOrWindowExtentChange,
		kHistoryGtao | kHistoryMotionMatrices | kHistoryCockpitMotion |
		kHistoryFrozenStaticMotion | kHistoryCapturedWorld, kHistoryInvalidate },
	{ HistoryEvent::SsaaOrMsaaSignatureChange,
		kHistoryGtao | kHistoryMotionMatrices | kHistoryCockpitMotion |
		kHistoryFrozenStaticMotion | kHistoryCapturedWorld, kHistoryInvalidate },
	{ HistoryEvent::GtaoResolutionOrTemporalModeChange,
		kHistoryGtao, kHistoryInvalidate },
	{ HistoryEvent::RelevantFeatureToggle,
		kHistoryGtao | kHistoryMotionMatrices | kHistoryCockpitMotion |
		kHistoryCapturedWorld, kHistoryInvalidate },
	{ HistoryEvent::DeviceOrSurfaceRecreation,
		kHistoryGtao | kHistoryMotionMatrices | kHistoryCockpitMotion |
		kHistoryFrozenStaticMotion | kHistoryCapturedWorld, kHistoryInvalidate },
	{ HistoryEvent::CameraCutOrEngineInvalidation,
		kHistoryGtao | kHistoryMotionMatrices | kHistoryCockpitMotion |
		kHistoryFrozenStaticMotion, kHistoryInvalidate },
	{ HistoryEvent::PauseOrFreeze,
		kHistoryGtao | kHistoryMotionMatrices | kHistoryCockpitMotion |
		kHistoryFrozenStaticMotion, kHistoryFreeze | kHistoryDoNotAdvance },
	{ HistoryEvent::SkippedOrFailedPresent,
		kHistoryGtao | kHistoryMotionMatrices | kHistoryCockpitMotion |
		kHistoryFrozenStaticMotion, kHistoryDoNotAdvance },
	{ HistoryEvent::AcceptedPresent,
		kHistoryGtao | kHistoryMotionMatrices | kHistoryCockpitMotion |
		kHistoryFrozenStaticMotion, kHistoryAdvance },
};

const size_t kHistoryEventContractCount =
	sizeof(kHistoryEventContract) / sizeof(kHistoryEventContract[0]);

static_assert(sizeof(kFrozenGraphNodeContract) / sizeof(kFrozenGraphNodeContract[0]) ==
	static_cast<size_t>(GraphNodeId::Count), "frozen graph dependency contract mismatch");
static_assert(static_cast<uint32_t>(GraphResource::Count) <= 64,
	"GraphResourceMask needs widening");
static_assert(sizeof(kHistoryEventContract) / sizeof(kHistoryEventContract[0]) ==
	static_cast<size_t>(HistoryEvent::Count), "history event contract mismatch");
static_assert(sizeof(kInsertedGraphNodeContract) /
	sizeof(kInsertedGraphNodeContract[0]) ==
	static_cast<size_t>(InsertedGraphNodeId::Count),
	"inserted graph node contract mismatch");

} // namespace render
} // namespace piccu
