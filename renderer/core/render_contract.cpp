#include "render_contract.h"

#include <algorithm>

namespace piccu
{
namespace render
{

const GraphNodeManifestEntry kFrozenGraphManifest[] = {
	{ GraphNodeId::CapWorld, "CAP_WORLD", "capture.world", GraphDomain::Scene, 1 },
	{ GraphNodeId::CapDepthLogical, "CAP_DEPTH_LOGICAL", "capture.depth_logical", GraphDomain::Scene, 0 },
	{ GraphNodeId::ResolveColor, "RES_COLOR", "resolve.color", GraphDomain::Resolve, 0 },
	{ GraphNodeId::ResolveDepth, "RES_DEPTH", "resolve.depth", GraphDomain::Resolve, 0 },
	{ GraphNodeId::ResolveVelocity, "RES_VEL", "resolve.velocity", GraphDomain::Resolve, 0 },
	{ GraphNodeId::ResolveObjectId, "RES_ID", "resolve.object_id", GraphDomain::Resolve, 0 },
	{ GraphNodeId::ResolveProtectionMask, "RES_MASK", "resolve.mask", GraphDomain::Resolve, 0 },
	{ GraphNodeId::ResolveAoClass, "RES_AOCLASS", "resolve.ao_class", GraphDomain::Resolve, 0 },
	{ GraphNodeId::Ssaa4To2, "SSAA_4_TO_2", "ssaa.4to2", GraphDomain::Resolve, 0 },
	{ GraphNodeId::Ssaa2To1, "SSAA_2_TO_1", "ssaa.2to1", GraphDomain::Resolve, 0 },
	{ GraphNodeId::PrepareDepthLogical, "PREP_DEPTH_LOGICAL", "post.depth_logical", GraphDomain::Resolve, 0 },
	{ GraphNodeId::AoDepth, "AO_DEPTH", "gtao.depth_weight", GraphDomain::PostAuthored, 0 },
	{ GraphNodeId::AoRaw, "AO_RAW", "gtao.raw", GraphDomain::PostAuthored, 0 },
	{ GraphNodeId::AoBlurX, "AO_BLUR_X", "gtao.blur_x", GraphDomain::PostAuthored, 0 },
	{ GraphNodeId::AoBlurY, "AO_BLUR_Y", "gtao.blur_y", GraphDomain::PostAuthored, 0 },
	{ GraphNodeId::AoTemporal, "AO_TEMPORAL", "gtao.history_next", GraphDomain::PostAuthored, 0 },
	{ GraphNodeId::AoSuppress, "AO_SUPPRESS", "gtao.suppression", GraphDomain::PostAuthored, 0 },
	{ GraphNodeId::AoApply, "AO_APPLY", "gtao.applied", GraphDomain::PostAuthored, 1 },
	{ GraphNodeId::AoDeferredComposite, "AO_DEFERRED_COMPOSITE", "gtao.deferred_composite", GraphDomain::PostAuthored, 1 },
	{ GraphNodeId::BloomThreshold, "BLOOM_THRESHOLD", "bloom.level0", GraphDomain::PostAuthored, 0 },
	{ GraphNodeId::BloomDown, "BLOOM_DOWN_n", "bloom.level_n", GraphDomain::PostAuthored, 0 },
	{ GraphNodeId::BloomUp, "BLOOM_UP_n", "bloom.merge_n", GraphDomain::PostAuthored, 1 },
	{ GraphNodeId::NormalComposite, "NORMAL_COMPOSITE", "post.normal_bloom_gamma", GraphDomain::PostGammaEncoded, 1 },
	{ GraphNodeId::NormalBlit, "NORMAL_BLIT", "post.normal_gamma", GraphDomain::PostGammaEncoded, 0 },
	{ GraphNodeId::MotionNormal, "MOTION_NORMAL", "post.normal_motion", GraphDomain::PostGammaEncoded, 1 },
	{ GraphNodeId::MotionDebugNormal, "MOTION_DEBUG_NORMAL", "post.normal_motion_debug", GraphDomain::PostGammaEncoded, 1 },
	{ GraphNodeId::NormalUi, "NORMAL_UI", "post.normal_ui", GraphDomain::PostGammaEncoded, 1 },
	{ GraphNodeId::CockpitLinearCopy, "COCKPIT_LINEAR_COPY", "post.cockpit_linear", GraphDomain::PostAuthored, 0 },
	{ GraphNodeId::MotionCockpitPre, "MOTION_COCKPIT_PRE", "post.cockpit_motion", GraphDomain::PostAuthored, 1 },
	{ GraphNodeId::MotionDebugCockpitPre, "MOTION_DEBUG_COCKPIT_PRE", "post.cockpit_motion_debug", GraphDomain::PostAuthored, 1 },
	{ GraphNodeId::CockpitUiPre, "COCKPIT_UI_PRE", "post.cockpit_ui_pre", GraphDomain::PostAuthored, 1 },
	{ GraphNodeId::CockpitScene, "COCKPIT_SCENE", "cockpit.scene", GraphDomain::Cockpit, 1 },
	{ GraphNodeId::PostAlphaClear, "POST_ALPHA_CLEAR", "post.alpha_cleared", GraphDomain::PostAuthored, 1 },
	{ GraphNodeId::CockpitResolve, "COCKPIT_RESOLVE", "cockpit.resolved", GraphDomain::Cockpit, 0 },
	{ GraphNodeId::BloomDeferred, "BLOOM_DEFERRED_*", "bloom.deferred_*", GraphDomain::Cockpit, 0 },
	{ GraphNodeId::CockpitOver, "COCKPIT_OVER", "cockpit.composited", GraphDomain::PostAuthored, 1 },
	{ GraphNodeId::CockpitBloomGamma, "COCKPIT_BLOOM_GAMMA", "post.cockpit_bloom_gamma", GraphDomain::PostGammaEncoded, 1 },
	{ GraphNodeId::CockpitGammaOnly, "COCKPIT_GAMMA_ONLY", "post.cockpit_gamma", GraphDomain::PostGammaEncoded, 0 },
	{ GraphNodeId::CockpitUiPost, "COCKPIT_UI_POST", "post.cockpit_ui_post", GraphDomain::PostGammaEncoded, 1 },
	{ GraphNodeId::Present, "PRESENT", "present.swapchain", GraphDomain::Present, 1 },
};

const size_t kFrozenGraphManifestCount =
	sizeof(kFrozenGraphManifest) / sizeof(kFrozenGraphManifest[0]);

const BlendClassContract kBlendClassContract[] = {
	{ BlendClass::Opaque, 0, BlendFactorContract::One, BlendFactorContract::Zero,
		BlendFactorContract::One, BlendFactorContract::Zero },
	{ BlendClass::Alpha, 1, BlendFactorContract::SourceAlpha,
		BlendFactorContract::OneMinusSourceAlpha, BlendFactorContract::One,
		BlendFactorContract::OneMinusSourceAlpha },
	{ BlendClass::Saturate, 1, BlendFactorContract::SourceAlpha,
		BlendFactorContract::One, BlendFactorContract::One,
		BlendFactorContract::OneMinusSourceAlpha },
	{ BlendClass::Multiply, 1, BlendFactorContract::DestinationColor,
		BlendFactorContract::Zero, BlendFactorContract::One,
		BlendFactorContract::OneMinusSourceAlpha },
};

const size_t kBlendClassContractCount =
	sizeof(kBlendClassContract) / sizeof(kBlendClassContract[0]);

const SceneAttachmentContract kSceneAttachmentContract[] = {
	{ 0, ImageSemantic::SceneColor, RenderFormat::R8G8B8A8Unorm, 0, 0,
		AttachmentBlendMode::LegacyColorByDraw, BlendFactorContract::One,
		BlendFactorContract::Zero, BlendOperationContract::Add,
		ResolveRule::AverageSamples, { 0, 0, 0, 1 }, { 0, 0, 0, 0 } },
	{ 1, ImageSemantic::Velocity, RenderFormat::R16G16Sfloat, 0, 0,
		AttachmentBlendMode::Replace, BlendFactorContract::One,
		BlendFactorContract::Zero, BlendOperationContract::Add,
		ResolveRule::AverageSamples, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
	{ 2, ImageSemantic::ProtectionMask, RenderFormat::R8G8Unorm, 0, 0,
		AttachmentBlendMode::ComponentMax, BlendFactorContract::One,
		BlendFactorContract::One, BlendOperationContract::Maximum,
		ResolveRule::AverageSamples, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
	{ 3, ImageSemantic::AoClass, RenderFormat::R8Unorm, 0, 0,
		AttachmentBlendMode::Replace, BlendFactorContract::One,
		BlendFactorContract::Zero, BlendOperationContract::Add,
		ResolveRule::AverageSamples, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
	{ 4, ImageSemantic::MotionObjectId, RenderFormat::R32Uint, 0, 1,
		AttachmentBlendMode::Replace, BlendFactorContract::One,
		BlendFactorContract::Zero, BlendOperationContract::Add,
		ResolveRule::SampleZero, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
	{ kInvalidId, ImageSemantic::SceneDepth, RenderFormat::D32Sfloat, 1, 0,
		AttachmentBlendMode::Depth, BlendFactorContract::One,
		BlendFactorContract::Zero, BlendOperationContract::Add,
		ResolveRule::SampleZero, { 1, 0, 0, 0 }, { 0, 0, 0, 0 } },
};

const size_t kSceneAttachmentContractCount =
	sizeof(kSceneAttachmentContract) / sizeof(kSceneAttachmentContract[0]);

const StartFrameResetContract kStartFrameResetContract[] = {
	{ RenderTargetClass::Scene,
		kResetAoSuppression | kResetBloomSuppression | kResetAoClass |
		kResetAoWeight | kEnableDepthWrites | kMarkSceneDirty |
		kInvalidateSoftDepth | kClearProtectionAndAoClassOnce |
		kClearMotionAndObjectIdOnceIfUnfrozen | kApplyRequestedClears |
		kRestoreDefaultSceneDrawBuffers | kUseTargetMsaaState |
		kUpdateClipProjectionViewport | kPreserveUnlistedStickyState,
		0.0f, 0.0f, 0, 1.0f, 1,
		kWriteColor | kWriteProtectionMask | kWriteAoClass },
	{ RenderTargetClass::PostPresent,
		kFlushFontsBeforeRoute | kColorAttachmentOnly | kApplyRequestedClears |
		kDisableMsaa | kUpdateClipProjectionViewport | kPreserveUnlistedStickyState,
		0.0f, 0.0f, 0, 1.0f, 0, kWriteColor },
	{ RenderTargetClass::CockpitScene,
		kResetAoSuppression | kResetBloomSuppression | kResetAoClass |
		kResetAoWeight | kEnableDepthWrites | kColorAttachmentOnly |
		kUnconditionalTransparentColorAndDepthClear | kUseTargetMsaaState |
		kUpdateClipProjectionViewport | kPreserveUnlistedStickyState,
		0.0f, 0.0f, 0, 1.0f, 1, kWriteColor },
};

const size_t kStartFrameResetContractCount =
	sizeof(kStartFrameResetContract) / sizeof(kStartFrameResetContract[0]);

float GL4DepthFromEyeZ(float eye_z)
{
	const float clamped_z = std::max(eye_z, 0.0001f);
	return std::max(0.0f, std::min(1.0f, 1.0f - (1.0f / clamped_z)));
}

uint32_t NormalizeRequestedMsaa(uint32_t msaa_samples, bool legacy_antialias)
{
	if (msaa_samples >= 8)
		return 8;
	if (msaa_samples >= 4)
		return 4;
	if (msaa_samples >= 2)
		return 2;
	if (legacy_antialias)
		return 4;
	return 0;
}

uint32_t NormalizeOverscanPercent(const CapturedPreferredState &state)
{
	if (!state.gtao_enabled)
		return 100;
	return std::max(100u, std::min(150u, state.gtao_overscan_percent));
}

bool WantsMotionResources(const CapturedPreferredState &state)
{
	const bool gtao_temporal = state.gtao_enabled != 0 &&
		(state.gtao_temporal_blend > 0.0f || state.gtao_temporal_debug_preview != 0);
	const bool combined_motion = state.combined_motion_blur != 0 &&
		(state.pixel_motion_blur_strength > 0.0f ||
		 state.pixel_motion_blur_legacy_object_strength > 0.0f);
	return gtao_temporal || combined_motion;
}

bool IsLegalMrtWriteMask(uint32_t mask)
{
	for (size_t i = 0; i < sizeof(kLegalMrtWriteMasks) / sizeof(kLegalMrtWriteMasks[0]); ++i)
	{
		if (kLegalMrtWriteMasks[i] == mask)
			return true;
	}
	return false;
}

bool IsCaptureCommandTypeValid(CaptureCommandType type)
{
	return static_cast<uint32_t>(type) < static_cast<uint32_t>(CaptureCommandType::Count);
}

static_assert(sizeof(kFrozenGraphManifest) / sizeof(kFrozenGraphManifest[0]) ==
	static_cast<size_t>(GraphNodeId::Count),
	"Frozen graph manifest and GraphNodeId must change together");
static_assert(sizeof(kSceneAttachmentContract) / sizeof(kSceneAttachmentContract[0]) == 6,
	"Scene attachment ABI requires five color attachments plus depth");
static_assert(sizeof(kBlendClassContract) / sizeof(kBlendClassContract[0]) == 4,
	"legacy blend class contract mismatch");

} // namespace render
} // namespace piccu
