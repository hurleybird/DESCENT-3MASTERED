/* Backend-neutral Vulkan device, format, descriptor, and texture contract. */
#pragma once

#include "render_graph_contract.h"

namespace piccu
{
namespace render
{

constexpr uint32_t kVulkanRequiredApiMajor = 1;
constexpr uint32_t kVulkanRequiredApiMinor = 3;
constexpr uint32_t kVulkanRequiredApiPatch = 0;

enum class RequiredDeviceFeature : uint32_t
{
	DynamicRendering = 0,
	Synchronization2,
	TimelineSemaphore,
	IndependentBlend,
	MultiDrawIndirect,
	ShaderDrawParameters,
	NonUniformSampledImageIndexing,
	ExtendedDynamicRasterDepthState,
	Swapchain,
	PlatformSurface,
	GraphicsQueue,
	ComputeOnGraphicsQueue,
	Presentation,
	Count,
};

struct DeviceFeatureRequirement
{
	RequiredDeviceFeature feature;
	const char *diagnostic_name;
	uint32_t required;
};

extern const DeviceFeatureRequirement kRequiredDeviceFeatures[];
extern const size_t kRequiredDeviceFeatureCount;

enum class DeviceLimit : uint32_t
{
	MaxColorAttachments = 0,
	MaxFragmentOutputAttachments,
	MaxFragmentCombinedOutputResources,
	MaxBoundDescriptorSets,
	MaxPerStageResources,
	MaxPerStageDescriptorUniformBuffers,
	MaxDescriptorSetUniformBuffers,
	MaxDescriptorSetUniformBuffersDynamic,
	MaxPerStageDescriptorStorageBuffers,
	MaxDescriptorSetStorageBuffers,
	MaxPerStageDescriptorSamplers,
	MaxDescriptorSetSamplers,
	MaxPerStageDescriptorSampledImages,
	MaxDescriptorSetSampledImages,
	MaxStorageBufferRange,
	MinStorageBufferOffsetAlignment,
	MinUniformBufferOffsetAlignment,
	MaxBufferSize,
	MaxTexelBufferElements,
	MaxDrawIndirectCount,
	MaxPushConstantsSize,
	MaxComputeWorkGroupInvocations,
	MaxComputeWorkGroupSizeX,
	MaxComputeSharedMemorySize,
	MaxComputeWorkGroupCount,
	MaxImageDimension2D,
	MaxImageArrayLayers,
	MaxFramebufferWidth,
	MaxFramebufferHeight,
	MaxFramebufferLayers,
	MaxViewportDimensions,
	ViewportBoundsRange,
	MaxVertexInputBindings,
	MaxVertexInputAttributes,
	MaxVertexInputBindingStride,
	IndexAndDrawOffsetRanges,
	NonCoherentAtomSize,
	CopyOffsetAlignment,
	CopyRowPitchAlignment,
	TimestampPeriod,
	TimestampValidBits,
	FramebufferSampleCounts,
	MemoryHeapBudgets,
	Count,
};

enum class LimitValidation : uint32_t
{
	Minimum = 0,
	DerivedFromReflectedAbi,
	DerivedFromRequestedSignature,
	QueryForDiagnostics,
};

struct DeviceLimitRequirement
{
	DeviceLimit limit;
	const char *diagnostic_name;
	LimitValidation validation;
	uint64_t minimum;
};

extern const DeviceLimitRequirement kRequiredDeviceLimits[];
extern const size_t kRequiredDeviceLimitCount;

enum FormatUsageBits : uint32_t
{
	kFormatSampled = 1u << 0,
	kFormatColorAttachment = 1u << 1,
	kFormatDepthAttachment = 1u << 2,
	kFormatBlend = 1u << 3,
	kFormatTransferSource = 1u << 4,
	kFormatTransferDestination = 1u << 5,
	kFormatStorage = 1u << 6,
	kFormatLinearFilter = 1u << 7,
	kFormatNearestFilter = 1u << 8,
	kFormatMultisample = 1u << 9,
};

enum class FormatSemantic : uint32_t
{
	SceneAndPostColor = 0,
	Velocity,
	ProtectionMask,
	AoClass,
	MotionObjectId,
	Depth,
	GtaoDepthWeight,
	GtaoRawBlur,
	GtaoHistory,
	GtaoSuppression,
	Count,
};

struct FormatRequirement
{
	FormatSemantic semantic;
	RenderFormat format;
	uint32_t required_usage;
};

extern const FormatRequirement kRequiredFormats[];
extern const size_t kRequiredFormatCount;

constexpr uint32_t kPhysicalDeviceUuidSize = 16;
constexpr uint64_t kAllRequiredDeviceFeatureBits =
	(uint64_t(1) << static_cast<uint32_t>(RequiredDeviceFeature::Count)) - 1;
constexpr uint32_t kAllRequiredFormatBits =
	(uint32_t(1) << static_cast<uint32_t>(FormatSemantic::Count)) - 1;

struct PhysicalDeviceUuid
{
	uint8_t bytes[kPhysicalDeviceUuidSize];
};

enum class PhysicalDeviceType : uint32_t
{
	Other = 0,
	Cpu,
	Virtual,
	Integrated,
	Discrete,
	Count,
};

// API-neutral results of the Vulkan profile, surface, and requested-signature
// probes. Unsupported candidates remain in the inventory for exact override
// diagnostics, but are filtered before default ranking.
struct DeviceProfileSupport
{
	uint32_t api_major;
	uint32_t api_minor;
	uint32_t api_patch;
	uint64_t required_feature_bits;
	uint32_t required_format_bits;
	uint32_t all_required_limits_satisfied;
	uint32_t surface_configuration_satisfied;
	uint32_t requested_signature_supported;
	uint32_t descriptor_page_tier;
	uint32_t common_sample_count_mask;
	uint32_t graphics_compute_queue_family;
	uint32_t present_queue_family;
};

struct PhysicalDeviceCandidate
{
	PhysicalDeviceUuid uuid;
	uint32_t enumeration_index;
	PhysicalDeviceType type;
	DeviceProfileSupport profile;
	uint64_t device_local_budget_bytes;
};

enum class DeviceSelectionOverrideKind : uint32_t
{
	None = 0,
	Uuid,
	EnumerationIndex,
	Count,
};

struct DeviceSelectionOverride
{
	DeviceSelectionOverrideKind kind;
	PhysicalDeviceUuid uuid;
	uint32_t enumeration_index;
};

enum class DeviceSelectionStatus : uint32_t
{
	Success = 0,
	InvalidInput,
	InvalidOverride,
	DuplicateUuid,
	DuplicateEnumerationIndex,
	OverrideNotFound,
	OverrideRejectedByRequiredProfile,
	NoRequiredProfileDevice,
	Count,
};

struct PhysicalDeviceSelection
{
	DeviceSelectionStatus status;
	// Position in the caller's candidate array. For a rejected override this
	// identifies the exact candidate that failed the required profile.
	uint32_t candidate_position;
	uint32_t enumeration_index;
	PhysicalDeviceUuid uuid;
};

bool PhysicalDeviceUuidIsZero(const PhysicalDeviceUuid &uuid);
bool PhysicalDeviceUuidEqual(const PhysicalDeviceUuid &left,
	const PhysicalDeviceUuid &right);
bool SupportsRequiredDeviceProfile(const DeviceProfileSupport &profile);
// Positive means left ranks ahead of right; zero requires equal UUIDs.
int32_t ComparePhysicalDevicePreference(const PhysicalDeviceCandidate &left,
	const PhysicalDeviceCandidate &right);
PhysicalDeviceSelection SelectPhysicalDevice(
	const PhysicalDeviceCandidate *candidates, size_t candidate_count,
	const DeviceSelectionOverride &selection_override);

enum class DescriptorKind : uint32_t
{
	DynamicUniformBuffer = 0,
	Sampler,
	SampledFloat2D,
	SampledFloat2DArray,
	CombinedFloat2D,
	CombinedFloat2DArray,
	StorageBuffer,
};

enum ShaderStageBits : uint32_t
{
	kStageVertex = 1u << 0,
	kStageFragment = 1u << 1,
	kStageCompute = 1u << 2,
};

struct WorldDescriptorBindingContract
{
	uint32_t set;
	uint32_t binding;
	DescriptorKind kind;
	uint32_t count;
	uint32_t page_tier_count;
	uint32_t stage_mask;
};

constexpr uint32_t kDescriptorPageTiers[] = { 32, 64, 128, 256, 512, 1024 };
extern const WorldDescriptorBindingContract kWorldDescriptorBindings[];
extern const size_t kWorldDescriptorBindingCount;

// Post pipelines use a separate set-0 ABI.  The descriptor kind is deliberately
// more specific than VkDescriptorType: reflection must also preserve the GLSL
// numeric class and multisample dimensionality.
enum class PostDescriptorKind : uint32_t
{
	UniformBuffer = 0,
	Sampler,
	SampledFloat2D,
	SampledFloat2DMultisample,
	SampledDepth2D,
	SampledDepth2DMultisample,
	SampledUint2D,
	SampledUint2DMultisample,
	SampledFloat2DArray,
	Count,
};

enum class PostDescriptorRequirement : uint32_t
{
	Required = 0,
	// The graph resource may be absent, but the descriptor is still populated
	// with the typed diagnostic fallback (partially-bound is not in the profile).
	Optional,
	Count,
};

enum class PostDescriptorResourceSemantic : uint32_t
{
	PassUniforms = 0,
	FixedSamplerTable,
	GraphResource,
	SelectedGraphInput,
	Count,
};

enum class PostPassVariant : uint32_t
{
	Only = 0,
	SingleSample,
	Multisample,
	SsaaFourToTwo,
	SsaaTwoToOne,
	BloomThresholdPhase,
	BloomDownsamplePhase,
	BloomMergePhase,
	Count,
};

enum class PostPassDescriptorUse : uint32_t
{
	ImmutablePostSet = 0,
	WorldDescriptorAbi,
	AttachmentOperation,
	Count,
};

constexpr uint32_t kPostDescriptorSet = 0;
constexpr uint32_t kPostUniformBinding = 0;
constexpr uint32_t kPostSamplerTableBinding = 1;
constexpr uint32_t kPostFirstImageBinding = 2;

// Universal std140 post-pass record. Every post invocation owns one immutable
// record; unused fields are zero. Vector slots avoid implementation-defined
// scalar packing and are reflected at the exact offsets asserted below.
struct alignas(16) PostPassUniforms
{
	float current_projection[16];                 // 0
	float current_inverse_modelview[16];          // 64
	float previous_view_projection[16];           // 128
	float source_extent_inv_extent[4];            // w,h,1/w,1/h
	float destination_extent_inv_extent[4];       // w,h,1/w,1/h
	float visible_origin_size[4];                 // canonical top-left x,y,w,h
	float source_visible_origin_size[4];          // canonical top-left x,y,w,h
	float uv_origin_scale[4];                     // primary origin.xy, scale.xy
	float secondary_uv_origin_scale[4];           // secondary origin.xy, scale.xy
	float velocity_uv_origin_scale[4];            // velocity origin.xy, scale.xy
	float scene_uv_origin_scale[4];               // scene origin.xy, scale.xy
	float ao_uv_origin_scale[4];                  // AO origin.xy, scale.xy
	float alpha_mask_uv_origin_scale[4];          // alpha origin.xy, scale.xy
	float screen_size_inv_size[4];                // logical w,h,1/w,1/h
	float ao_screen_size_inv_size[4];             // AO w,h,1/w,1/h
	float projection_info[4];                     // exact GTAO proj_info
	float near_far_radius_radius_pixels[4];        // near,far,radius,radius_pixels
	float noise_origin_jitter[4];                 // origin.xy,jitter.xy
	float blur_delta_sharpness_reserved[4];       // delta.xy,sharpness,zero
	float bloom_gamma_threshold_intensity_spread[4]; // gamma,threshold,intensity,spread
	float ao_max_radius_neg_inv_radius2_bias_intensity[4]; // max px,-1/r2,bias,intensity
	float ao_class_weights[4];                    // terrain,poly,mine-rock,mine
	float temporal_blend_depth_velocity_frame_time[4]; // blend,depth reject,vel reject,dt
	float motion_strength_legacy_object_centers[4]; // pixel,legacy-object,center,legacy-center
	float motion_legacy_frame_sphere_density_exponent[4]; // dt,sphere,density,alpha exp
	float motion_periphery_combined_strength_sphere_density[4]; // periphery,legacy,sphere,density
	float motion_afterburner_exponent_fov_pixel_scalar[4]; // exponent,FOV,pixel,active scalar
	uint32_t sample_counts[4];                    // source,motion,directions,steps
	uint32_t feature_flags[4];                    // [0]=PostUniformFeatureBits
	int32_t integer_params[4];                    // kernel,debug,dest x,dest y
	uint32_t frame_branch[4];                     // frame,branch,history,invocation
};

enum PostUniformFeatureBits : uint32_t
{
	kPostUniformHistoryValid = 1u << 0,
	kPostUniformHasDynamicVelocity = 1u << 1,
	kPostUniformHasStaticReconstruction = 1u << 2,
	kPostUniformHasAoClass = 1u << 3,
	kPostUniformAoWeightIsDirect = 1u << 4,
	kPostUniformHasMask = 1u << 5,
	kPostUniformUseBloomMask = 1u << 6,
	kPostUniformUseDepthMask = 1u << 7,
	kPostUniformUseProtectionMask = 1u << 8,
	kPostUniformUseAlphaOcclusionMask = 1u << 9,
	kPostUniformUseVisibleRect = 1u << 10,
	kPostUniformHasSuppressionMask = 1u << 11,
	kPostUniformUseAlphaMask = 1u << 12,
	kPostUniformSourceVisibleRect = 1u << 13,
	kPostUniformDebugTemporal = 1u << 14,
	kPostUniformPausedOrFrozen = 1u << 15,
	kPostUniformAllFeatures = (1u << 16) - 1,
};

enum class PostUniformField : uint32_t
{
	CurrentProjection = 0,
	CurrentInverseModelview,
	PreviousViewProjection,
	SourceExtent,
	DestinationExtent,
	VisibleOriginSize,
	SourceVisibleOriginSize,
	PrimaryUv,
	SecondaryUv,
	VelocityUv,
	SceneUv,
	AoUv,
	AlphaMaskUv,
	ScreenSize,
	AoScreenSize,
	ProjectionInfo,
	NearFarRadius,
	NoiseOriginJitter,
	BlurParameters,
	BloomParameters,
	AoParameters,
	AoClassWeights,
	TemporalParameters,
	MotionParameters0,
	MotionParameters1,
	MotionParameters2,
	MotionParameters3,
	SampleCounts,
	FeatureFlags,
	IntegerParameters,
	FrameBranch,
	Count,
};

using PostUniformFieldMask = uint64_t;
constexpr PostUniformFieldMask PostUniformBit(PostUniformField field)
{
	return PostUniformFieldMask(1) << static_cast<uint32_t>(field);
}

struct PostPassUniformUsageContract
{
	GraphNodeId node;
	PostUniformFieldMask required_fields;
	PostUniformFieldMask conditional_fields;
};

extern const PostPassUniformUsageContract kPostPassUniformUsageContract[];
extern const size_t kPostPassUniformUsageContractCount;

enum class PostUniformScalarKind : uint32_t
{
	Float32 = 0,
	Uint32,
	Int32,
	Count,
};

// Reflection must reproduce this complete, ordered std140 layout. byte_size is
// the occupied ABI range (mat4 = 64 bytes, every other field = one vec4).
struct PostUniformFieldLayoutContract
{
	PostUniformField field;
	uint32_t byte_offset;
	uint32_t byte_size;
	PostUniformScalarKind scalar_kind;
};

extern const PostUniformFieldLayoutContract kPostUniformFieldLayoutContract[];
extern const size_t kPostUniformFieldLayoutContractCount;

enum class CompilerGraphPhaseKind : uint32_t
{
	SampledPostPass = 0,
	AttachmentAlphaOnlyClear,
	MultisampleResolve,
	SsaaDownsample,
	BloomThreshold,
	BloomDownsample,
	BloomMerge,
	ResourceChannelAlias,
	Count,
};

enum class CompilerGraphPhaseInvocationRule : uint32_t
{
	OncePerLogicalInvocation = 0,
	CaptureColorSingleSsaaOne,
	CaptureColorMsaaSsaaOne,
	CaptureColorMsaaBeforeSsaa,
	CaptureColorSsaaFourToTwo,
	CaptureColorSsaaTwoToOne,
	CockpitSingleSsaaOne,
	CockpitMsaaSsaaOne,
	CockpitMsaaBeforeSsaa,
	CockpitSsaaFourToTwo,
	CockpitSsaaTwoToOne,
	CockpitAlphaAliasWhenBloom,
	DeferredBloomThreshold,
	DeferredBloomDownLevels,
	DeferredBloomMergeLevels,
	Count,
};

// frame_branch.y selects the sole live source among the same-typed optional
// bindings exposed by CAP_WORLD and COCKPIT_RESOLVE SSAA descriptor variants.
// This is capture ABI, not a texture-size or fallback-image heuristic.
enum class PostUniformSourceSelector : uint32_t
{
	Primary2D = 2,              // binding 2: original single-sample source
	MsaaResolved2D = 3,        // binding 3: explicit MSAA-resolved source
	SsaaIntermediate2x2D = 4,  // binding 4: prior 4x -> 2x phase output
	Count,
};

struct CompilerGraphPhaseContract
{
	GraphNodeId node;
	uint32_t phase_index;
	uint32_t phase_count;
	CompilerGraphPhaseKind kind;
	GraphResourceMask inputs;
	GraphResourceMask outputs;
	GraphResourceMask attachment_load_inputs;
	uint32_t selected_attachments;
	uint32_t color_channel_mask;
	uint32_t ordered_before_next_phase;
	PostPassVariant descriptor_variant;
	CompilerGraphPhaseInvocationRule invocation_rule;
	RenderFormat output_formats[2];
	uint32_t output_location_count;
	uint32_t output_alpha_channel_alias;
};

extern const CompilerGraphPhaseContract kCompilerGraphPhaseContract[];
extern const size_t kCompilerGraphPhaseContractCount;
uint32_t EvaluateCompilerGraphPhaseInvocationCount(
	const CompilerGraphPhaseContract &phase,
	const GraphEvaluationContext &context);
PostUniformSourceSelector SelectCompilerGraphPhaseSource(
	const CompilerGraphPhaseContract &phase,
	const GraphEvaluationContext &context);

struct PostPassDescriptorBindingContract
{
	GraphNodeId node;
	PostPassVariant variant;
	uint32_t set;
	uint32_t binding;
	PostDescriptorKind kind;
	uint32_t count;
	PostDescriptorRequirement requirement;
	PostDescriptorResourceSemantic semantic;
	// Exactly one of resource/selected_input is meaningful for the two graph
	// semantic classes.  The unused value is the corresponding Count/None.
	GraphResource resource;
	GraphInputSemantic selected_input;
};

struct PostPassDescriptorSetContract
{
	GraphNodeId node;
	PostPassVariant variant;
	PostPassDescriptorUse use;
	uint32_t binding_count;
	uint32_t uniform_record_size;
	uint32_t immutable_per_invocation;
	uint32_t ordinary_sampled_images_only;
	uint32_t optional_slots_always_valid;
	// Inputs consumed by dynamic-rendering LOAD/blend rather than descriptors.
	GraphResourceMask attachment_load_inputs;
};

extern const PostPassDescriptorBindingContract kPostPassDescriptorBindings[];
extern const size_t kPostPassDescriptorBindingCount;
extern const PostPassDescriptorSetContract kPostPassDescriptorSets[];
extern const size_t kPostPassDescriptorSetCount;

const PostPassDescriptorSetContract *FindPostPassDescriptorSet(
	GraphNodeId node, PostPassVariant variant);
bool ValidatePostPassDescriptorContract();

static_assert(sizeof(PostPassUniforms) == 640,
	"post-pass std140 uniform ABI changed");
static_assert(alignof(PostPassUniforms) == 16,
	"post-pass uniform alignment changed");
static_assert(offsetof(PostPassUniforms, source_extent_inv_extent) == 192,
	"post-pass source extent offset");
static_assert(offsetof(PostPassUniforms, uv_origin_scale) == 256,
	"post-pass primary UV offset");
static_assert(offsetof(PostPassUniforms, projection_info) == 384,
	"post-pass GTAO projection offset");
static_assert(offsetof(PostPassUniforms, bloom_gamma_threshold_intensity_spread) == 448,
	"post-pass bloom parameter offset");
static_assert(offsetof(PostPassUniforms, temporal_blend_depth_velocity_frame_time) == 496,
	"post-pass temporal parameter offset");
static_assert(offsetof(PostPassUniforms, sample_counts) == 576,
	"post-pass sample-count offset");
static_assert(offsetof(PostPassUniforms, frame_branch) == 624,
	"post-pass frame/branch offset");

enum class SamplerAddressMode : uint32_t
{
	Repeat = 0,
	ClampToEdge,
};

enum class SamplerFilterMode : uint32_t
{
	Nearest = 0,
	Linear,
};

enum class SamplerMipMode : uint32_t
{
	Disabled = 0,
	Nearest,
	Linear,
};

enum class SamplerSemantic : uint32_t
{
	GenericRepeatPointNoMip = 0,
	GenericRepeatLinearNoMip,
	GenericRepeatPointMip,
	GenericRepeatLinearMip,
	GenericClampPointNoMip,
	GenericClampLinearNoMip,
	GenericClampPointMip,
	GenericClampLinearMip,
	GenericClampURepeatVPointNoMip,
	GenericClampURepeatVLinearNoMip,
	GenericClampURepeatVPointMip,
	GenericClampURepeatVLinearMip,
	LightmapClamp,
	LightmapRepeat,
	Font,
	PostNearest,
	PostLinear,
	GtaoNoise,
	HistoryLinear,
	BloomLinear,
	MaskNearest,
	DepthNearest,
	Count,
};

struct SamplerContract
{
	SamplerSemantic semantic;
	SamplerAddressMode address_u;
	SamplerAddressMode address_v;
	SamplerFilterMode magnification;
	SamplerFilterMode minification;
	SamplerMipMode mip;
	uint32_t anisotropy_enabled;
};

extern const SamplerContract kSamplerContract[];
extern const size_t kSamplerContractCount;

struct PostSamplerSlotContract
{
	uint32_t slot;
	SamplerSemantic semantic;
};

constexpr uint32_t kPostSamplerTableSize = 7;
extern const PostSamplerSlotContract kPostSamplerSlots[];
extern const size_t kPostSamplerSlotCount;

// Exact GL4@d34494a 4x4 two-channel noise upload.  Each byte is the C++
// uint8_t truncation of kMtNoise[n] * 255.0f from renderer/gl_gtao.cpp.  The
// sampler contract also preserves the clamp state imposed by GL4's binding
// helper at draw time (which overrides the texture's creation-time repeat).
struct GtaoNoiseTextureContract
{
	uint32_t width;
	uint32_t height;
	uint32_t channels;
	RenderFormat format;
	SamplerSemantic sampler;
	uint32_t row_major_interleaved_rg;
};

extern const GtaoNoiseTextureContract kGtaoNoiseTextureContract;
extern const uint8_t kGtaoNoiseRg8[32];
extern const size_t kGtaoNoiseRg8Size;

struct AlphaTypeBlendContract
{
	uint32_t alpha_type;
	BlendClass blend_class;
	uint32_t luminance_post_mask;
};

extern const AlphaTypeBlendContract kAlphaTypeBlendContract[];
extern const size_t kAlphaTypeBlendContractCount;
const AlphaTypeBlendContract *FindAlphaTypeBlendContract(uint32_t alpha_type);

enum class TextureResidencyState : uint32_t
{
	CpuSnapshot = 0,
	PendingUpload,
	Resident,
	Evictable,
	Retired,
};

enum TextureDirtyBits : uint32_t
{
	kTextureBitmapChanged = 1u << 0,
	kTextureBitmapBrandNew = 1u << 1,
	kTextureLightmapChanged = 1u << 2,
	kTextureLightmapBrandNew = 1u << 3,
};

constexpr uint32_t kTextureDirtyMask = kTextureBitmapChanged |
	kTextureBitmapBrandNew | kTextureLightmapChanged | kTextureLightmapBrandNew;

struct TextureVersionBindInput
{
	uint32_t dirty_flags;
	uint32_t has_logical_mapping;
	uint32_t mapped_identity_matches;
	uint32_t mapped_version_referenced_earlier_in_segment;
	uint64_t mapped_version_last_use_timeline;
	uint64_t completed_timeline;
};

struct TextureVersionBindDecision
{
	uint32_t snapshot_cpu_pixels_now;
	uint32_t create_new_logical_version;
	uint32_t attach_new_version_to_subsequent_draws;
	uint32_t clear_dirty_flags_now;
	uint32_t copy_on_write_image_required;
	uint32_t may_recycle_completed_image;
	uint32_t reuse_mapped_logical_version;
	uint32_t invalidate_stale_mapping;
};

TextureVersionBindDecision EvaluateTextureVersionBind(
	const TextureVersionBindInput &input);

enum class TextureMappingInvalidationReason : uint32_t
{
	ResetCache = 0,
	LevelUnload,
	DestroyedTexture,
	RendererRecreation,
	Count,
};

struct TextureMappingInvalidationContract
{
	TextureMappingInvalidationReason reason;
	uint32_t invalidate_logical_mapping_immediately;
	uint32_t destroy_in_flight_versions_immediately;
	uint32_t completed_versions_may_recycle;
};

extern const TextureMappingInvalidationContract kTextureMappingInvalidationContract[];
extern const size_t kTextureMappingInvalidationContractCount;

using LogicalTextureVersion = CapturedTextureVersion;

constexpr uint32_t kCapturedTextureUploadPayloadAlignment = 4;

enum class TextureUploadSubresourceOrder : uint32_t
{
	MipMajorLayerMinor = 0,
	Count,
};

enum class TextureUploadRowOrder : uint32_t
{
	TopDown = 0,
	Count,
};

constexpr TextureUploadSubresourceOrder kCapturedTextureUploadSubresourceOrder =
	TextureUploadSubresourceOrder::MipMajorLayerMinor;
constexpr TextureUploadRowOrder kCapturedTextureUploadRowOrder =
	TextureUploadRowOrder::TopDown;

struct TextureUploadLayoutInput
{
	RenderFormat format;
	uint32_t width;
	uint32_t height;
	uint32_t layer_count;
	uint32_t mip_count;
	uint32_t payload_alignment;
};

struct TextureUploadSubresourceLayout
{
	uint32_t mip_level;
	uint32_t array_layer;
	uint32_t width;
	uint32_t height;
	uint64_t byte_offset;
	uint64_t row_pitch_bytes;
	uint64_t byte_size;
};

enum class TextureUploadLayoutError : uint32_t
{
	None = 0,
	InvalidFormat,
	InvalidExtentOrLayerCount,
	InvalidMipCount,
	InvalidPayloadAlignment,
	PayloadSizeAlignmentMismatch,
	SubresourceCountOverflow,
	ByteSizeOverflow,
	InsufficientOutputCapacity,
	NullLayout,
	NonCanonicalSubresource,
	PayloadSizeMismatch,
	Count,
};

struct TextureUploadLayoutResult
{
	TextureUploadLayoutError error;
	uint32_t subresource_count;
	uint64_t total_byte_size;
};

TextureUploadLayoutInput MakeCapturedTextureUploadLayoutInput(
	const CapturedTextureVersion &version);
// Builds the exact capture byte manifest. Rows are top-down and tight;
// subresources are contiguous with no inter-row, inter-layer, or inter-mip
// padding, ordered by mip first and array layer second.
TextureUploadLayoutResult BuildCapturedTextureUploadLayout(
	const TextureUploadLayoutInput &input,
	TextureUploadSubresourceLayout *subresources,
	uint32_t subresource_capacity);
TextureUploadLayoutError ValidateCapturedTextureUploadLayout(
	const TextureUploadLayoutInput &input,
	const TextureUploadSubresourceLayout *subresources,
	uint32_t subresource_count, uint64_t payload_byte_size);

struct DescriptorPageContract
{
	uint32_t float_image_2d_capacity_selected_from_tier;
	uint32_t float_image_array_capacity;
	uint32_t every_draw_texture_tuple_must_fit;
	uint32_t diagnostic_fill_required;
	uint32_t patch_virtual_ids_to_page_local_indices;
	uint32_t immutable_after_recording;
	uint32_t timeline_retired_pool;
	uint32_t page_change_breaks_indirect_run;
	uint32_t page_change_preserves_draw_order;
	uint32_t initialize_every_new_slot_diagnostic;
	uint32_t safe_reuse_rewrites_only_previously_used_slots;
	uint32_t post_set_is_distinct_and_immutable_per_invocation;
	uint32_t update_after_bind_allowed;
	uint32_t variable_descriptor_count_allowed;
};

extern const DescriptorPageContract kDescriptorPageContract;

uint32_t SelectDescriptorPageTier(uint32_t supported_float_2d_images);
uint32_t SelectSupportedMsaa(uint32_t normalized_request,
	uint32_t supported_sample_count_mask);

} // namespace render
} // namespace piccu
