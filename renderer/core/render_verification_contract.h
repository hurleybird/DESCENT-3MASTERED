/* Deterministic parity corpus, capture names, and comparison policy. */
#pragma once

#include "render_contract.h"

namespace piccu
{
namespace render
{

enum class ComparisonClass : uint32_t
{
	Exact = 0,
	ExactDefinedUnorm,
	DepthFloat,
	VelocityFloat,
	GtaoFloat,
	FinalRgba,
	TemporalSequence,
	RasterEdgeQualified,
	DiagnosticOnly,
};

struct CaptureArtifactContract
{
	const char *name_pattern;
	ComparisonClass comparison;
	float absolute_tolerance;
	float relative_tolerance;
	float required_pixel_fraction;
	float per_channel_tolerance;
	float unexplained_max_tolerance;
};

extern const CaptureArtifactContract kCaptureArtifactContract[];
extern const size_t kCaptureArtifactContractCount;

struct GraphArtifactContract
{
	GraphNodeId node;
	CaptureArtifactContract artifact;
	uint32_t capture_every_invocation;
};

extern const GraphArtifactContract kGraphArtifactContract[];
extern const size_t kGraphArtifactContractCount;

struct ArtifactNameGrammarContract
{
	const char *grammar;
	uint32_t includes_case_id;
	uint32_t includes_case_frame;
	uint32_t includes_segment;
	uint32_t includes_graph_node_and_invocation;
	uint32_t includes_semantic_format_extent_samples;
	uint32_t forbids_overwrite;
};

extern const ArtifactNameGrammarContract kArtifactNameGrammarContract;

enum class CorpusGroup : uint32_t
{
	Indoor = 0,
	Terrain,
	MirrorFog,
	Polymodel,
	Effects,
	Post,
	SamplingHistory,
	CockpitHud,
	UiMedia,
	Editor,
	PlatformRecovery,
	Count,
};

struct CorpusCoverageContract
{
	CorpusGroup group;
	const char *id;
	const char *required_coverage;
};

extern const CorpusCoverageContract kCorpusCoverageContract[];
extern const size_t kCorpusCoverageContractCount;

enum class ValidationRequirement : uint32_t
{
	VulkanValidationClean = 0,
	SynchronizationValidationClean,
	GpuAssistedReducedCorpusClean,
	NoVulkanOrVmaLeaks,
	NoRawGlReachableUnderVulkan,
	ShaderReflectionAbiClean,
	PipelineMatrixComplete,
	TextureConversionBytesMatch,
	RetainedGenerationInvalidation,
	DescriptorPageCapacityStress,
	TwoFrameResourceVersionStress,
	SwapchainSemaphoreReuse,
	NormalEditorDedicatedBuilds,
	Count,
};

struct ValidationContract
{
	ValidationRequirement requirement;
	const char *diagnostic_name;
};

extern const ValidationContract kValidationContract[];
extern const size_t kValidationContractCount;

enum class RequiredHardwareClass : uint32_t
{
	NvidiaDiscrete = 0,
	AmdDiscrete,
	IntelIntegrated,
	NativeWin32Wsi,
	EveryAdvertisedAdditionalWsi,
	Count,
};

struct DeterministicRunnerContract
{
	uint32_t fixed_simulation_timestep;
	uint32_t fixed_random_seeds;
	uint32_t fixed_input_stream;
	uint32_t identical_preferences_and_detail;
	uint32_t named_frame_and_graph_stage;
	uint32_t normalize_rows_without_value_change;
};

constexpr DeterministicRunnerContract kDeterministicRunnerContract =
	{ 1, 1, 1, 1, 1, 1 };

enum class CorpusAsset : uint32_t
{
	Level1 = 0,
	Polaris,
	Taurus,
	TheCore,
	Count,
};

struct CorpusAssetContract
{
	CorpusAsset asset;
	const char *id;
	const char *repository_path;
	const char *sha256;
	uint32_t d3l_version;
	const char *level_name;
	uint32_t room_count;
	uint32_t vertex_count;
	uint32_t face_count;
	uint32_t face_vertex_count;
	uint32_t portal_count;
	uint32_t terrain_chunk_bytes;
};

extern const CorpusAssetContract kCorpusAssetContract[];
extern const size_t kCorpusAssetContractCount;

enum class CorpusInputSchedule : uint32_t
{
	Hold = 0,
	Fire4,
	Motion,
	Yaw,
	Rearview,
	Count,
};

enum class CorpusInputAction : uint32_t
{
	FirePrimaryPulse = 0,
	HeadingThrust,
	ForwardThrust,
	VerticalThrust,
	RearviewTogglePulse,
};

struct CorpusInputActionContract
{
	CorpusInputSchedule schedule;
	uint32_t first_tick;
	uint32_t last_tick_inclusive;
	CorpusInputAction action;
	float value;
	uint32_t down_count;
};

struct CorpusInputScheduleContract
{
	CorpusInputSchedule schedule;
	const char *id;
	uint32_t first_action;
	uint32_t action_count;
	uint32_t complete_zeroed_controls_when_unlisted;
	uint32_t physical_input_polled;
};

extern const CorpusInputActionContract kCorpusInputActionContract[];
extern const size_t kCorpusInputActionContractCount;
extern const CorpusInputScheduleContract kCorpusInputScheduleContract[];
extern const size_t kCorpusInputScheduleContractCount;

enum class CorpusPreference : uint32_t
{
	Authored720p = 0,
	FullPost720p,
	FullPostSsaa2Msaa4,
	FullPostMsaa8,
	FullPostSsaa4,
	FullPostCockpit,
	FullPostRearview,
	FullPostMsaa2,
	FullPostGammaLow,
	FullPostGammaHigh,
	Count,
};

enum class CorpusHudMode : uint32_t
{
	GameDefault = 0,
	Cockpit,
	Fullscreen,
};

struct CorpusEngineSettings
{
	float aspect_width;
	float aspect_height;
	float fov_degrees;
	float hud_scale;
	uint32_t terrain_render_distance;
	float pixel_error;
	uint32_t specular_lighting;
	uint32_t dynamic_lighting;
	uint32_t fast_headlight;
	uint32_t mirrored_surfaces;
	uint32_t fog;
	uint32_t coronas;
	uint32_t procedurals;
	uint32_t powerup_halos;
	uint32_t scorches;
	uint32_t weapon_coronas;
	uint32_t bumpmapping;
	uint32_t specular_mapping_type;
	uint32_t object_complexity;
	uint32_t soft_visual_effects;
	uint32_t cpu_batch_cache;
	uint32_t disable_powerup_sparkles;
	uint32_t simd_particle_builder;
	uint32_t gl4_particle_instancing;
	uint32_t face_probe;
	uint32_t use_newrender;
	uint32_t debug_overlays;
	CorpusHudMode hud_mode;
};

struct CorpusPreferenceContract
{
	CorpusPreference preference;
	const char *id;
	CapturedPreferredState renderer;
	CorpusEngineSettings engine;
	uint32_t record_requested_and_applied_msaa;
};

extern const CorpusPreferenceContract kCorpusPreferenceContract[];
extern const size_t kCorpusPreferenceContractCount;

// Fixed-tick events complement the zeroed input stream.  They are commands for
// the deterministic runner, not prose tags: every argument has a closed domain
// and fault injection is an explicit, separately validated operation.
enum class CorpusEventArgumentDomain : uint32_t
{
	None = 0,
	TextureMutation,
	LightmapMutation,
	TerrainCamera,
	TerrainFallback,
	MirrorVariant,
	FogVariant,
	RoomEffect,
	ModelVariant,
	LightingVariant,
	SpecularVariant,
	MotionGhostVariant,
	EffectVariant,
	PrimitiveVariant,
	BlendVariant,
	PostVariant,
	GammaPreset,
	Boolean,
	ResizePreset,
	SmallView,
	CockpitMode,
	CockpitLayer,
	UiFlow,
	MediaFlow,
	ScreenshotKind,
	EditorPrimitive,
	Fault,
	PreferredField,
	EngineField,
	Count,
};

enum class CorpusPreferredField : uint32_t
{
	Mipping = 0,
	Filtering,
	Antialised,
	BitDepth,
	Gamma,
	Width,
	Height,
	WindowWidth,
	WindowHeight,
	Fullscreen,
	SupersamplingFactor,
	MsaaSamples,
	PerPixelLighting,
	BloomEnabled,
	BloomThreshold,
	BloomIntensity,
	BloomSpread,
	GtaoEnabled,
	GtaoResolution,
	GtaoSampleCount,
	GtaoBlurRadius,
	GtaoRadius,
	GtaoIntensity,
	GtaoBias,
	GtaoOverscanPercent,
	GtaoDebugPreview,
	GtaoTemporalBlend,
	GtaoTemporalDepthReject,
	GtaoTemporalVelocityReject,
	GtaoTemporalDebugPreview,
	GtaoTerrainOcclusion,
	GtaoPolyobjectOcclusion,
	GtaoMineRockOcclusion,
	GtaoMineOcclusion,
	MotionVectorMode,
	MotionVectorDebugPreview,
	PixelMotionBlurStrength,
	CombinedMotionBlur,
	CombinedMotionBlurLegacyStrength,
	CombinedMotionBlurLegacyFrameTime,
	CombinedMotionBlurLegacySphereSize,
	CombinedMotionBlurLegacyCopyDensity,
	CombinedMotionBlurLegacyMaxIterations,
	CombinedMotionBlurLegacyAlphaExponent,
	PixelMotionBlurPeripheryStrength,
	PixelMotionBlurLegacyObjectStrength,
	PixelMotionBlurCenterSuppression,
	PixelMotionBlurLegacyObjectCenterSuppression,
	PixelMotionBlurSamples,
	AfterburnerFovMultiplier,
	AfterburnerPixelBlurMultiplier,
	Count,
};

enum class CorpusEngineField : uint32_t
{
	AspectWidth = 0,
	AspectHeight,
	FovDegrees,
	HudScale,
	TerrainRenderDistance,
	PixelError,
	SpecularLighting,
	DynamicLighting,
	FastHeadlight,
	MirroredSurfaces,
	Fog,
	Coronas,
	Procedurals,
	PowerupHalos,
	Scorches,
	WeaponCoronas,
	Bumpmapping,
	SpecularMappingType,
	ObjectComplexity,
	SoftVisualEffects,
	CpuBatchCache,
	DisablePowerupSparkles,
	SimdParticleBuilder,
	Gl4ParticleInstancing,
	FaceProbe,
	UseNewrender,
	DebugOverlays,
	HudMode,
	Count,
};

enum class CorpusFault : uint32_t
{
	None = 0,
	ForceMsaa2To1Fallback,
	AcquireOutOfDate,
	PresentSuboptimal,
	SurfaceLost,
	RejectRequiredDeviceFeature,
	Count,
};

enum class CorpusEventKind : uint32_t
{
	CaptureCheckpoint = 0,
	MutateTexture,
	MutateLightmap,
	DestroyFace,
	SetTerrainCamera,
	SetTerrainFallback,
	SetMirrorVariant,
	SetFogVariant,
	SpawnRoomEffect,
	SetModelVariant,
	SetLightingVariant,
	SetSpecularVariant,
	SetMotionGhostVariant,
	SpawnEffectVariant,
	DrawPrimitiveVariant,
	SetBlendVariant,
	SetPostVariant,
	SetGammaPreset,
	SetPauseState,
	CameraCut,
	ResizePreset,
	SetSmallView,
	SetCockpitMode,
	SetCockpitLayer,
	ShowMessageConsole,
	SetUiFlow,
	SetMediaFlow,
	UpdateMovieBitmap,
	DrawChunkedBitmap,
	RequestScreenshot,
	EditorDrawPrimitive,
	ReadPixelLoop,
	FullscreenTransition,
	SetMinimized,
	InjectFault,
	ShutdownRenderer,
	SweepPreferredField,
	SweepEngineField,
	Count,
};

constexpr uint64_t CorpusEventBit(CorpusEventKind event)
{
	return uint64_t(1) << static_cast<uint32_t>(event);
}

struct CorpusEventKindContract
{
	CorpusEventKind event;
	const char *id;
	CorpusEventArgumentDomain argument_domain;
	uint32_t argument_count;
	uint32_t fault_injection;
};

enum class CorpusEventSchedule : uint32_t
{
	Steady = 0,
	IndoorMutation,
	TerrainSweep,
	MirrorFogEffects,
	ModelModes,
	EffectStress,
	PostVariants,
	Msaa2Fallback,
	GammaLow,
	GammaHigh,
	HistoryTransitions,
	SmallViews,
	CockpitModes,
	UiMenuFonts,
	NarrativeMedia,
	MovieMutation,
	Screenshots,
	EditorDraw,
	EditorReadback,
	PlatformResize,
	SurfaceFaults,
	StartupFallback,
	PreferenceSweep,
	Count,
};

struct CorpusEventActionContract
{
	CorpusEventSchedule schedule;
	uint32_t tick;
	CorpusEventKind event;
	uint32_t argument;
	uint32_t repeat_count;
	CorpusFault fault;
};

struct CorpusEventScheduleContract
{
	CorpusEventSchedule schedule;
	const char *id;
	uint32_t first_event;
	uint32_t event_count;
};

extern const CorpusEventKindContract kCorpusEventKindContract[];
extern const size_t kCorpusEventKindContractCount;
extern const CorpusEventActionContract kCorpusEventActionContract[];
extern const size_t kCorpusEventActionContractCount;
extern const CorpusEventScheduleContract kCorpusEventScheduleContract[];
extern const size_t kCorpusEventScheduleContractCount;

struct CorpusFrameSpan
{
	uint32_t first_frame;
	uint32_t last_frame_inclusive;
};

enum CorpusExplicitStageBits : uint32_t
{
	kCorpusScenePreCapture = 1u << 0,
	kCorpusSceneAfterCapture = 1u << 1,
	kCorpusSceneAttachments = 1u << 2,
	kCorpusCapturedMatrices = 1u << 3,
	kCorpusFinalLogical = 1u << 4,
	kCorpusCockpitAlpha = 1u << 5,
	kCorpusDeferredBloomInput = 1u << 6,
	kCorpusHistoryEvents = 1u << 7,
	kCorpusRequestedAppliedSignature = 1u << 8,
	kCorpusCommandStateTrace = 1u << 9,
};

constexpr uint64_t CorpusGraphBit(GraphNodeId node)
{
	return uint64_t(1) << static_cast<uint32_t>(node);
}

enum class CorpusCase : uint32_t
{
	IndoorLevel1HoldAuthored = 0,
	IndoorLevel1FireFullPost,
	TerrainPolarisMotionFullPost,
	TerrainPolarisCombinedSampling,
	SamplingTaurusMsaa8,
	SamplingTaurusSsaa4,
	CockpitTheCoreFullPost,
	HistoryTheCoreRearview,
	IndoorLevel1Mutation,
	TerrainPolarisFallbacks,
	MirrorFogLevel1Effects,
	ModelTaurusModes,
	EffectsLevel1Stress,
	PostTaurusVariants,
	SamplingTaurusMsaa2,
	SamplingTaurusMsaa2Fallback,
	SamplingTaurusGammaLow,
	SamplingTaurusGammaHigh,
	HistoryTheCoreTransitions,
	HistoryTheCoreSmallViews,
	CockpitTheCoreModes,
	UiLevel1MenuFonts,
	UiLevel1NarrativeMedia,
	UiLevel1MovieMutation,
	UiLevel1Screenshots,
	EditorLevel1Draw,
	EditorLevel1Readback,
	PlatformLevel1Resize,
	PlatformLevel1SurfaceFaults,
	PlatformStartupFallback,
	PreferencesLevel1Sweep,
	Count,
};

struct CorpusCaseContract
{
	CorpusCase corpus_case;
	const char *id;
	CorpusAsset asset;
	uint64_t root_seed;
	CorpusInputSchedule input;
	uint32_t warmup_frames;
	uint32_t first_frame_span;
	uint32_t frame_span_count;
	CorpusPreference preference;
	uint64_t graph_node_mask;
	// Nodes whose predicates additionally require an applied MSAA count > 1.
	// Requested MSAA alone is never sufficient because support may fall to 1.
	uint64_t multisample_graph_node_mask;
	uint32_t explicit_stage_mask;
	uint32_t fixed_timestep_numerator;
	uint32_t fixed_timestep_denominator;
	uint32_t fresh_process;
	CorpusEventSchedule event_schedule;
};

extern const CorpusFrameSpan kCorpusFrameSpan[];
extern const size_t kCorpusFrameSpanCount;
extern const CorpusCaseContract kCorpusCaseContract[];
extern const size_t kCorpusCaseContractCount;
uint64_t ExpectedCorpusGraphNodeMask(const CorpusCaseContract &corpus_case,
	uint32_t applied_msaa_samples);

struct CorpusCoverageInstantiationContract
{
	uint32_t coverage_index;
	CorpusCase corpus_case;
	uint64_t required_event_mask;
};

extern const CorpusCoverageInstantiationContract
	kCorpusCoverageInstantiationContract[];
extern const size_t kCorpusCoverageInstantiationContractCount;

enum class CorpusRngStream : uint32_t
{
	PsRand = 0,
	CrtRand,
	ProceduralPholdrand,
	IsolatedPsRand,
	Count,
};

uint32_t DeriveCorpusRngSeed(uint64_t root_seed, CorpusRngStream stream);

struct ExternalCorpusAssetContract
{
	const char *id;
	const char *content_root_relative_path;
	uint64_t byte_size;
	const char *sha256;
	uint32_t required_for_repository_ci;
};

extern const ExternalCorpusAssetContract kOptionalExternalCorpusAssetContract;

} // namespace render
} // namespace piccu
