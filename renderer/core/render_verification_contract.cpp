#include "render_verification_contract.h"

namespace piccu
{
namespace render
{

namespace
{

CapturedPreferredState MakeCorpusRendererPreference(uint32_t full_post,
	uint32_t ssaa, uint32_t requested_msaa)
{
	CapturedPreferredState state = {};
	state.mipping = 1;
	state.filtering = 1;
	state.antialised = 0;
	state.bit_depth = 32;
	state.gamma = 1.0f;
	state.width = 1280;
	state.height = 720;
	state.window_width = 1280;
	state.window_height = 720;
	state.fullscreen = 0;
	state.supersampling_factor = ssaa;
	state.msaa_samples = requested_msaa;
	state.per_pixel_lighting = 1;
	state.bloom_enabled = full_post;
	state.bloom_threshold = 0.75f;
	state.bloom_intensity = 0.75f;
	state.bloom_spread = 0.75f;
	state.gtao_enabled = full_post;
	state.gtao_resolution = 2; // GTAO_RESOLUTION_HALF
	state.gtao_sample_count = 32;
	state.gtao_blur_radius = 6;
	state.gtao_radius = 4.0f;
	state.gtao_intensity = 2.5f;
	state.gtao_bias = 0.25f;
	state.gtao_overscan_percent = 107;
	state.gtao_debug_preview = 0;
	state.gtao_temporal_blend = 0.9f;
	state.gtao_temporal_depth_reject = 0.03f;
	state.gtao_temporal_velocity_reject = 128.0f;
	state.gtao_temporal_debug_preview = 0;
	state.gtao_terrain_occlusion = 0.5f;
	state.gtao_polyobject_occlusion = 0.5f;
	state.gtao_mine_rock_occlusion = 0.5f;
	state.gtao_mine_occlusion = 1.0f;
	state.motion_vector_mode = full_post; // OFF=0, PIXEL=1
	state.motion_vector_debug_preview = 0;
	state.pixel_motion_blur_strength = full_post ? 0.32f : 0.0f;
	state.combined_motion_blur = full_post;
	state.combined_motion_blur_legacy_strength = 1.0f;
	state.combined_motion_blur_legacy_frame_time = full_post ? 0.03f : 0.05f;
	state.combined_motion_blur_legacy_sphere_size = 0.20f;
	state.combined_motion_blur_legacy_copy_density = 2.0f;
	state.combined_motion_blur_legacy_max_iterations = 24;
	state.combined_motion_blur_legacy_alpha_exponent = full_post ? 2.0f : 1.5f;
	state.pixel_motion_blur_periphery_strength = 1.0f;
	state.pixel_motion_blur_legacy_object_strength = full_post ? 0.32f : 0.0f;
	state.pixel_motion_blur_center_suppression = full_post ? 1.0f : 0.0f;
	state.pixel_motion_blur_legacy_object_center_suppression = 0.0f;
	state.pixel_motion_blur_samples = full_post ? 17 : 9;
	state.afterburner_fov_multiplier = 0.08f;
	state.afterburner_pixel_blur_multiplier = 2.0f;
	return state;
}

CapturedPreferredState MakeCorpusRendererPreferenceWithGamma(float gamma)
{
	CapturedPreferredState state = MakeCorpusRendererPreference(1, 1, 0);
	state.gamma = gamma;
	return state;
}

CorpusEngineSettings MakeCorpusEngineSettings(CorpusHudMode hud_mode)
{
	CorpusEngineSettings state = {};
	state.aspect_width = 16.0f;
	state.aspect_height = 9.0f;
	state.fov_degrees = 72.0f;
	state.hud_scale = 1.0f;
	state.terrain_render_distance = 1920;
	state.pixel_error = 10.0f;
	state.specular_lighting = 1;
	state.dynamic_lighting = 1;
	state.fast_headlight = 1;
	state.mirrored_surfaces = 1;
	state.fog = 1;
	state.coronas = 1;
	state.procedurals = 1;
	state.powerup_halos = 1;
	state.scorches = 1;
	state.weapon_coronas = 1;
	state.bumpmapping = 0;
	state.specular_mapping_type = 1;
	state.object_complexity = 2;
	state.soft_visual_effects = 1;
	state.cpu_batch_cache = 0;
	state.disable_powerup_sparkles = 0;
	state.simd_particle_builder = 1;
	state.gl4_particle_instancing = 1;
	state.face_probe = 0;
	state.use_newrender = 0;
	state.debug_overlays = 0;
	state.hud_mode = hud_mode;
	return state;
}

constexpr uint64_t kFullPostNormalGraph =
	CorpusGraphBit(GraphNodeId::CapWorld) |
	CorpusGraphBit(GraphNodeId::CapDepthLogical) |
	CorpusGraphBit(GraphNodeId::PrepareDepthLogical) |
	CorpusGraphBit(GraphNodeId::AoDepth) | CorpusGraphBit(GraphNodeId::AoRaw) |
	CorpusGraphBit(GraphNodeId::AoBlurX) | CorpusGraphBit(GraphNodeId::AoBlurY) |
	CorpusGraphBit(GraphNodeId::AoTemporal) | CorpusGraphBit(GraphNodeId::AoSuppress) |
	CorpusGraphBit(GraphNodeId::AoApply) |
	CorpusGraphBit(GraphNodeId::AoDeferredComposite) |
	CorpusGraphBit(GraphNodeId::BloomThreshold) |
	CorpusGraphBit(GraphNodeId::BloomDown) | CorpusGraphBit(GraphNodeId::BloomUp) |
	CorpusGraphBit(GraphNodeId::NormalComposite) |
	CorpusGraphBit(GraphNodeId::MotionNormal) |
	CorpusGraphBit(GraphNodeId::NormalUi) | CorpusGraphBit(GraphNodeId::Present);

constexpr uint64_t kAllResolveGraph =
	CorpusGraphBit(GraphNodeId::ResolveColor) |
	CorpusGraphBit(GraphNodeId::ResolveDepth) |
	CorpusGraphBit(GraphNodeId::ResolveVelocity) |
	CorpusGraphBit(GraphNodeId::ResolveObjectId) |
	CorpusGraphBit(GraphNodeId::ResolveProtectionMask) |
	CorpusGraphBit(GraphNodeId::ResolveAoClass);

constexpr uint64_t kCockpitGraph =
	CorpusGraphBit(GraphNodeId::CockpitLinearCopy) |
	CorpusGraphBit(GraphNodeId::MotionCockpitPre) |
	CorpusGraphBit(GraphNodeId::CockpitUiPre) |
	CorpusGraphBit(GraphNodeId::CockpitScene) |
	CorpusGraphBit(GraphNodeId::PostAlphaClear) |
	CorpusGraphBit(GraphNodeId::CockpitResolve) |
	CorpusGraphBit(GraphNodeId::BloomDeferred) |
	CorpusGraphBit(GraphNodeId::CockpitOver) |
	CorpusGraphBit(GraphNodeId::CockpitBloomGamma) |
	CorpusGraphBit(GraphNodeId::CockpitUiPost) |
	CorpusGraphBit(GraphNodeId::Present);

} // namespace

const CaptureArtifactContract kCaptureArtifactContract[] = {
	{ "trace.commands", ComparisonClass::Exact, 0, 0, 1, 0, 0 },
	{ "trace.states", ComparisonClass::Exact, 0, 0, 1, 0, 0 },
	{ "trace.materials", ComparisonClass::Exact, 0, 0, 1, 0, 0 },
	{ "trace.transforms", ComparisonClass::Exact, 0, 0, 1, 0, 0 },
	{ "trace.payload_bindings", ComparisonClass::Exact, 0, 0, 1, 0, 0 },
	{ "trace.texture_versions", ComparisonClass::Exact, 0, 0, 1, 0, 0 },
	{ "trace.target_signatures", ComparisonClass::Exact, 0, 0, 1, 0, 0 },
	// Device/queue/image-count/generation identity differs by backend and is
	// retained for diagnosis rather than used as a GL4/Vulkan equality oracle.
	{ "trace.wsi_signatures", ComparisonClass::DiagnosticOnly, 0, 0, 0, 0, 0 },
	{ "scene.color.pre_capture", ComparisonClass::ExactDefinedUnorm, 0, 0, 1, 0, 0 },
	{ "scene.color.post_capture", ComparisonClass::ExactDefinedUnorm, 0, 0, 1, 0, 0 },
	{ "scene.depth", ComparisonClass::DepthFloat, 2.0e-6f, 2.0e-6f, 1, 0, 0 },
	{ "scene.velocity", ComparisonClass::VelocityFloat, 2.0e-5f, 2.0e-5f, 1, 0, 0 },
	{ "scene.object_id", ComparisonClass::Exact, 0, 0, 1, 0, 0 },
	{ "scene.protection_mask", ComparisonClass::ExactDefinedUnorm, 0, 0, 1, 0, 0 },
	{ "scene.ao_class", ComparisonClass::ExactDefinedUnorm, 0, 0, 1, 0, 0 },
	{ "resolve.color", ComparisonClass::ExactDefinedUnorm, 0, 0, 1, 0, 0 },
	{ "resolve.depth", ComparisonClass::DepthFloat, 2.0e-6f, 2.0e-6f, 1, 0, 0 },
	{ "resolve.velocity", ComparisonClass::VelocityFloat, 2.0e-5f, 2.0e-5f, 1, 0, 0 },
	{ "resolve.object_id", ComparisonClass::Exact, 0, 0, 1, 0, 0 },
	{ "resolve.protection_mask", ComparisonClass::ExactDefinedUnorm, 0, 0, 1, 0, 0 },
	{ "resolve.ao_class", ComparisonClass::ExactDefinedUnorm, 0, 0, 1, 0, 0 },
	{ "ssaa.4to2", ComparisonClass::ExactDefinedUnorm, 0, 0, 1, 0, 0 },
	{ "ssaa.2to1", ComparisonClass::ExactDefinedUnorm, 0, 0, 1, 0, 0 },
	{ "capture.world", ComparisonClass::ExactDefinedUnorm, 0, 0, 1, 0, 0 },
	{ "capture.depth_logical", ComparisonClass::DepthFloat, 2.0e-6f, 2.0e-6f, 1, 0, 0 },
	{ "gtao.depth_weight", ComparisonClass::GtaoFloat, 2.0e-5f, 2.0e-5f, 1, 0, 0 },
	{ "gtao.raw", ComparisonClass::GtaoFloat, 2.0e-5f, 2.0e-5f, 1, 0, 0 },
	{ "gtao.blur_x", ComparisonClass::GtaoFloat, 2.0e-5f, 2.0e-5f, 1, 0, 0 },
	{ "gtao.blur_y", ComparisonClass::GtaoFloat, 2.0e-5f, 2.0e-5f, 1, 0, 0 },
	{ "gtao.current", ComparisonClass::GtaoFloat, 2.0e-5f, 2.0e-5f, 1, 0, 0 },
	{ "gtao.suppression", ComparisonClass::ExactDefinedUnorm, 0, 0, 1, 0, 0 },
	{ "gtao.history_previous", ComparisonClass::TemporalSequence, 2.0e-5f, 2.0e-5f, 1, 0, 0 },
	{ "gtao.history_next", ComparisonClass::TemporalSequence, 2.0e-5f, 2.0e-5f, 1, 0, 0 },
	{ "gtao.applied", ComparisonClass::ExactDefinedUnorm, 0, 0, 1, 0, 0 },
	{ "gtao.deferred_composite", ComparisonClass::ExactDefinedUnorm, 0, 0, 1, 0, 0 },
	{ "bloom.threshold", ComparisonClass::ExactDefinedUnorm, 0, 0, 1, 0, 0 },
	{ "bloom.down", ComparisonClass::ExactDefinedUnorm, 0, 0, 1, 0, 0 },
	{ "bloom.up", ComparisonClass::ExactDefinedUnorm, 0, 0, 1, 0, 0 },
	{ "post.normal_before_motion", ComparisonClass::ExactDefinedUnorm, 0, 0, 1, 0, 0 },
	{ "post.cockpit_before_motion", ComparisonClass::ExactDefinedUnorm, 0, 0, 1, 0, 0 },
	{ "motion.normal", ComparisonClass::TemporalSequence, 2.0e-5f, 2.0e-5f, 1, 0, 0 },
	{ "motion.cockpit", ComparisonClass::TemporalSequence, 2.0e-5f, 2.0e-5f, 1, 0, 0 },
	{ "cockpit.scene", ComparisonClass::ExactDefinedUnorm, 0, 0, 1, 0, 0 },
	{ "cockpit.resolved", ComparisonClass::ExactDefinedUnorm, 0, 0, 1, 0, 0 },
	{ "cockpit.alpha", ComparisonClass::ExactDefinedUnorm, 0, 0, 1, 0, 0 },
	{ "cockpit.deferred_bloom_input", ComparisonClass::ExactDefinedUnorm, 0, 0, 1, 0, 0 },
	{ "cockpit.composited", ComparisonClass::ExactDefinedUnorm, 0, 0, 1, 0, 0 },
	{ "final.logical", ComparisonClass::FinalRgba, 0, 0, 0.999f, 2.0f / 255.0f, 4.0f / 255.0f },
	{ "present.swapchain", ComparisonClass::FinalRgba, 0, 0, 0.999f, 2.0f / 255.0f, 4.0f / 255.0f },
};

const size_t kCaptureArtifactContractCount =
	sizeof(kCaptureArtifactContract) / sizeof(kCaptureArtifactContract[0]);

#define GRAPH_ARTIFACT(node, semantic, comparison, absolute, relative, fraction, channel, maximum) \
	{ GraphNodeId::node, { semantic, ComparisonClass::comparison, absolute, relative, \
		fraction, channel, maximum }, 1 }

// One semantic record per frozen graph node. Repeated bloom invocations use
// the artifact filename's invocation index; no wildcard may stand in for a
// missing level/history invocation.
const GraphArtifactContract kGraphArtifactContract[] = {
	GRAPH_ARTIFACT(CapWorld, "capture.world", ExactDefinedUnorm, 0,0,1,0,0),
	GRAPH_ARTIFACT(CapDepthLogical, "capture.depth_logical", DepthFloat, 2e-6f,2e-6f,1,0,0),
	GRAPH_ARTIFACT(ResolveColor, "resolve.color", ExactDefinedUnorm, 0,0,1,0,0),
	GRAPH_ARTIFACT(ResolveDepth, "resolve.depth", DepthFloat, 2e-6f,2e-6f,1,0,0),
	GRAPH_ARTIFACT(ResolveVelocity, "resolve.velocity", VelocityFloat, 2e-5f,2e-5f,1,0,0),
	GRAPH_ARTIFACT(ResolveObjectId, "resolve.object_id", Exact, 0,0,1,0,0),
	GRAPH_ARTIFACT(ResolveProtectionMask, "resolve.protection_mask", ExactDefinedUnorm, 0,0,1,0,0),
	GRAPH_ARTIFACT(ResolveAoClass, "resolve.ao_class", ExactDefinedUnorm, 0,0,1,0,0),
	GRAPH_ARTIFACT(Ssaa4To2, "ssaa.4to2", ExactDefinedUnorm, 0,0,1,0,0),
	GRAPH_ARTIFACT(Ssaa2To1, "ssaa.2to1", ExactDefinedUnorm, 0,0,1,0,0),
	GRAPH_ARTIFACT(PrepareDepthLogical, "post.depth_logical", DepthFloat, 2e-6f,2e-6f,1,0,0),
	GRAPH_ARTIFACT(AoDepth, "gtao.depth_weight", GtaoFloat, 2e-5f,2e-5f,1,0,0),
	GRAPH_ARTIFACT(AoRaw, "gtao.raw", GtaoFloat, 2e-5f,2e-5f,1,0,0),
	GRAPH_ARTIFACT(AoBlurX, "gtao.blur_x", GtaoFloat, 2e-5f,2e-5f,1,0,0),
	GRAPH_ARTIFACT(AoBlurY, "gtao.blur_y", GtaoFloat, 2e-5f,2e-5f,1,0,0),
	GRAPH_ARTIFACT(AoTemporal, "gtao.history_next", TemporalSequence, 2e-5f,2e-5f,1,0,0),
	GRAPH_ARTIFACT(AoSuppress, "gtao.suppression", ExactDefinedUnorm, 0,0,1,0,0),
	GRAPH_ARTIFACT(AoApply, "gtao.applied", ExactDefinedUnorm, 0,0,1,0,0),
	GRAPH_ARTIFACT(AoDeferredComposite, "gtao.deferred_composite", ExactDefinedUnorm, 0,0,1,0,0),
	GRAPH_ARTIFACT(BloomThreshold, "bloom.threshold", ExactDefinedUnorm, 0,0,1,0,0),
	GRAPH_ARTIFACT(BloomDown, "bloom.down", ExactDefinedUnorm, 0,0,1,0,0),
	GRAPH_ARTIFACT(BloomUp, "bloom.up", ExactDefinedUnorm, 0,0,1,0,0),
	GRAPH_ARTIFACT(NormalComposite, "post.normal_bloom_gamma", ExactDefinedUnorm, 0,0,1,0,0),
	GRAPH_ARTIFACT(NormalBlit, "post.normal_gamma", ExactDefinedUnorm, 0,0,1,0,0),
	GRAPH_ARTIFACT(MotionNormal, "post.normal_motion", TemporalSequence, 2e-5f,2e-5f,1,0,0),
	GRAPH_ARTIFACT(MotionDebugNormal, "post.normal_motion_debug", ExactDefinedUnorm, 0,0,1,0,0),
	GRAPH_ARTIFACT(NormalUi, "post.normal_ui", RasterEdgeQualified, 0,0,1,0,0),
	GRAPH_ARTIFACT(CockpitLinearCopy, "post.cockpit_linear", ExactDefinedUnorm, 0,0,1,0,0),
	GRAPH_ARTIFACT(MotionCockpitPre, "post.cockpit_motion", TemporalSequence, 2e-5f,2e-5f,1,0,0),
	GRAPH_ARTIFACT(MotionDebugCockpitPre, "post.cockpit_motion_debug", ExactDefinedUnorm, 0,0,1,0,0),
	GRAPH_ARTIFACT(CockpitUiPre, "post.cockpit_ui_pre", RasterEdgeQualified, 0,0,1,0,0),
	GRAPH_ARTIFACT(CockpitScene, "cockpit.scene", RasterEdgeQualified, 0,0,1,0,0),
	GRAPH_ARTIFACT(PostAlphaClear, "post.alpha_cleared", ExactDefinedUnorm, 0,0,1,0,0),
	GRAPH_ARTIFACT(CockpitResolve, "cockpit.resolved", ExactDefinedUnorm, 0,0,1,0,0),
	GRAPH_ARTIFACT(BloomDeferred, "bloom.deferred", ExactDefinedUnorm, 0,0,1,0,0),
	GRAPH_ARTIFACT(CockpitOver, "cockpit.composited", ExactDefinedUnorm, 0,0,1,0,0),
	GRAPH_ARTIFACT(CockpitBloomGamma, "post.cockpit_bloom_gamma", ExactDefinedUnorm, 0,0,1,0,0),
	GRAPH_ARTIFACT(CockpitGammaOnly, "post.cockpit_gamma", ExactDefinedUnorm, 0,0,1,0,0),
	GRAPH_ARTIFACT(CockpitUiPost, "post.cockpit_ui_post", RasterEdgeQualified, 0,0,1,0,0),
	GRAPH_ARTIFACT(Present, "present.swapchain", FinalRgba, 0,0,0.999f,2.0f/255.0f,4.0f/255.0f),
};

const size_t kGraphArtifactContractCount =
	sizeof(kGraphArtifactContract) / sizeof(kGraphArtifactContract[0]);

#undef GRAPH_ARTIFACT

const ArtifactNameGrammarContract kArtifactNameGrammarContract = {
	"{case_id}/{preference_id}/{backend}/f{case_frame:06}/"
	"n{node_id:02}-{node_name}-i{invocation:02}-{semantic}-{format}-"
	"{width}x{height}x{layers}-s{samples}.piccuatt;"
	"x-{explicit_stage}-i{invocation:02}-{semantic}-{format}-"
	"{width}x{height}x{layers}-s{samples}.piccuatt;"
	"trace-s{segment:03}.piccutrace",
	1, 1, 1, 1, 1, 1
};

const CorpusAssetContract kCorpusAssetContract[] = {
	{ CorpusAsset::Level1, "asset.level1.v1",
		"scripts/data/linuxdemohog/level1.d3l",
		"A67CADAF906D31A2373F1DC19FA5370B68967D10462DDB6427864C00056FB5E9",
		127, "PTMC Data Retention Center", 102, 21715, 18988, 73747, 366, 55508 },
	{ CorpusAsset::Polaris, "asset.polaris.v1",
		"scripts/data/linuxdemohog/Polaris.d3l",
		"E406E85B4F111DEF13040EDB9953F6477EF8F81BBD487AD2303535D7D051D6CC",
		127, "Polaris", 108, 7944, 6356, 26078, 256, 145328 },
	{ CorpusAsset::Taurus, "asset.taurus.v1",
		"scripts/data/linuxdemohog/Taurus.d3l",
		"94EF815238A9071B8AC9B780F135F00910223964892E5A1847E1EAF87452B955",
		127, "Taurus", 24, 2564, 2194, 8888, 124, 3896 },
	{ CorpusAsset::TheCore, "asset.thecore.v1",
		"scripts/data/linuxdemohog/thecore.d3l",
		"08639458E8282DFDF2593ACC5E4E757E7C75D396E01F7CA96E817F51CC44BF5E",
		128, "The Core", 18, 1817, 1510, 6104, 76, 3896 },
};

const size_t kCorpusAssetContractCount =
	sizeof(kCorpusAssetContract) / sizeof(kCorpusAssetContract[0]);

const CorpusInputActionContract kCorpusInputActionContract[] = {
	{ CorpusInputSchedule::Fire4, 60, 60, CorpusInputAction::FirePrimaryPulse, 1.0f/60.0f, 1 },
	{ CorpusInputSchedule::Fire4, 90, 90, CorpusInputAction::FirePrimaryPulse, 1.0f/60.0f, 1 },
	{ CorpusInputSchedule::Fire4, 120, 120, CorpusInputAction::FirePrimaryPulse, 1.0f/60.0f, 1 },
	{ CorpusInputSchedule::Fire4, 150, 150, CorpusInputAction::FirePrimaryPulse, 1.0f/60.0f, 1 },
	{ CorpusInputSchedule::Motion, 30, 89, CorpusInputAction::HeadingThrust, 0.25f, 0 },
	{ CorpusInputSchedule::Motion, 90, 149, CorpusInputAction::ForwardThrust, 0.50f, 0 },
	{ CorpusInputSchedule::Motion, 150, 179, CorpusInputAction::VerticalThrust, 0.25f, 0 },
	{ CorpusInputSchedule::Yaw, 30, 149, CorpusInputAction::HeadingThrust, 0.25f, 0 },
	{ CorpusInputSchedule::Rearview, 90, 90, CorpusInputAction::RearviewTogglePulse, 1.0f, 1 },
	{ CorpusInputSchedule::Rearview, 150, 150, CorpusInputAction::RearviewTogglePulse, 1.0f, 1 },
};

const size_t kCorpusInputActionContractCount =
	sizeof(kCorpusInputActionContract) / sizeof(kCorpusInputActionContract[0]);

const CorpusInputScheduleContract kCorpusInputScheduleContract[] = {
	{ CorpusInputSchedule::Hold, "input.hold.v1", 0, 0, 1, 0 },
	{ CorpusInputSchedule::Fire4, "input.fire4.v1", 0, 4, 1, 0 },
	{ CorpusInputSchedule::Motion, "input.motion.v1", 4, 3, 1, 0 },
	{ CorpusInputSchedule::Yaw, "input.yaw.v1", 7, 1, 1, 0 },
	{ CorpusInputSchedule::Rearview, "input.rearview.v1", 8, 2, 1, 0 },
};

const size_t kCorpusInputScheduleContractCount =
	sizeof(kCorpusInputScheduleContract) / sizeof(kCorpusInputScheduleContract[0]);

const CorpusPreferenceContract kCorpusPreferenceContract[] = {
	{ CorpusPreference::Authored720p, "pref.authored.720p.v1",
		MakeCorpusRendererPreference(0,1,0), MakeCorpusEngineSettings(CorpusHudMode::GameDefault), 1 },
	{ CorpusPreference::FullPost720p, "pref.fullpost.720p.v1",
		MakeCorpusRendererPreference(1,1,0), MakeCorpusEngineSettings(CorpusHudMode::GameDefault), 1 },
	{ CorpusPreference::FullPostSsaa2Msaa4, "pref.fullpost.ssaa2.msaa4.720p.v1",
		MakeCorpusRendererPreference(1,2,4), MakeCorpusEngineSettings(CorpusHudMode::GameDefault), 1 },
	{ CorpusPreference::FullPostMsaa8, "pref.fullpost.msaa8.720p.v1",
		MakeCorpusRendererPreference(1,1,8), MakeCorpusEngineSettings(CorpusHudMode::GameDefault), 1 },
	{ CorpusPreference::FullPostSsaa4, "pref.fullpost.ssaa4.720p.v1",
		MakeCorpusRendererPreference(1,4,0), MakeCorpusEngineSettings(CorpusHudMode::GameDefault), 1 },
	{ CorpusPreference::FullPostCockpit, "pref.fullpost.cockpit.720p.v1",
		MakeCorpusRendererPreference(1,1,0), MakeCorpusEngineSettings(CorpusHudMode::Cockpit), 1 },
	{ CorpusPreference::FullPostRearview, "pref.fullpost.rearview.720p.v1",
		MakeCorpusRendererPreference(1,1,0), MakeCorpusEngineSettings(CorpusHudMode::Fullscreen), 1 },
	{ CorpusPreference::FullPostMsaa2, "pref.fullpost.msaa2.720p.v1",
		MakeCorpusRendererPreference(1,1,2), MakeCorpusEngineSettings(CorpusHudMode::GameDefault), 1 },
	{ CorpusPreference::FullPostGammaLow, "pref.fullpost.gamma0_5.720p.v1",
		MakeCorpusRendererPreferenceWithGamma(0.5f),
		MakeCorpusEngineSettings(CorpusHudMode::GameDefault), 1 },
	{ CorpusPreference::FullPostGammaHigh, "pref.fullpost.gamma2_0.720p.v1",
		MakeCorpusRendererPreferenceWithGamma(2.0f),
		MakeCorpusEngineSettings(CorpusHudMode::GameDefault), 1 },
};

const size_t kCorpusPreferenceContractCount =
	sizeof(kCorpusPreferenceContract) / sizeof(kCorpusPreferenceContract[0]);

#define EVENT_KIND(kind, id, domain, argument_count, fault) \
	{ CorpusEventKind::kind, id, CorpusEventArgumentDomain::domain, argument_count, fault }

const CorpusEventKindContract kCorpusEventKindContract[] = {
	EVENT_KIND(CaptureCheckpoint, "event.capture_checkpoint.v1", None, 1, 0),
	EVENT_KIND(MutateTexture, "event.mutate_texture.v1", TextureMutation, 4, 0),
	EVENT_KIND(MutateLightmap, "event.mutate_lightmap.v1", LightmapMutation, 2, 0),
	EVENT_KIND(DestroyFace, "event.destroy_face.v1", None, 1, 0),
	EVENT_KIND(SetTerrainCamera, "event.terrain_camera.v1", TerrainCamera, 5, 0),
	EVENT_KIND(SetTerrainFallback, "event.terrain_fallback.v1", TerrainFallback, 4, 0),
	EVENT_KIND(SetMirrorVariant, "event.mirror_variant.v1", MirrorVariant, 3, 0),
	EVENT_KIND(SetFogVariant, "event.fog_variant.v1", FogVariant, 3, 0),
	EVENT_KIND(SpawnRoomEffect, "event.room_effect.v1", RoomEffect, 4, 0),
	EVENT_KIND(SetModelVariant, "event.model_variant.v1", ModelVariant, 6, 0),
	EVENT_KIND(SetLightingVariant, "event.lighting_variant.v1", LightingVariant, 3, 0),
	EVENT_KIND(SetSpecularVariant, "event.specular_variant.v1", SpecularVariant, 6, 0),
	EVENT_KIND(SetMotionGhostVariant, "event.motion_ghost.v1", MotionGhostVariant, 2, 0),
	EVENT_KIND(SpawnEffectVariant, "event.effect_variant.v1", EffectVariant, 7, 0),
	EVENT_KIND(DrawPrimitiveVariant, "event.primitive_variant.v1", PrimitiveVariant, 5, 0),
	EVENT_KIND(SetBlendVariant, "event.blend_variant.v1", BlendVariant, 5, 0),
	EVENT_KIND(SetPostVariant, "event.post_variant.v1", PostVariant, 9, 0),
	EVENT_KIND(SetGammaPreset, "event.gamma_preset.v1", GammaPreset, 2, 0),
	EVENT_KIND(SetPauseState, "event.pause_state.v1", Boolean, 2, 0),
	EVENT_KIND(CameraCut, "event.camera_cut.v1", None, 1, 0),
	EVENT_KIND(ResizePreset, "event.resize_preset.v1", ResizePreset, 3, 0),
	EVENT_KIND(SetSmallView, "event.small_view.v1", SmallView, 2, 0),
	EVENT_KIND(SetCockpitMode, "event.cockpit_mode.v1", CockpitMode, 4, 0),
	EVENT_KIND(SetCockpitLayer, "event.cockpit_layer.v1", CockpitLayer, 6, 0),
	EVENT_KIND(ShowMessageConsole, "event.message_console.v1", None, 1, 0),
	EVENT_KIND(SetUiFlow, "event.ui_flow.v1", UiFlow, 4, 0),
	EVENT_KIND(SetMediaFlow, "event.media_flow.v1", MediaFlow, 3, 0),
	EVENT_KIND(UpdateMovieBitmap, "event.movie_bitmap.v1", TextureMutation, 4, 0),
	EVENT_KIND(DrawChunkedBitmap, "event.chunked_bitmap.v1", None, 1, 0),
	EVENT_KIND(RequestScreenshot, "event.screenshot.v1", ScreenshotKind, 3, 0),
	EVENT_KIND(EditorDrawPrimitive, "event.editor_primitive.v1", EditorPrimitive, 5, 0),
	EVENT_KIND(ReadPixelLoop, "event.read_pixel_loop.v1", None, 1, 0),
	EVENT_KIND(FullscreenTransition, "event.fullscreen_transition.v1", Boolean, 2, 0),
	EVENT_KIND(SetMinimized, "event.minimized.v1", Boolean, 2, 0),
	EVENT_KIND(InjectFault, "event.inject_fault.v1", Fault,
		static_cast<uint32_t>(CorpusFault::Count), 1),
	EVENT_KIND(ShutdownRenderer, "event.shutdown_renderer.v1", None, 1, 0),
	EVENT_KIND(SweepPreferredField, "event.preferred_field.v1", PreferredField,
		static_cast<uint32_t>(CorpusPreferredField::Count), 0),
	EVENT_KIND(SweepEngineField, "event.engine_field.v1", EngineField,
		static_cast<uint32_t>(CorpusEngineField::Count), 0),
};

#undef EVENT_KIND

const size_t kCorpusEventKindContractCount =
	sizeof(kCorpusEventKindContract) / sizeof(kCorpusEventKindContract[0]);

#define EVENT(schedule, tick, event, argument) \
	{ CorpusEventSchedule::schedule, tick, CorpusEventKind::event, argument, 1, CorpusFault::None }
#define REPEAT_EVENT(schedule, tick, event, argument, count) \
	{ CorpusEventSchedule::schedule, tick, CorpusEventKind::event, argument, count, CorpusFault::None }
#define FAULT_EVENT(schedule, tick, fault) \
	{ CorpusEventSchedule::schedule, tick, CorpusEventKind::InjectFault, \
		static_cast<uint32_t>(CorpusFault::fault), 1, CorpusFault::fault }

const CorpusEventActionContract kCorpusEventActionContract[] = {
	EVENT(Steady, 0, CaptureCheckpoint, 0),

	EVENT(IndoorMutation, 60, MutateTexture, 0),
	EVENT(IndoorMutation, 75, MutateTexture, 1),
	EVENT(IndoorMutation, 90, DestroyFace, 0),
	EVENT(IndoorMutation, 120, MutateLightmap, 0),
	EVENT(IndoorMutation, 150, MutateTexture, 2),

	EVENT(TerrainSweep, 60, SetTerrainCamera, 0),
	EVENT(TerrainSweep, 75, SetTerrainCamera, 1),
	EVENT(TerrainSweep, 90, SetTerrainCamera, 2),
	EVENT(TerrainSweep, 105, SetTerrainCamera, 3),
	EVENT(TerrainSweep, 120, SetTerrainCamera, 4),
	EVENT(TerrainSweep, 135, SetTerrainFallback, 0),
	EVENT(TerrainSweep, 150, SetTerrainFallback, 1),
	EVENT(TerrainSweep, 165, SetTerrainFallback, 2),
	EVENT(TerrainSweep, 180, SetTerrainFallback, 3),

	EVENT(MirrorFogEffects, 60, SetMirrorVariant, 0),
	EVENT(MirrorFogEffects, 75, SetMirrorVariant, 1),
	EVENT(MirrorFogEffects, 90, SetMirrorVariant, 2),
	EVENT(MirrorFogEffects, 105, SetFogVariant, 0),
	EVENT(MirrorFogEffects, 120, SetFogVariant, 1),
	EVENT(MirrorFogEffects, 135, SetFogVariant, 2),
	EVENT(MirrorFogEffects, 150, SpawnRoomEffect, 0),
	EVENT(MirrorFogEffects, 155, SpawnRoomEffect, 1),
	EVENT(MirrorFogEffects, 160, SpawnRoomEffect, 2),
	EVENT(MirrorFogEffects, 165, SpawnRoomEffect, 3),

	EVENT(ModelModes, 60, SetModelVariant, 0),
	EVENT(ModelModes, 75, SetModelVariant, 1),
	EVENT(ModelModes, 90, SetModelVariant, 2),
	EVENT(ModelModes, 105, SetModelVariant, 3),
	EVENT(ModelModes, 120, SetModelVariant, 4),
	EVENT(ModelModes, 135, SetModelVariant, 5),
	EVENT(ModelModes, 150, SetLightingVariant, 0),
	EVENT(ModelModes, 155, SetLightingVariant, 1),
	EVENT(ModelModes, 160, SetLightingVariant, 2),
	EVENT(ModelModes, 165, SetSpecularVariant, 0),
	EVENT(ModelModes, 168, SetSpecularVariant, 1),
	EVENT(ModelModes, 171, SetSpecularVariant, 2),
	EVENT(ModelModes, 174, SetSpecularVariant, 3),
	EVENT(ModelModes, 177, SetSpecularVariant, 4),
	EVENT(ModelModes, 180, SetSpecularVariant, 5),
	EVENT(ModelModes, 183, SetMotionGhostVariant, 0),
	EVENT(ModelModes, 186, SetMotionGhostVariant, 1),

	EVENT(EffectStress, 60, SpawnEffectVariant, 0),
	EVENT(EffectStress, 70, SpawnEffectVariant, 1),
	EVENT(EffectStress, 80, SpawnEffectVariant, 2),
	EVENT(EffectStress, 90, SpawnEffectVariant, 3),
	EVENT(EffectStress, 100, SpawnEffectVariant, 4),
	EVENT(EffectStress, 110, SpawnEffectVariant, 5),
	EVENT(EffectStress, 120, SpawnEffectVariant, 6),
	EVENT(EffectStress, 130, DrawPrimitiveVariant, 0),
	EVENT(EffectStress, 135, DrawPrimitiveVariant, 1),
	EVENT(EffectStress, 140, DrawPrimitiveVariant, 2),
	EVENT(EffectStress, 145, DrawPrimitiveVariant, 3),
	EVENT(EffectStress, 150, DrawPrimitiveVariant, 4),
	EVENT(EffectStress, 160, SetBlendVariant, 0),
	EVENT(EffectStress, 165, SetBlendVariant, 1),
	EVENT(EffectStress, 170, SetBlendVariant, 2),
	EVENT(EffectStress, 175, SetBlendVariant, 3),
	EVENT(EffectStress, 180, SetBlendVariant, 4),

	EVENT(PostVariants, 60, SetPostVariant, 0),
	EVENT(PostVariants, 75, SetPostVariant, 1),
	EVENT(PostVariants, 90, SetPostVariant, 2),
	EVENT(PostVariants, 105, SetPostVariant, 3),
	EVENT(PostVariants, 120, SetPostVariant, 4),
	EVENT(PostVariants, 135, SetPostVariant, 5),
	EVENT(PostVariants, 150, SetPostVariant, 6),
	EVENT(PostVariants, 165, SetPostVariant, 7),
	EVENT(PostVariants, 180, SetPostVariant, 8),

	FAULT_EVENT(Msaa2Fallback, 0, ForceMsaa2To1Fallback),
	EVENT(Msaa2Fallback, 120, CaptureCheckpoint, 0),
	EVENT(GammaLow, 0, SetGammaPreset, 0),
	EVENT(GammaLow, 120, CaptureCheckpoint, 0),
	EVENT(GammaHigh, 0, SetGammaPreset, 1),
	EVENT(GammaHigh, 120, CaptureCheckpoint, 0),

	EVENT(HistoryTransitions, 60, SetPauseState, 1),
	EVENT(HistoryTransitions, 90, SetPauseState, 0),
	EVENT(HistoryTransitions, 120, CameraCut, 0),
	EVENT(HistoryTransitions, 150, ResizePreset, 1),
	EVENT(HistoryTransitions, 180, ResizePreset, 0),
	EVENT(HistoryTransitions, 183, CaptureCheckpoint, 0),

	EVENT(SmallViews, 60, SetSmallView, 0),
	EVENT(SmallViews, 90, SetSmallView, 1),
	EVENT(SmallViews, 120, CaptureCheckpoint, 0),

	EVENT(CockpitModes, 60, SetCockpitMode, 0),
	EVENT(CockpitModes, 90, SetCockpitMode, 1),
	EVENT(CockpitModes, 120, SetCockpitMode, 2),
	EVENT(CockpitModes, 150, SetCockpitMode, 3),
	EVENT(CockpitModes, 155, SetCockpitLayer, 0),
	EVENT(CockpitModes, 160, SetCockpitLayer, 1),
	EVENT(CockpitModes, 165, SetCockpitLayer, 2),
	EVENT(CockpitModes, 170, SetCockpitLayer, 3),
	EVENT(CockpitModes, 175, SetCockpitLayer, 4),
	EVENT(CockpitModes, 180, SetCockpitLayer, 5),
	EVENT(CockpitModes, 185, ShowMessageConsole, 0),

	EVENT(UiMenuFonts, 60, SetUiFlow, 0),
	EVENT(UiMenuFonts, 90, SetUiFlow, 1),
	EVENT(UiMenuFonts, 120, SetUiFlow, 2),
	EVENT(UiMenuFonts, 150, SetUiFlow, 3),
	EVENT(NarrativeMedia, 60, SetMediaFlow, 0),
	EVENT(NarrativeMedia, 90, SetMediaFlow, 1),
	EVENT(NarrativeMedia, 120, SetMediaFlow, 2),
	EVENT(MovieMutation, 60, UpdateMovieBitmap, 0),
	EVENT(MovieMutation, 90, UpdateMovieBitmap, 3),
	EVENT(MovieMutation, 120, DrawChunkedBitmap, 0),
	EVENT(Screenshots, 60, RequestScreenshot, 0),
	EVENT(Screenshots, 90, RequestScreenshot, 1),
	EVENT(Screenshots, 120, RequestScreenshot, 2),

	EVENT(EditorDraw, 60, EditorDrawPrimitive, 0),
	EVENT(EditorDraw, 75, EditorDrawPrimitive, 1),
	EVENT(EditorDraw, 90, EditorDrawPrimitive, 2),
	EVENT(EditorDraw, 105, EditorDrawPrimitive, 3),
	EVENT(EditorDraw, 120, EditorDrawPrimitive, 4),
	EVENT(EditorReadback, 60, EditorDrawPrimitive, 0),
	REPEAT_EVENT(EditorReadback, 90, ReadPixelLoop, 0, 32),

	EVENT(PlatformResize, 60, ResizePreset, 1),
	EVENT(PlatformResize, 75, FullscreenTransition, 1),
	EVENT(PlatformResize, 90, SetMinimized, 1),
	EVENT(PlatformResize, 105, SetMinimized, 0),
	EVENT(PlatformResize, 120, ResizePreset, 2),
	EVENT(PlatformResize, 150, ResizePreset, 0),
	EVENT(PlatformResize, 165, FullscreenTransition, 0),
	FAULT_EVENT(SurfaceFaults, 60, AcquireOutOfDate),
	FAULT_EVENT(SurfaceFaults, 90, PresentSuboptimal),
	FAULT_EVENT(SurfaceFaults, 120, SurfaceLost),
	EVENT(SurfaceFaults, 150, CaptureCheckpoint, 0),
	FAULT_EVENT(StartupFallback, 0, RejectRequiredDeviceFeature),
	EVENT(StartupFallback, 1, ShutdownRenderer, 0),

	REPEAT_EVENT(PreferenceSweep, 60, SweepPreferredField, 0,
		static_cast<uint32_t>(CorpusPreferredField::Count)),
	REPEAT_EVENT(PreferenceSweep, 180, SweepEngineField, 0,
		static_cast<uint32_t>(CorpusEngineField::Count)),
	EVENT(PreferenceSweep, 220, CaptureCheckpoint, 0),
};

#undef FAULT_EVENT
#undef REPEAT_EVENT
#undef EVENT

const size_t kCorpusEventActionContractCount =
	sizeof(kCorpusEventActionContract) / sizeof(kCorpusEventActionContract[0]);

const CorpusEventScheduleContract kCorpusEventScheduleContract[] = {
	{ CorpusEventSchedule::Steady, "schedule.steady.v1", 0, 1 },
	{ CorpusEventSchedule::IndoorMutation, "schedule.indoor_mutation.v1", 1, 5 },
	{ CorpusEventSchedule::TerrainSweep, "schedule.terrain_sweep.v1", 6, 9 },
	{ CorpusEventSchedule::MirrorFogEffects, "schedule.mirror_fog_effects.v1", 15, 10 },
	{ CorpusEventSchedule::ModelModes, "schedule.model_modes.v1", 25, 17 },
	{ CorpusEventSchedule::EffectStress, "schedule.effect_stress.v1", 42, 17 },
	{ CorpusEventSchedule::PostVariants, "schedule.post_variants.v1", 59, 9 },
	{ CorpusEventSchedule::Msaa2Fallback, "schedule.msaa2_fallback.v1", 68, 2 },
	{ CorpusEventSchedule::GammaLow, "schedule.gamma_low.v1", 70, 2 },
	{ CorpusEventSchedule::GammaHigh, "schedule.gamma_high.v1", 72, 2 },
	{ CorpusEventSchedule::HistoryTransitions, "schedule.history_transitions.v1", 74, 6 },
	{ CorpusEventSchedule::SmallViews, "schedule.small_views.v1", 80, 3 },
	{ CorpusEventSchedule::CockpitModes, "schedule.cockpit_modes.v1", 83, 11 },
	{ CorpusEventSchedule::UiMenuFonts, "schedule.ui_menu_fonts.v1", 94, 4 },
	{ CorpusEventSchedule::NarrativeMedia, "schedule.narrative_media.v1", 98, 3 },
	{ CorpusEventSchedule::MovieMutation, "schedule.movie_mutation.v1", 101, 3 },
	{ CorpusEventSchedule::Screenshots, "schedule.screenshots.v1", 104, 3 },
	{ CorpusEventSchedule::EditorDraw, "schedule.editor_draw.v1", 107, 5 },
	{ CorpusEventSchedule::EditorReadback, "schedule.editor_readback.v1", 112, 2 },
	{ CorpusEventSchedule::PlatformResize, "schedule.platform_resize.v1", 114, 7 },
	{ CorpusEventSchedule::SurfaceFaults, "schedule.surface_faults.v1", 121, 4 },
	{ CorpusEventSchedule::StartupFallback, "schedule.startup_fallback.v1", 125, 2 },
	{ CorpusEventSchedule::PreferenceSweep, "schedule.preference_sweep.v1", 127, 3 },
};

const size_t kCorpusEventScheduleContractCount =
	sizeof(kCorpusEventScheduleContract) / sizeof(kCorpusEventScheduleContract[0]);

const CorpusFrameSpan kCorpusFrameSpan[] = {
	{180,180},
	{60,63},{90,93},{120,123},{150,153},
	{120,127},{178,183},
	{180,183},
	{120,120},
	{120,120},
	{120,123},
	{88,96},{148,156},
	{58,154},
	{58,182},
	{58,167},
	{58,190},
	{58,182},
	{58,182},
	{118,123},
	{118,123},
	{118,123},
	{118,123},
	{58,185},
	{58,123},
	{58,187},
	{58,152},
	{58,122},
	{58,122},
	{58,122},
	{58,122},
	{58,123},
	{58,167},
	{58,152},
	{0,2},
	{58,223},
};

const size_t kCorpusFrameSpanCount =
	sizeof(kCorpusFrameSpan) / sizeof(kCorpusFrameSpan[0]);

const CorpusCaseContract kCorpusCaseContract[] = {
	{ CorpusCase::IndoorLevel1HoldAuthored, "indoor.level1.hold.authored.v1",
		CorpusAsset::Level1, UINT64_C(0x00000000d3449401), CorpusInputSchedule::Hold,
		120, 0, 1, CorpusPreference::Authored720p,
		CorpusGraphBit(GraphNodeId::CapWorld) |
		CorpusGraphBit(GraphNodeId::NormalBlit) | CorpusGraphBit(GraphNodeId::NormalUi) |
		CorpusGraphBit(GraphNodeId::Present),
		0,
		kCorpusScenePreCapture | kCorpusSceneAfterCapture | kCorpusFinalLogical |
		kCorpusCommandStateTrace, 1, 60, 1, CorpusEventSchedule::Steady },
	{ CorpusCase::IndoorLevel1FireFullPost, "indoor.level1.fire.fullpost.v1",
		CorpusAsset::Level1, UINT64_C(0x00000000d3449402), CorpusInputSchedule::Fire4,
		60, 1, 4, CorpusPreference::FullPost720p, kFullPostNormalGraph,
		0,
		kCorpusSceneAttachments | kCorpusFinalLogical | kCorpusCommandStateTrace,
		1, 60, 1, CorpusEventSchedule::Steady },
	{ CorpusCase::TerrainPolarisMotionFullPost, "terrain.polaris.motion.fullpost.v1",
		CorpusAsset::Polaris, UINT64_C(0x00000000d3449403), CorpusInputSchedule::Motion,
		120, 5, 2, CorpusPreference::FullPost720p, kFullPostNormalGraph,
		0,
		kCorpusSceneAttachments | kCorpusScenePreCapture | kCorpusSceneAfterCapture |
		kCorpusFinalLogical, 1, 60, 1, CorpusEventSchedule::Steady },
	{ CorpusCase::TerrainPolarisCombinedSampling, "terrain.polaris.combined_sampling.v1",
		CorpusAsset::Polaris, UINT64_C(0x00000000d3449404), CorpusInputSchedule::Motion,
		120, 7, 1, CorpusPreference::FullPostSsaa2Msaa4,
		kFullPostNormalGraph | CorpusGraphBit(GraphNodeId::Ssaa2To1),
		kAllResolveGraph,
		kCorpusSceneAttachments | kCorpusFinalLogical | kCorpusRequestedAppliedSignature,
		1, 60, 1, CorpusEventSchedule::Steady },
	{ CorpusCase::SamplingTaurusMsaa8, "sampling.taurus.msaa8.v1",
		CorpusAsset::Taurus, UINT64_C(0x00000000d3449405), CorpusInputSchedule::Hold,
		60, 8, 1, CorpusPreference::FullPostMsaa8,
		kFullPostNormalGraph,
		kAllResolveGraph,
		kCorpusRequestedAppliedSignature | kCorpusFinalLogical, 1, 60, 1,
		CorpusEventSchedule::Steady },
	{ CorpusCase::SamplingTaurusSsaa4, "sampling.taurus.ssaa4.v1",
		CorpusAsset::Taurus, UINT64_C(0x00000000d3449406), CorpusInputSchedule::Hold,
		60, 9, 1, CorpusPreference::FullPostSsaa4,
		kFullPostNormalGraph | CorpusGraphBit(GraphNodeId::Ssaa4To2) |
			CorpusGraphBit(GraphNodeId::Ssaa2To1),
		0,
		kCorpusRequestedAppliedSignature | kCorpusFinalLogical, 1, 60, 1,
		CorpusEventSchedule::Steady },
	{ CorpusCase::CockpitTheCoreFullPost, "cockpit.thecore.fullpost.v1",
		CorpusAsset::TheCore, UINT64_C(0x00000000d3449407), CorpusInputSchedule::Hold,
		60, 10, 1, CorpusPreference::FullPostCockpit,
		(kFullPostNormalGraph & ~(CorpusGraphBit(GraphNodeId::NormalComposite) |
		 CorpusGraphBit(GraphNodeId::MotionNormal) | CorpusGraphBit(GraphNodeId::NormalUi) |
		 CorpusGraphBit(GraphNodeId::BloomThreshold) | CorpusGraphBit(GraphNodeId::BloomDown) |
		 CorpusGraphBit(GraphNodeId::BloomUp) |
		 CorpusGraphBit(GraphNodeId::Present))) | kCockpitGraph,
		0,
		kCorpusCockpitAlpha | kCorpusDeferredBloomInput | kCorpusFinalLogical,
		1, 60, 1, CorpusEventSchedule::Steady },
	{ CorpusCase::HistoryTheCoreRearview, "history.thecore.rearview.v1",
		CorpusAsset::TheCore, UINT64_C(0x00000000d3449408), CorpusInputSchedule::Rearview,
		60, 11, 2, CorpusPreference::FullPostRearview, kFullPostNormalGraph,
		0,
		kCorpusCapturedMatrices | kCorpusSceneAttachments | kCorpusHistoryEvents |
		kCorpusFinalLogical, 1, 60, 1, CorpusEventSchedule::Steady },
	{ CorpusCase::IndoorLevel1Mutation, "indoor.level1.mutation.fullpost.v1",
		CorpusAsset::Level1, UINT64_C(0x00000000d3449409), CorpusInputSchedule::Hold,
		60, 13, 1, CorpusPreference::FullPost720p, kFullPostNormalGraph, 0,
		kCorpusScenePreCapture | kCorpusSceneAfterCapture | kCorpusSceneAttachments |
		kCorpusCommandStateTrace, 1, 60, 1, CorpusEventSchedule::IndoorMutation },
	{ CorpusCase::TerrainPolarisFallbacks, "terrain.polaris.fallbacks.v1",
		CorpusAsset::Polaris, UINT64_C(0x00000000d344940a), CorpusInputSchedule::Hold,
		60, 14, 1, CorpusPreference::FullPost720p, kFullPostNormalGraph, 0,
		kCorpusSceneAttachments | kCorpusFinalLogical | kCorpusCommandStateTrace,
		1, 60, 1, CorpusEventSchedule::TerrainSweep },
	{ CorpusCase::MirrorFogLevel1Effects, "mirror_fog.level1.effects.v1",
		CorpusAsset::Level1, UINT64_C(0x00000000d344940b), CorpusInputSchedule::Hold,
		60, 15, 1, CorpusPreference::FullPost720p, kFullPostNormalGraph, 0,
		kCorpusSceneAttachments | kCorpusScenePreCapture | kCorpusSceneAfterCapture,
		1, 60, 1, CorpusEventSchedule::MirrorFogEffects },
	{ CorpusCase::ModelTaurusModes, "model.taurus.modes.v1",
		CorpusAsset::Taurus, UINT64_C(0x00000000d344940c), CorpusInputSchedule::Yaw,
		60, 16, 1, CorpusPreference::FullPost720p, kFullPostNormalGraph, 0,
		kCorpusCapturedMatrices | kCorpusSceneAttachments | kCorpusCommandStateTrace,
		1, 60, 1, CorpusEventSchedule::ModelModes },
	{ CorpusCase::EffectsLevel1Stress, "effects.level1.stress.v1",
		CorpusAsset::Level1, UINT64_C(0x00000000d344940d), CorpusInputSchedule::Fire4,
		60, 17, 1, CorpusPreference::FullPost720p, kFullPostNormalGraph, 0,
		kCorpusSceneAttachments | kCorpusFinalLogical | kCorpusCommandStateTrace,
		1, 60, 1, CorpusEventSchedule::EffectStress },
	{ CorpusCase::PostTaurusVariants, "post.taurus.variants.v1",
		CorpusAsset::Taurus, UINT64_C(0x00000000d344940e), CorpusInputSchedule::Motion,
		60, 18, 1, CorpusPreference::FullPost720p,
		kFullPostNormalGraph | CorpusGraphBit(GraphNodeId::MotionDebugNormal), 0,
		kCorpusSceneAttachments | kCorpusHistoryEvents | kCorpusFinalLogical,
		1, 60, 1, CorpusEventSchedule::PostVariants },
	{ CorpusCase::SamplingTaurusMsaa2, "sampling.taurus.msaa2.v1",
		CorpusAsset::Taurus, UINT64_C(0x00000000d344940f), CorpusInputSchedule::Hold,
		60, 19, 1, CorpusPreference::FullPostMsaa2, kFullPostNormalGraph,
		kAllResolveGraph, kCorpusRequestedAppliedSignature | kCorpusFinalLogical,
		1, 60, 1, CorpusEventSchedule::Steady },
	{ CorpusCase::SamplingTaurusMsaa2Fallback, "sampling.taurus.msaa2.forced_fallback.v1",
		CorpusAsset::Taurus, UINT64_C(0x00000000d3449410), CorpusInputSchedule::Hold,
		60, 20, 1, CorpusPreference::FullPostMsaa2, kFullPostNormalGraph,
		kAllResolveGraph, kCorpusRequestedAppliedSignature | kCorpusFinalLogical,
		1, 60, 1, CorpusEventSchedule::Msaa2Fallback },
	{ CorpusCase::SamplingTaurusGammaLow, "sampling.taurus.gamma0_5.v1",
		CorpusAsset::Taurus, UINT64_C(0x00000000d3449411), CorpusInputSchedule::Hold,
		60, 21, 1, CorpusPreference::FullPostGammaLow, kFullPostNormalGraph, 0,
		kCorpusFinalLogical | kCorpusCommandStateTrace,
		1, 60, 1, CorpusEventSchedule::GammaLow },
	{ CorpusCase::SamplingTaurusGammaHigh, "sampling.taurus.gamma2_0.v1",
		CorpusAsset::Taurus, UINT64_C(0x00000000d3449412), CorpusInputSchedule::Hold,
		60, 22, 1, CorpusPreference::FullPostGammaHigh, kFullPostNormalGraph, 0,
		kCorpusFinalLogical | kCorpusCommandStateTrace,
		1, 60, 1, CorpusEventSchedule::GammaHigh },
	{ CorpusCase::HistoryTheCoreTransitions, "history.thecore.transitions.v1",
		CorpusAsset::TheCore, UINT64_C(0x00000000d3449413), CorpusInputSchedule::Motion,
		60, 23, 1, CorpusPreference::FullPost720p, kFullPostNormalGraph, 0,
		kCorpusCapturedMatrices | kCorpusHistoryEvents | kCorpusSceneAttachments |
		kCorpusRequestedAppliedSignature, 1, 60, 1,
		CorpusEventSchedule::HistoryTransitions },
	{ CorpusCase::HistoryTheCoreSmallViews, "history.thecore.small_views.v1",
		CorpusAsset::TheCore, UINT64_C(0x00000000d3449414), CorpusInputSchedule::Hold,
		60, 24, 1, CorpusPreference::FullPost720p, kFullPostNormalGraph, 0,
		kCorpusCapturedMatrices | kCorpusHistoryEvents | kCorpusSceneAttachments,
		1, 60, 1, CorpusEventSchedule::SmallViews },
	{ CorpusCase::CockpitTheCoreModes, "cockpit.thecore.all_modes.v1",
		CorpusAsset::TheCore, UINT64_C(0x00000000d3449415), CorpusInputSchedule::Hold,
		60, 25, 1, CorpusPreference::FullPostCockpit,
		(kFullPostNormalGraph & ~(CorpusGraphBit(GraphNodeId::NormalComposite) |
		 CorpusGraphBit(GraphNodeId::MotionNormal) | CorpusGraphBit(GraphNodeId::NormalUi) |
		 CorpusGraphBit(GraphNodeId::BloomThreshold) | CorpusGraphBit(GraphNodeId::BloomDown) |
		 CorpusGraphBit(GraphNodeId::BloomUp) | CorpusGraphBit(GraphNodeId::Present))) |
		 kCockpitGraph, 0,
		kCorpusCockpitAlpha | kCorpusDeferredBloomInput | kCorpusFinalLogical |
		kCorpusCommandStateTrace, 1, 60, 1, CorpusEventSchedule::CockpitModes },
	{ CorpusCase::UiLevel1MenuFonts, "ui.level1.menu_fonts_telcom.v1",
		CorpusAsset::Level1, UINT64_C(0x00000000d3449416), CorpusInputSchedule::Hold,
		60, 26, 1, CorpusPreference::FullPost720p, kFullPostNormalGraph, 0,
		kCorpusFinalLogical | kCorpusCommandStateTrace,
		1, 60, 1, CorpusEventSchedule::UiMenuFonts },
	{ CorpusCase::UiLevel1NarrativeMedia, "ui.level1.narrative_media.v1",
		CorpusAsset::Level1, UINT64_C(0x00000000d3449417), CorpusInputSchedule::Hold,
		60, 27, 1, CorpusPreference::FullPost720p, kFullPostNormalGraph, 0,
		kCorpusFinalLogical | kCorpusCommandStateTrace,
		1, 60, 1, CorpusEventSchedule::NarrativeMedia },
	{ CorpusCase::UiLevel1MovieMutation, "ui.level1.movie_mutation.v1",
		CorpusAsset::Level1, UINT64_C(0x00000000d3449418), CorpusInputSchedule::Hold,
		60, 28, 1, CorpusPreference::FullPost720p, kFullPostNormalGraph, 0,
		kCorpusSceneAfterCapture | kCorpusFinalLogical | kCorpusCommandStateTrace,
		1, 60, 1, CorpusEventSchedule::MovieMutation },
	{ CorpusCase::UiLevel1Screenshots, "ui.level1.screenshots.v1",
		CorpusAsset::Level1, UINT64_C(0x00000000d3449419), CorpusInputSchedule::Hold,
		60, 29, 1, CorpusPreference::FullPost720p, kFullPostNormalGraph, 0,
		kCorpusSceneAfterCapture | kCorpusFinalLogical | kCorpusCommandStateTrace,
		1, 60, 1, CorpusEventSchedule::Screenshots },
	{ CorpusCase::EditorLevel1Draw, "editor.level1.draw.v1",
		CorpusAsset::Level1, UINT64_C(0x00000000d344941a), CorpusInputSchedule::Hold,
		60, 30, 1, CorpusPreference::Authored720p,
		CorpusGraphBit(GraphNodeId::CapWorld) | CorpusGraphBit(GraphNodeId::NormalBlit) |
		CorpusGraphBit(GraphNodeId::NormalUi) | CorpusGraphBit(GraphNodeId::Present), 0,
		kCorpusScenePreCapture | kCorpusCommandStateTrace | kCorpusFinalLogical,
		1, 60, 1, CorpusEventSchedule::EditorDraw },
	{ CorpusCase::EditorLevel1Readback, "editor.level1.readback.v1",
		CorpusAsset::Level1, UINT64_C(0x00000000d344941b), CorpusInputSchedule::Hold,
		60, 31, 1, CorpusPreference::Authored720p,
		CorpusGraphBit(GraphNodeId::CapWorld) | CorpusGraphBit(GraphNodeId::NormalBlit) |
		CorpusGraphBit(GraphNodeId::NormalUi) | CorpusGraphBit(GraphNodeId::Present), 0,
		kCorpusScenePreCapture | kCorpusCommandStateTrace | kCorpusFinalLogical,
		1, 60, 1, CorpusEventSchedule::EditorReadback },
	{ CorpusCase::PlatformLevel1Resize, "platform.level1.resize.v1",
		CorpusAsset::Level1, UINT64_C(0x00000000d344941c), CorpusInputSchedule::Hold,
		60, 32, 1, CorpusPreference::FullPost720p, kFullPostNormalGraph, 0,
		kCorpusRequestedAppliedSignature | kCorpusHistoryEvents | kCorpusFinalLogical,
		1, 60, 1, CorpusEventSchedule::PlatformResize },
	{ CorpusCase::PlatformLevel1SurfaceFaults, "platform.level1.surface_faults.v1",
		CorpusAsset::Level1, UINT64_C(0x00000000d344941d), CorpusInputSchedule::Hold,
		60, 33, 1, CorpusPreference::FullPost720p, kFullPostNormalGraph, 0,
		kCorpusRequestedAppliedSignature | kCorpusHistoryEvents | kCorpusFinalLogical,
		1, 60, 1, CorpusEventSchedule::SurfaceFaults },
	{ CorpusCase::PlatformStartupFallback, "platform.startup_fallback.v1",
		CorpusAsset::Level1, UINT64_C(0x00000000d344941e), CorpusInputSchedule::Hold,
		0, 34, 1, CorpusPreference::Authored720p, 0, 0,
		kCorpusCommandStateTrace, 1, 60, 1, CorpusEventSchedule::StartupFallback },
	{ CorpusCase::PreferencesLevel1Sweep, "preferences.level1.exhaustive.v1",
		CorpusAsset::Level1, UINT64_C(0x00000000d344941f), CorpusInputSchedule::Hold,
		60, 35, 1, CorpusPreference::FullPost720p, kFullPostNormalGraph, 0,
		kCorpusRequestedAppliedSignature | kCorpusCommandStateTrace |
		kCorpusFinalLogical, 1, 60, 1, CorpusEventSchedule::PreferenceSweep },
};

const size_t kCorpusCaseContractCount =
	sizeof(kCorpusCaseContract) / sizeof(kCorpusCaseContract[0]);

uint64_t ExpectedCorpusGraphNodeMask(const CorpusCaseContract &corpus_case,
	uint32_t applied_msaa_samples)
{
	return corpus_case.graph_node_mask |
		(applied_msaa_samples > 1 ? corpus_case.multisample_graph_node_mask : 0);
}

uint32_t DeriveCorpusRngSeed(uint64_t root_seed, CorpusRngStream stream)
{
	static const uint64_t salts[] = {
		UINT64_C(0x70735f72616e645f), UINT64_C(0x6372745f72616e64),
		UINT64_C(0x70686f6c6472616e), UINT64_C(0x505352616e645f31)
	};
	const uint32_t index = static_cast<uint32_t>(stream);
	if (index >= static_cast<uint32_t>(CorpusRngStream::Count))
		return 0;
	uint64_t value = root_seed ^ salts[index];
	value += UINT64_C(0x9e3779b97f4a7c15);
	value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
	value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
	value ^= value >> 31;
	return static_cast<uint32_t>(value ^ (value >> 32));
}

const ExternalCorpusAssetContract kOptionalExternalCorpusAssetContract = {
	"external.secret2.temporal.v1", "demo/Secret2.dem", UINT64_C(5087119),
	"FB319919B1E67D150F9549F6EBDC9E060BD91246320C4A64B2535FD4D410F3D7", 0
};

const CorpusCoverageContract kCorpusCoverageContract[] = {
	{ CorpusGroup::Indoor, "indoor.portals", "portal traversal and effective room order" },
	{ CorpusGroup::Indoor, "indoor.faces", "large, glancing, nonplanar, coplanar, and current zero-alpha behavior" },
	{ CorpusGroup::Indoor, "indoor.material_mutation", "UV slides, destroyable faces, procedural and lightmap updates" },
	{ CorpusGroup::Terrain, "terrain.external_rooms", "external rooms and mine-terrain portals in both directions" },
	{ CorpusGroup::Terrain, "terrain.surface", "full detail, water, holes, fog/no-fog, sky, stars, and satellites" },
	{ CorpusGroup::Terrain, "terrain.camera_anywhere", "above, intersecting, below, boundary, and outside-grid cameras" },
	{ CorpusGroup::Terrain, "terrain.fallbacks", "automap, editor, viewer deformation, and no-lightmap T0 paths" },
	{ CorpusGroup::MirrorFog, "mirror.current", "current reflected CPU geometry, custom plane, and repetition quirk" },
	{ CorpusGroup::MirrorFog, "fog.rooms", "room fog, portals, deferred AO suppression, and terrain fog" },
	{ CorpusGroup::MirrorFog, "room.effects", "scorches, coronas, glows, and custom clipping" },
	{ CorpusGroup::Polymodel, "model.submodels", "submodel transforms, clipping, morphing, cloak, and fades" },
	{ CorpusGroup::Polymodel, "model.lighting", "Gouraud/per-pixel, dynamic lights, and headlight shaping" },
	{ CorpusGroup::Polymodel, "model.specular", "authored, split, field, smooth, legacy, and per-pixel-off paths" },
	{ CorpusGroup::Polymodel, "model.motion_ghosts", "Old and Combo newest-to-oldest retained history geometry" },
	{ CorpusGroup::Effects, "effects.particles", "ordinary, instanced, and soft particles over changing depth" },
	{ CorpusGroup::Effects, "effects.weapons", "weapons, explosions, powerup halos, and lightning" },
	{ CorpusGroup::Effects, "effects.primitives", "lines, special-line variants, points, stars, and wireframes" },
	{ CorpusGroup::Effects, "effects.blends", "alpha, saturate, multiply, protection, and AO masks" },
	{ CorpusGroup::Post, "post.gtao", "GTAO alone, temporal sequence, debug modes, classes, and suppression" },
	{ CorpusGroup::Post, "post.bloom", "bloom alone, every pyramid level, threshold, spread, and protection" },
	{ CorpusGroup::Post, "post.motion", "pixel motion alone, object IDs, static reconstruction, and debug" },
	{ CorpusGroup::Post, "post.combined", "GTAO, bloom, motion, cockpit, SSAA, and MSAA together" },
	{ CorpusGroup::SamplingHistory, "sampling.ssaa", "SSAA 1, 2, and 4" },
	{ CorpusGroup::SamplingHistory, "sampling.msaa", "requested 0, 2, 4, 8 and downward fallback" },
	{ CorpusGroup::SamplingHistory, "sampling.gamma_overscan", "gamma extremes and GTAO overscan" },
	{ CorpusGroup::SamplingHistory, "history.transitions", "pause, unpause, camera cuts, resize, and resets" },
	{ CorpusGroup::SamplingHistory, "history.small_views", "rear/guided small views and protective velocity/ID" },
	{ CorpusGroup::CockpitHud, "cockpit.modes", "every cockpit mode, no-cockpit, backing, and scanlines" },
	{ CorpusGroup::CockpitHud, "cockpit.layers", "gauges, HUD, reticle, close-screen, deferred bloom, and post-post UI" },
	{ CorpusGroup::CockpitHud, "cockpit.messages", "message consoles after gamma" },
	{ CorpusGroup::UiMedia, "ui.menu_fonts", "menus, both font buckets, every flush cause, and TelCom" },
	{ CorpusGroup::UiMedia, "ui.narrative", "briefings, cinematics, and demo playback" },
	{ CorpusGroup::UiMedia, "ui.movies", "movies, dirty texture versions, and chunked bitmaps" },
	{ CorpusGroup::UiMedia, "ui.screenshots", "early/late screenshot, PNG dimensions, alpha, and row order" },
	{ CorpusGroup::Editor, "editor.draw", "NEWEDITOR primitives and compatibility scratch behavior" },
	{ CorpusGroup::Editor, "editor.readback", "TSearch repeated synchronous GetPixel segmentation" },
	{ CorpusGroup::PlatformRecovery, "platform.resize", "resize, fullscreen, minimize, restore, and zero extent" },
	{ CorpusGroup::PlatformRecovery, "platform.surface", "out-of-date, suboptimal, and surface-lost injection" },
	{ CorpusGroup::PlatformRecovery, "platform.fallback", "startup rejection, GL4 fallback, and clean shutdown" },
	{ CorpusGroup::PlatformRecovery, "preferences.exhaustive", "every preferred-state field and detail toggle" },
};

const size_t kCorpusCoverageContractCount =
	sizeof(kCorpusCoverageContract) / sizeof(kCorpusCoverageContract[0]);

#define COVERAGE(index, corpus, events) \
	{ index, CorpusCase::corpus, events }
#define EVENT_MASK(event) CorpusEventBit(CorpusEventKind::event)

// A coverage row may have several instantiations when its acceptance requires
// distinct immutable preferences (notably SSAA, requested MSAA, and gamma).
// The runner executes every row here; required_event_mask is evidence that the
// selected case's typed schedule actually drives the promised transition.
const CorpusCoverageInstantiationContract kCorpusCoverageInstantiationContract[] = {
	COVERAGE(0, IndoorLevel1HoldAuthored, EVENT_MASK(CaptureCheckpoint)),
	COVERAGE(1, IndoorLevel1HoldAuthored, EVENT_MASK(CaptureCheckpoint)),
	COVERAGE(2, IndoorLevel1Mutation, EVENT_MASK(MutateTexture) |
		EVENT_MASK(MutateLightmap) | EVENT_MASK(DestroyFace)),
	COVERAGE(3, TerrainPolarisMotionFullPost, EVENT_MASK(CaptureCheckpoint)),
	COVERAGE(4, TerrainPolarisMotionFullPost, EVENT_MASK(CaptureCheckpoint)),
	COVERAGE(5, TerrainPolarisFallbacks, EVENT_MASK(SetTerrainCamera)),
	COVERAGE(6, TerrainPolarisFallbacks, EVENT_MASK(SetTerrainFallback)),
	COVERAGE(7, MirrorFogLevel1Effects, EVENT_MASK(SetMirrorVariant)),
	COVERAGE(8, MirrorFogLevel1Effects, EVENT_MASK(SetFogVariant)),
	COVERAGE(9, MirrorFogLevel1Effects, EVENT_MASK(SpawnRoomEffect)),
	COVERAGE(10, ModelTaurusModes, EVENT_MASK(SetModelVariant)),
	COVERAGE(11, ModelTaurusModes, EVENT_MASK(SetLightingVariant)),
	COVERAGE(12, ModelTaurusModes, EVENT_MASK(SetSpecularVariant)),
	COVERAGE(13, ModelTaurusModes, EVENT_MASK(SetMotionGhostVariant)),
	COVERAGE(14, EffectsLevel1Stress, EVENT_MASK(SpawnEffectVariant)),
	COVERAGE(15, EffectsLevel1Stress, EVENT_MASK(SpawnEffectVariant)),
	COVERAGE(16, EffectsLevel1Stress, EVENT_MASK(DrawPrimitiveVariant)),
	COVERAGE(17, EffectsLevel1Stress, EVENT_MASK(SetBlendVariant)),
	COVERAGE(18, PostTaurusVariants, EVENT_MASK(SetPostVariant)),
	COVERAGE(18, IndoorLevel1FireFullPost, EVENT_MASK(CaptureCheckpoint)),
	COVERAGE(19, PostTaurusVariants, EVENT_MASK(SetPostVariant)),
	COVERAGE(20, PostTaurusVariants, EVENT_MASK(SetPostVariant)),
	COVERAGE(21, PostTaurusVariants, EVENT_MASK(SetPostVariant)),
	COVERAGE(21, CockpitTheCoreFullPost, EVENT_MASK(CaptureCheckpoint)),
	COVERAGE(22, IndoorLevel1HoldAuthored, EVENT_MASK(CaptureCheckpoint)),
	COVERAGE(22, TerrainPolarisCombinedSampling, EVENT_MASK(CaptureCheckpoint)),
	COVERAGE(22, SamplingTaurusSsaa4, EVENT_MASK(CaptureCheckpoint)),
	COVERAGE(23, IndoorLevel1HoldAuthored, EVENT_MASK(CaptureCheckpoint)),
	COVERAGE(23, SamplingTaurusMsaa2, EVENT_MASK(CaptureCheckpoint)),
	COVERAGE(23, TerrainPolarisCombinedSampling, EVENT_MASK(CaptureCheckpoint)),
	COVERAGE(23, SamplingTaurusMsaa8, EVENT_MASK(CaptureCheckpoint)),
	COVERAGE(23, SamplingTaurusMsaa2Fallback, EVENT_MASK(InjectFault)),
	COVERAGE(24, SamplingTaurusGammaLow, EVENT_MASK(SetGammaPreset)),
	COVERAGE(24, SamplingTaurusGammaHigh, EVENT_MASK(SetGammaPreset)),
	COVERAGE(25, HistoryTheCoreTransitions, EVENT_MASK(SetPauseState) |
		EVENT_MASK(CameraCut) | EVENT_MASK(ResizePreset)),
	COVERAGE(26, HistoryTheCoreSmallViews, EVENT_MASK(SetSmallView)),
	COVERAGE(26, HistoryTheCoreRearview, EVENT_MASK(CaptureCheckpoint)),
	COVERAGE(27, CockpitTheCoreModes, EVENT_MASK(SetCockpitMode)),
	COVERAGE(27, CockpitTheCoreFullPost, EVENT_MASK(CaptureCheckpoint)),
	COVERAGE(28, CockpitTheCoreModes, EVENT_MASK(SetCockpitLayer)),
	COVERAGE(29, CockpitTheCoreModes, EVENT_MASK(ShowMessageConsole)),
	COVERAGE(30, UiLevel1MenuFonts, EVENT_MASK(SetUiFlow)),
	COVERAGE(31, UiLevel1NarrativeMedia, EVENT_MASK(SetMediaFlow)),
	COVERAGE(32, UiLevel1MovieMutation, EVENT_MASK(UpdateMovieBitmap) |
		EVENT_MASK(DrawChunkedBitmap)),
	COVERAGE(33, UiLevel1Screenshots, EVENT_MASK(RequestScreenshot)),
	COVERAGE(34, EditorLevel1Draw, EVENT_MASK(EditorDrawPrimitive)),
	COVERAGE(35, EditorLevel1Readback, EVENT_MASK(ReadPixelLoop)),
	COVERAGE(36, PlatformLevel1Resize, EVENT_MASK(ResizePreset) |
		EVENT_MASK(FullscreenTransition) | EVENT_MASK(SetMinimized)),
	COVERAGE(37, PlatformLevel1SurfaceFaults, EVENT_MASK(InjectFault)),
	COVERAGE(38, PlatformStartupFallback, EVENT_MASK(InjectFault) |
		EVENT_MASK(ShutdownRenderer)),
	COVERAGE(39, PreferencesLevel1Sweep, EVENT_MASK(SweepPreferredField) |
		EVENT_MASK(SweepEngineField)),
};

#undef EVENT_MASK
#undef COVERAGE

const size_t kCorpusCoverageInstantiationContractCount =
	sizeof(kCorpusCoverageInstantiationContract) /
	sizeof(kCorpusCoverageInstantiationContract[0]);

const ValidationContract kValidationContract[] = {
	{ ValidationRequirement::VulkanValidationClean, "Vulkan validation clean" },
	{ ValidationRequirement::SynchronizationValidationClean, "synchronization validation clean" },
	{ ValidationRequirement::GpuAssistedReducedCorpusClean, "GPU-assisted reduced corpus clean" },
	{ ValidationRequirement::NoVulkanOrVmaLeaks, "no Vulkan/VMA object leaks" },
	{ ValidationRequirement::NoRawGlReachableUnderVulkan, "no reachable raw GL under Vulkan" },
	{ ValidationRequirement::ShaderReflectionAbiClean, "shader reflection and ABI clean" },
	{ ValidationRequirement::PipelineMatrixComplete, "pipeline matrix complete" },
	{ ValidationRequirement::TextureConversionBytesMatch, "texture conversion bytes match GL4" },
	{ ValidationRequirement::RetainedGenerationInvalidation, "retained generation invalidation" },
	{ ValidationRequirement::DescriptorPageCapacityStress, "descriptor page capacity stress" },
	{ ValidationRequirement::TwoFrameResourceVersionStress, "two-frame resource version stress" },
	{ ValidationRequirement::SwapchainSemaphoreReuse, "swapchain semaphore reuse" },
	{ ValidationRequirement::NormalEditorDedicatedBuilds, "normal/editor/dedicated builds" },
};

const size_t kValidationContractCount =
	sizeof(kValidationContract) / sizeof(kValidationContract[0]);

static_assert(sizeof(kValidationContract) / sizeof(kValidationContract[0]) ==
	static_cast<size_t>(ValidationRequirement::Count), "validation contract mismatch");
static_assert(sizeof(kCorpusAssetContract) / sizeof(kCorpusAssetContract[0]) ==
	static_cast<size_t>(CorpusAsset::Count), "corpus asset contract mismatch");
static_assert(sizeof(kCorpusInputScheduleContract) /
	sizeof(kCorpusInputScheduleContract[0]) ==
	static_cast<size_t>(CorpusInputSchedule::Count),
	"corpus input schedule contract mismatch");
static_assert(sizeof(kCorpusEventKindContract) /
	sizeof(kCorpusEventKindContract[0]) ==
	static_cast<size_t>(CorpusEventKind::Count),
	"corpus event kind contract mismatch");
static_assert(sizeof(kCorpusEventScheduleContract) /
	sizeof(kCorpusEventScheduleContract[0]) ==
	static_cast<size_t>(CorpusEventSchedule::Count),
	"corpus event schedule contract mismatch");
static_assert(static_cast<uint32_t>(CorpusEventKind::Count) <= 64,
	"corpus event masks must fit uint64_t");
static_assert(static_cast<size_t>(CorpusPreferredField::Count) * sizeof(uint32_t) ==
	sizeof(CapturedPreferredState),
	"preferred-field sweep must enumerate every four-byte captured field");
static_assert(static_cast<size_t>(CorpusEngineField::Count) * sizeof(uint32_t) ==
	sizeof(CorpusEngineSettings),
	"engine-field sweep must enumerate every four-byte corpus setting");
static_assert(sizeof(kCorpusPreferenceContract) /
	sizeof(kCorpusPreferenceContract[0]) ==
	static_cast<size_t>(CorpusPreference::Count),
	"corpus preference contract mismatch");
static_assert(sizeof(kCorpusCaseContract) / sizeof(kCorpusCaseContract[0]) ==
	static_cast<size_t>(CorpusCase::Count), "corpus case contract mismatch");

} // namespace render
} // namespace piccu
