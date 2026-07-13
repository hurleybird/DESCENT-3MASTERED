/* Coordinate, projection, clipping, fog, and T1 eligibility contract. */
#pragma once

#include "render_contract.h"

namespace piccu
{
namespace render
{

enum class CoordinateSource : uint32_t
{
	ViewLocalT0 = 0,
	TerrainT2ViewLocal,
	FontTargetAbsolute,
	RectClearTargetAbsolute,
	PostTargetAbsolute,
	ReadbackTargetAbsolute,
	Count,
};

struct CoordinateSourceContract
{
	CoordinateSource source;
	uint32_t target_absolute;
	uint32_t add_clip_origin;
	uint32_t positive_height_viewport;
	uint32_t upper_left_origin;
};

extern const CoordinateSourceContract kCoordinateSourceContract[];
extern const size_t kCoordinateSourceContractCount;

enum class InterpolationMode : uint32_t
{
	Unused = 0,
	NoperspectiveRaw,
	NoperspectiveQPackDivide,
	SmoothRaw,
	Flat,
};

enum class InterpolationSemantic : uint32_t
{
	QuantizedVertexRgba = 0,
	BaseUv,
	LightmapUv,
	LegacyMappedDepthOrFog,
	RawPerPixelSpecularNormal,
	PhongNormal,
	DynamicWorldOrViewPosition,
	PerPixelSpecularViewPosition,
	FieldSpecularCenter,
	FieldSpecularColor,
	MotionPositions,
	SoftParticleMappedDepth,
	StateMaterialMotionId,
	TerrainPages,
	FontArrayLayer,
	FontUvColorOrAnalyticScreen,
	AnalyticPrimitiveParameters,
	Count,
};

struct InterpolationContract
{
	InterpolationSemantic semantic;
	InterpolationMode t0;
	InterpolationMode t1;
	InterpolationMode t2;
};

extern const InterpolationContract kInterpolationContract[];
extern const size_t kInterpolationContractCount;

enum class LegacyEntryPoint : uint32_t
{
	DrawPolygon2D = 0,
	DrawScaledBitmap,
	DrawScaledBitmapWithZ,
	DrawSimpleBitmap,
	DrawChunkedBitmap,
	DrawScaledChunkedBitmap,
	DrawFontCharacter,
	SetPixelOrPoint,
	DrawLine,
	DrawSpecialLine,
	DrawSpecialLineBatch,
	FillRect,
	FillCircle,
	DrawCircle,
	GetPixel,
	Count,
};

enum class DepthValueSource : uint32_t
{
	InputEyeZ = 0,
	SyntheticEyeZOne,
	AlreadyMappedZero,
	SpecialLineLiteralAlreadyMapped,
	Unused,
};

struct EntryPointCoordinateContract
{
	LegacyEntryPoint entry_point;
	DepthInterpretation depth_interpretation;
	DepthValueSource depth_source;
	int32_t logical_offset_x;
	int32_t logical_offset_y;
	uint32_t target_absolute;
	uint32_t add_clip_origin;
	uint32_t emits_command;
};

extern const EntryPointCoordinateContract kEntryPointCoordinateContract[];
extern const size_t kEntryPointCoordinateContractCount;

struct CoordinateColorDepthContract
{
	uint32_t upper_left_origin;
	uint32_t positive_height_viewports;
	uint32_t gl_ccw_maps_to_vulkan_clockwise;
	uint32_t automatic_srgb_decode_encode;
	uint32_t alpha_test_or_final_discard;
	uint32_t depth_compare_less_or_equal;
	float depth_clear;
};

constexpr CoordinateColorDepthContract kCoordinateColorDepthContract =
	{ 1, 1, 1, 0, 0, 1, 1.0f };

enum class T1SourceKind : uint32_t
{
	OrdinaryRoomBase = 0,
	OrdinaryRoomPostrender,
	OrdinaryRoomSpecular,
	OrdinaryStaticPolymodel,
	Other,
};

enum T1ExclusionBits : uint32_t
{
	kT1Mirror = 1u << 0,
	kT1RoomFogOverlay = 1u << 1,
	kT1ScorchGlowEffectEditor = 1u << 2,
	kT1SofCustom = 1u << 3,
	kT1GeneratedOrMorphed = 1u << 4,
	kT1TerrainCell = 1u << 5,
};

enum T1FailureBits : uint32_t
{
	kT1FailureSource = 1u << 0,
	kT1FailureZBias = 1u << 1,
	kT1FailureTriviallyRejected = 1u << 2,
	kT1FailureNeedsLegacyClip = 1u << 3,
	kT1FailureNonFiniteOrBehind = 1u << 4,
	kT1FailureExcludedClass = 1u << 5,
	kT1FailureUnrepresentablePayload = 1u << 6,
	kT1FailureMissingRetainedRange = 1u << 7,
};

struct T1EligibilityInput
{
	T1SourceKind source;
	float z_bias;
	uint32_t cc_and;
	uint32_t cc_or;
	uint32_t all_z_finite_and_positive;
	uint32_t exclusion_bits;
	uint32_t payload_representable;
	uint32_t retained_range_available;
};

struct T1EligibilityResult
{
	uint32_t eligible;
	uint32_t use_legacy_t0;
	uint32_t whole_primitive_rejected;
	uint32_t renderer_invariant_failure;
	uint32_t failure_bits;
};

struct T0ProjectionResult
{
	float x_ndc;
	float y_ndc;
	float z_ndc;
	float clip_w;
	float reciprocal_q;
};

struct T1ProjectionConstants
{
	float ndc_center_x;
	float ndc_center_y;
	float ndc_scale_x;
	float ndc_scale_y;
};

struct T1ClipPosition
{
	float x;
	float y;
	float z;
	float w;
};

struct TerrainVertexMappingInput
{
	float screen_x;
	float screen_y;
	float viewport_width;
	float viewport_height;
	float rotated_eye_z;
	float world[3];
	float base_uv[2];
	float lightmap_uv[2];
	uint32_t texture_page;
	uint32_t lightmap_page;
};

struct TerrainVertexMappingOutput
{
	BaseVertex base;
	TerrainVertexPayload payload;
	DepthInterpretation depth_interpretation;
};

struct PixelCoordinate
{
	int32_t x;
	int32_t y;
};

struct UvCoordinate
{
	float u;
	float v;
};

struct FloatPixelCoordinate
{
	float x;
	float y;
};

struct UvTransform
{
	UvCoordinate origin;
	UvCoordinate scale;
};

// One logical visible rectangle expressed in every coordinate space used by
// the pinned GL4 post path.  The canonical fields are upper-left; only the
// explicitly named legacy field is lower-left.
struct VisibleRectCoordinateSet
{
	LogicalRect logical_top_left;
	LogicalRect ssaa_top_left;
	LogicalRect legacy_gl_ssaa_bottom_left;
	LogicalRect post_top_left;
};

struct HistoryReprojectionCoordinate
{
	UvCoordinate canonical_velocity;
	UvCoordinate previous_uv;
	// The texel is clamped for texel-fetch users; in_bounds remains the gate
	// used by the temporal-history path before any sample is consumed.
	PixelCoordinate nearest_texel;
	uint32_t in_bounds;
};

struct WronskiTwoToOneMapping
{
	UvCoordinate base_uv;
	UvCoordinate visible_uv_min;
	UvCoordinate visible_uv_max;
};

UvCoordinate TopLeftPixelCenterToUv(PixelCoordinate pixel,
	PixelCoordinate extent);
UvCoordinate TopLeftPixelCenterToLegacyGlUv(PixelCoordinate pixel,
	PixelCoordinate extent);
FloatPixelCoordinate TopLeftUvToPixelCenter(UvCoordinate uv,
	PixelCoordinate extent);
PixelCoordinate TopLeftUvToClampedTexel(UvCoordinate uv,
	PixelCoordinate extent);
UvCoordinate TopLeftUvToLegacyGlUv(UvCoordinate canonical_uv);
UvCoordinate LegacyGlUvToTopLeftUv(UvCoordinate legacy_uv);
float LegacyEquivalentFragmentY(float canonical_fragment_y,
	int32_t target_height);
LogicalRect TopLeftRectToLegacyGlBottomLeft(LogicalRect canonical_rect,
	PixelCoordinate target_extent);
VisibleRectCoordinateSet BuildVisibleRectCoordinateSet(
	LogicalRect logical_visible_rect, PixelCoordinate logical_target_extent,
	uint32_t ssaa_factor);
UvTransform VisibleRectUvTransform(LogicalRect canonical_visible_rect,
	PixelCoordinate source_extent);
UvCoordinate ApplyUvTransform(UvCoordinate destination_uv,
	const UvTransform &transform);

PixelCoordinate MapNearestPixelCenter(PixelCoordinate destination,
	PixelCoordinate destination_extent, PixelCoordinate source_origin,
	PixelCoordinate source_visible_extent, PixelCoordinate source_extent);
UvCoordinate MapFullscreenVisibleUv(PixelCoordinate destination,
	PixelCoordinate destination_extent, PixelCoordinate source_origin,
	PixelCoordinate source_visible_extent, PixelCoordinate source_extent);
WronskiTwoToOneMapping MapWronskiTwoToOne(PixelCoordinate destination,
	PixelCoordinate source_extent, PixelCoordinate source_visible_origin,
	PixelCoordinate source_visible_size);
PixelCoordinate MapGtaoReductionOrigin(PixelCoordinate ao_pixel,
	PixelCoordinate input_extent, PixelCoordinate ao_extent);
UvCoordinate MapVisibleSuppressionUv(PixelCoordinate source_pixel,
	PixelCoordinate source_visible_origin, PixelCoordinate source_visible_size);
UvCoordinate MapLegacyGtaoNoisePosition(PixelCoordinate ao_pixel,
	PixelCoordinate ao_extent, LogicalRect canonical_source_visible_rect,
	PixelCoordinate source_extent);
UvCoordinate MapLegacyGtaoNoiseUv(PixelCoordinate ao_pixel,
	PixelCoordinate ao_extent, LogicalRect canonical_source_visible_rect,
	PixelCoordinate source_extent);

UvCoordinate LegacyStoredVelocityToCanonical(UvCoordinate stored_velocity);
UvCoordinate CanonicalVelocityToLegacyStored(UvCoordinate canonical_velocity);
HistoryReprojectionCoordinate ReprojectHistoryCanonical(
	UvCoordinate current_uv, UvCoordinate legacy_stored_velocity,
	PixelCoordinate history_extent);

LogicalRect ComputeCanonicalPresentRect(PixelCoordinate logical_extent,
	PixelCoordinate drawable_extent);
UvCoordinate MapPresentPixelToSourceUv(PixelCoordinate drawable_pixel,
	LogicalRect canonical_present_rect);

struct FogRoomContract
{
	uint32_t overlay_none;
	uint32_t texture_flat;
	uint32_t lighting_none;
	uint32_t color_model_mono;
	uint32_t alpha_type_vertex;
	uint32_t alpha_value;
	uint32_t depth_write_enabled_during_draw;
	uint32_t depth_test_inherited;
	float coplanar_factor;
	float coplanar_units;
	uint32_t cpu_generated_t0;
	uint32_t flat_rgb_c_cast_truncation;
	uint32_t restore_only_ao_bias_and_depth_write;
};

constexpr FogRoomContract kFogRoomContract =
	{ 0, 0, 0, 0, 4, 255, 0, 1, -1.0f, -1.0f, 1, 1, 1 };

struct FogRoomAlphaInput
{
	float vertex[3];
	float viewer_eye[3];
	float viewer_forward[3];
	float room_plane[3];
	float room_plane_distance;
	float room_eye_distance;
	float fog_depth;
	float room_light_value;
	uint32_t viewer_outside;
	uint32_t viewer_inside;
};

T1EligibilityResult EvaluateT1Eligibility(const T1EligibilityInput &input);
T0ProjectionResult ProjectT0(float view_local_x, float view_local_y,
	float eye_z, float z_bias, float viewport_width, float viewport_height);
T1ClipPosition ProjectT1(const float view_position[3],
	const T1ProjectionConstants &constants);
TerrainVertexMappingOutput MapTerrainT2Vertex(
	const TerrainVertexMappingInput &input);
float SpecialLineMappedDepth(float eye_z, float z_bias);
float GenericFogAmount(float interpolated_biased_mapped_depth,
	float fog_near_eye_z, float fog_far_eye_z);
float FogRoomVertexAlpha(const FogRoomAlphaInput &input);

} // namespace render
} // namespace piccu
