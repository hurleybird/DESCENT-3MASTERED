#include "render_coordinate_contract.h"

#include <algorithm>
#include <cmath>

namespace piccu
{
namespace render
{

const CoordinateSourceContract kCoordinateSourceContract[] = {
	{ CoordinateSource::ViewLocalT0, 0, 0, 1, 1 },
	{ CoordinateSource::TerrainT2ViewLocal, 0, 0, 1, 1 },
	{ CoordinateSource::FontTargetAbsolute, 1, 1, 1, 1 },
	{ CoordinateSource::RectClearTargetAbsolute, 1, 0, 1, 1 },
	{ CoordinateSource::PostTargetAbsolute, 1, 0, 1, 1 },
	{ CoordinateSource::ReadbackTargetAbsolute, 1, 0, 1, 1 },
};

const size_t kCoordinateSourceContractCount =
	sizeof(kCoordinateSourceContract) / sizeof(kCoordinateSourceContract[0]);

const InterpolationContract kInterpolationContract[] = {
	{ InterpolationSemantic::QuantizedVertexRgba, InterpolationMode::NoperspectiveRaw, InterpolationMode::NoperspectiveRaw, InterpolationMode::NoperspectiveRaw },
	{ InterpolationSemantic::BaseUv, InterpolationMode::NoperspectiveQPackDivide, InterpolationMode::SmoothRaw, InterpolationMode::NoperspectiveQPackDivide },
	{ InterpolationSemantic::LightmapUv, InterpolationMode::NoperspectiveQPackDivide, InterpolationMode::SmoothRaw, InterpolationMode::NoperspectiveQPackDivide },
	{ InterpolationSemantic::LegacyMappedDepthOrFog, InterpolationMode::NoperspectiveRaw, InterpolationMode::NoperspectiveRaw, InterpolationMode::NoperspectiveRaw },
	{ InterpolationSemantic::RawPerPixelSpecularNormal, InterpolationMode::NoperspectiveRaw, InterpolationMode::NoperspectiveRaw, InterpolationMode::Unused },
	{ InterpolationSemantic::PhongNormal, InterpolationMode::NoperspectiveQPackDivide, InterpolationMode::SmoothRaw, InterpolationMode::Unused },
	{ InterpolationSemantic::DynamicWorldOrViewPosition, InterpolationMode::NoperspectiveQPackDivide, InterpolationMode::SmoothRaw, InterpolationMode::NoperspectiveQPackDivide },
	{ InterpolationSemantic::PerPixelSpecularViewPosition, InterpolationMode::NoperspectiveQPackDivide, InterpolationMode::SmoothRaw, InterpolationMode::Unused },
	{ InterpolationSemantic::FieldSpecularCenter, InterpolationMode::NoperspectiveQPackDivide, InterpolationMode::SmoothRaw, InterpolationMode::Unused },
	{ InterpolationSemantic::FieldSpecularColor, InterpolationMode::NoperspectiveRaw, InterpolationMode::NoperspectiveRaw, InterpolationMode::Unused },
	{ InterpolationSemantic::MotionPositions, InterpolationMode::NoperspectiveQPackDivide, InterpolationMode::SmoothRaw, InterpolationMode::NoperspectiveQPackDivide },
	{ InterpolationSemantic::SoftParticleMappedDepth, InterpolationMode::NoperspectiveRaw, InterpolationMode::NoperspectiveRaw, InterpolationMode::Unused },
	{ InterpolationSemantic::StateMaterialMotionId, InterpolationMode::Flat, InterpolationMode::Flat, InterpolationMode::Flat },
	{ InterpolationSemantic::TerrainPages, InterpolationMode::Unused, InterpolationMode::Unused, InterpolationMode::Flat },
	{ InterpolationSemantic::FontArrayLayer, InterpolationMode::Flat, InterpolationMode::Unused, InterpolationMode::Unused },
	{ InterpolationSemantic::FontUvColorOrAnalyticScreen, InterpolationMode::NoperspectiveRaw, InterpolationMode::Unused, InterpolationMode::Unused },
	{ InterpolationSemantic::AnalyticPrimitiveParameters, InterpolationMode::Flat, InterpolationMode::Unused, InterpolationMode::Unused },
};

const size_t kInterpolationContractCount =
	sizeof(kInterpolationContract) / sizeof(kInterpolationContract[0]);

const EntryPointCoordinateContract kEntryPointCoordinateContract[] = {
	{ LegacyEntryPoint::DrawPolygon2D, DepthInterpretation::EyeZLegacyMapped, DepthValueSource::InputEyeZ, 0, 0, 0, 0, 1 },
	{ LegacyEntryPoint::DrawScaledBitmap, DepthInterpretation::EyeZLegacyMapped, DepthValueSource::SyntheticEyeZOne, 0, 0, 0, 0, 1 },
	{ LegacyEntryPoint::DrawScaledBitmapWithZ, DepthInterpretation::EyeZLegacyMapped, DepthValueSource::InputEyeZ, 0, 0, 0, 0, 1 },
	{ LegacyEntryPoint::DrawSimpleBitmap, DepthInterpretation::EyeZLegacyMapped, DepthValueSource::SyntheticEyeZOne, 0, 0, 0, 0, 1 },
	{ LegacyEntryPoint::DrawChunkedBitmap, DepthInterpretation::EyeZLegacyMapped, DepthValueSource::SyntheticEyeZOne, 0, 0, 0, 0, 1 },
	{ LegacyEntryPoint::DrawScaledChunkedBitmap, DepthInterpretation::EyeZLegacyMapped, DepthValueSource::SyntheticEyeZOne, 0, 0, 0, 0, 1 },
	{ LegacyEntryPoint::DrawFontCharacter, DepthInterpretation::Irrelevant, DepthValueSource::Unused, 0, 0, 1, 1, 1 },
	{ LegacyEntryPoint::SetPixelOrPoint, DepthInterpretation::AlreadyMapped, DepthValueSource::AlreadyMappedZero, 0, 0, 0, 0, 1 },
	{ LegacyEntryPoint::DrawLine, DepthInterpretation::AlreadyMapped, DepthValueSource::AlreadyMappedZero, 1, 1, 0, 0, 1 },
	{ LegacyEntryPoint::DrawSpecialLine, DepthInterpretation::AlreadyMapped, DepthValueSource::SpecialLineLiteralAlreadyMapped, 0, 0, 0, 0, 1 },
	{ LegacyEntryPoint::DrawSpecialLineBatch, DepthInterpretation::AlreadyMapped, DepthValueSource::SpecialLineLiteralAlreadyMapped, 0, 0, 0, 0, 1 },
	{ LegacyEntryPoint::FillRect, DepthInterpretation::Irrelevant, DepthValueSource::Unused, 0, 0, 1, 1, 1 },
	{ LegacyEntryPoint::FillCircle, DepthInterpretation::Irrelevant, DepthValueSource::Unused, 0, 0, 0, 0, 0 },
	{ LegacyEntryPoint::DrawCircle, DepthInterpretation::Irrelevant, DepthValueSource::Unused, 0, 0, 0, 0, 0 },
	{ LegacyEntryPoint::GetPixel, DepthInterpretation::Irrelevant, DepthValueSource::Unused, 0, 0, 1, 0, 1 },
};

const size_t kEntryPointCoordinateContractCount =
	sizeof(kEntryPointCoordinateContract) / sizeof(kEntryPointCoordinateContract[0]);

T1EligibilityResult EvaluateT1Eligibility(const T1EligibilityInput &input)
{
	T1EligibilityResult result = {};
	const bool valid_source = input.source == T1SourceKind::OrdinaryRoomBase ||
		input.source == T1SourceKind::OrdinaryRoomPostrender ||
		input.source == T1SourceKind::OrdinaryRoomSpecular ||
		input.source == T1SourceKind::OrdinaryStaticPolymodel;
	if (!valid_source)
		result.failure_bits |= kT1FailureSource;
	if (input.z_bias != 0.0f)
		result.failure_bits |= kT1FailureZBias;
	if (input.cc_and != 0)
		result.failure_bits |= kT1FailureTriviallyRejected;
	if (input.cc_or != 0)
		result.failure_bits |= kT1FailureNeedsLegacyClip;
	if (!input.all_z_finite_and_positive)
		result.failure_bits |= kT1FailureNonFiniteOrBehind;
	if (input.exclusion_bits != 0)
		result.failure_bits |= kT1FailureExcludedClass;
	if (!input.payload_representable)
		result.failure_bits |= kT1FailureUnrepresentablePayload;

	const uint32_t eligibility_failures = result.failure_bits;
	if (eligibility_failures == 0 && !input.retained_range_available)
	{
		result.failure_bits |= kT1FailureMissingRetainedRange;
		result.renderer_invariant_failure = 1;
		return result;
	}

	if (input.cc_and != 0)
	{
		result.whole_primitive_rejected = 1;
		return result;
	}
	if (eligibility_failures != 0)
	{
		result.use_legacy_t0 = 1;
		return result;
	}
	result.eligible = 1;
	return result;
}

T0ProjectionResult ProjectT0(float view_local_x, float view_local_y,
	float eye_z, float z_bias, float viewport_width, float viewport_height)
{
	T0ProjectionResult result = {};
	const float biased_eye_z = eye_z + z_bias;
	result.x_ndc = 2.0f * view_local_x / viewport_width - 1.0f;
	result.y_ndc = 2.0f * view_local_y / viewport_height - 1.0f;
	result.z_ndc = GL4DepthFromEyeZ(biased_eye_z);
	result.clip_w = 1.0f;
	// Deliberately no 0.0001 clamp: current UV/lightmap reciprocal does not
	// share GL4DepthFromEyeZ's denominator guard.
	result.reciprocal_q = 1.0f / biased_eye_z;
	return result;
}

T1ClipPosition ProjectT1(const float view_position[3],
	const T1ProjectionConstants &constants)
{
	T1ClipPosition result = {};
	const float zv = view_position[2];
	const float depth = GL4DepthFromEyeZ(zv);
	result.x = constants.ndc_center_x * zv + constants.ndc_scale_x * view_position[0];
	result.y = constants.ndc_center_y * zv - constants.ndc_scale_y * view_position[1];
	result.z = depth * zv;
	result.w = zv;
	return result;
}

TerrainVertexMappingOutput MapTerrainT2Vertex(
	const TerrainVertexMappingInput &input)
{
	TerrainVertexMappingOutput result = {};
	const float eye_z = std::max(input.rotated_eye_z, 0.000001f);
	const float q = 1.0f / eye_z;
	const float depth = std::max(0.0f, std::min(1.0f, 1.0f - q));
	result.base.position[0] = 2.0f * input.screen_x / input.viewport_width - 1.0f;
	result.base.position[1] = 2.0f * input.screen_y / input.viewport_height - 1.0f;
	result.base.position[2] = depth;
	result.base.rgba8 = 0xffffffffu;
	for (uint32_t i = 0; i < 2; ++i)
	{
		result.base.uv0[i] = input.base_uv[i] * q;
		result.base.uv1[i] = input.lightmap_uv[i] * q;
	}
	for (uint32_t i = 0; i < 3; ++i)
		result.payload.world_q[i] = input.world[i] * q;
	result.payload.world_q[3] = q;
	result.payload.packed_pages =
		(input.texture_page << 8u) | (input.lightmap_page & 0xffu);
	result.depth_interpretation = DepthInterpretation::AlreadyMapped;
	return result;
}

static int32_t ClampInt(int32_t value, int32_t minimum, int32_t maximum)
{
	return std::max(minimum, std::min(maximum, value));
}

UvCoordinate TopLeftPixelCenterToUv(PixelCoordinate pixel,
	PixelCoordinate extent)
{
	UvCoordinate result = {};
	result.u = (pixel.x + 0.5f) / extent.x;
	result.v = (pixel.y + 0.5f) / extent.y;
	return result;
}

UvCoordinate TopLeftPixelCenterToLegacyGlUv(PixelCoordinate pixel,
	PixelCoordinate extent)
{
	return TopLeftUvToLegacyGlUv(TopLeftPixelCenterToUv(pixel, extent));
}

FloatPixelCoordinate TopLeftUvToPixelCenter(UvCoordinate uv,
	PixelCoordinate extent)
{
	FloatPixelCoordinate result = {};
	result.x = uv.u * extent.x - 0.5f;
	result.y = uv.v * extent.y - 0.5f;
	return result;
}

PixelCoordinate TopLeftUvToClampedTexel(UvCoordinate uv,
	PixelCoordinate extent)
{
	PixelCoordinate result = {};
	result.x = ClampInt(static_cast<int32_t>(floorf(uv.u * extent.x)),
		0, extent.x - 1);
	result.y = ClampInt(static_cast<int32_t>(floorf(uv.v * extent.y)),
		0, extent.y - 1);
	return result;
}

UvCoordinate TopLeftUvToLegacyGlUv(UvCoordinate canonical_uv)
{
	return { canonical_uv.u, 1.0f - canonical_uv.v };
}

UvCoordinate LegacyGlUvToTopLeftUv(UvCoordinate legacy_uv)
{
	return { legacy_uv.u, 1.0f - legacy_uv.v };
}

float LegacyEquivalentFragmentY(float canonical_fragment_y,
	int32_t target_height)
{
	return static_cast<float>(target_height) - canonical_fragment_y;
}

LogicalRect TopLeftRectToLegacyGlBottomLeft(LogicalRect canonical_rect,
	PixelCoordinate target_extent)
{
	LogicalRect result = canonical_rect;
	result.y = target_extent.y - canonical_rect.y - canonical_rect.height;
	return result;
}

static LogicalRect ScaleRect(LogicalRect rect, uint32_t scale)
{
	const int32_t signed_scale = static_cast<int32_t>(scale);
	return { rect.x * signed_scale, rect.y * signed_scale,
		rect.width * signed_scale, rect.height * signed_scale };
}

VisibleRectCoordinateSet BuildVisibleRectCoordinateSet(
	LogicalRect logical_visible_rect, PixelCoordinate logical_target_extent,
	uint32_t ssaa_factor)
{
	VisibleRectCoordinateSet result = {};
	result.logical_top_left = logical_visible_rect;
	result.ssaa_top_left = ScaleRect(logical_visible_rect, ssaa_factor);
	const PixelCoordinate ssaa_extent = {
		logical_target_extent.x * static_cast<int32_t>(ssaa_factor),
		logical_target_extent.y * static_cast<int32_t>(ssaa_factor)
	};
	result.legacy_gl_ssaa_bottom_left =
		TopLeftRectToLegacyGlBottomLeft(result.ssaa_top_left, ssaa_extent);
	result.post_top_left = { 0, 0, logical_visible_rect.width,
		logical_visible_rect.height };
	return result;
}

UvTransform VisibleRectUvTransform(LogicalRect canonical_visible_rect,
	PixelCoordinate source_extent)
{
	UvTransform result = {};
	result.origin.u = static_cast<float>(canonical_visible_rect.x) /
		source_extent.x;
	result.origin.v = static_cast<float>(canonical_visible_rect.y) /
		source_extent.y;
	result.scale.u = static_cast<float>(canonical_visible_rect.width) /
		source_extent.x;
	result.scale.v = static_cast<float>(canonical_visible_rect.height) /
		source_extent.y;
	return result;
}

UvCoordinate ApplyUvTransform(UvCoordinate destination_uv,
	const UvTransform &transform)
{
	return { transform.origin.u + destination_uv.u * transform.scale.u,
		transform.origin.v + destination_uv.v * transform.scale.v };
}

PixelCoordinate MapNearestPixelCenter(PixelCoordinate destination,
	PixelCoordinate destination_extent, PixelCoordinate source_origin,
	PixelCoordinate source_visible_extent, PixelCoordinate source_extent)
{
	PixelCoordinate result = {};
	const float source_x = source_origin.x +
		(destination.x + 0.5f) * source_visible_extent.x / destination_extent.x;
	const float source_y = source_origin.y +
		(destination.y + 0.5f) * source_visible_extent.y / destination_extent.y;
	result.x = ClampInt(static_cast<int32_t>(floorf(source_x)), source_origin.x,
		source_origin.x + source_visible_extent.x - 1);
	result.y = ClampInt(static_cast<int32_t>(floorf(source_y)), source_origin.y,
		source_origin.y + source_visible_extent.y - 1);
	result.x = ClampInt(result.x, 0, source_extent.x - 1);
	result.y = ClampInt(result.y, 0, source_extent.y - 1);
	return result;
}

UvCoordinate MapFullscreenVisibleUv(PixelCoordinate destination,
	PixelCoordinate destination_extent, PixelCoordinate source_origin,
	PixelCoordinate source_visible_extent, PixelCoordinate source_extent)
{
	UvCoordinate result = {};
	result.u = (source_origin.x + (destination.x + 0.5f) *
		source_visible_extent.x / destination_extent.x) / source_extent.x;
	result.v = (source_origin.y + (destination.y + 0.5f) *
		source_visible_extent.y / destination_extent.y) / source_extent.y;
	return result;
}

WronskiTwoToOneMapping MapWronskiTwoToOne(PixelCoordinate destination,
	PixelCoordinate source_extent, PixelCoordinate source_visible_origin,
	PixelCoordinate source_visible_size)
{
	WronskiTwoToOneMapping result = {};
	result.base_uv.u = (2.0f * destination.x + 1.0f) / source_extent.x;
	result.base_uv.v = (2.0f * destination.y + 1.0f) / source_extent.y;
	result.visible_uv_min.u = (source_visible_origin.x + 0.5f) / source_extent.x;
	result.visible_uv_min.v = (source_visible_origin.y + 0.5f) / source_extent.y;
	result.visible_uv_max.u = (source_visible_origin.x +
		source_visible_size.x - 0.5f) / source_extent.x;
	result.visible_uv_max.v = (source_visible_origin.y +
		source_visible_size.y - 0.5f) / source_extent.y;
	return result;
}

PixelCoordinate MapGtaoReductionOrigin(PixelCoordinate ao_pixel,
	PixelCoordinate input_extent, PixelCoordinate ao_extent)
{
	PixelCoordinate result = {};
	result.x = static_cast<int32_t>(floorf((ao_pixel.x + 0.5f) *
		input_extent.x / ao_extent.x - 0.5f));
	result.y = static_cast<int32_t>(floorf((ao_pixel.y + 0.5f) *
		input_extent.y / ao_extent.y - 0.5f));
	return result;
}

UvCoordinate MapVisibleSuppressionUv(PixelCoordinate source_pixel,
	PixelCoordinate source_visible_origin, PixelCoordinate source_visible_size)
{
	UvCoordinate result = {};
	result.u = (source_pixel.x - source_visible_origin.x + 0.5f) /
		source_visible_size.x;
	result.v = (source_pixel.y - source_visible_origin.y + 0.5f) /
		source_visible_size.y;
	return result;
}

UvCoordinate MapLegacyGtaoNoisePosition(PixelCoordinate ao_pixel,
	PixelCoordinate ao_extent, LogicalRect canonical_source_visible_rect,
	PixelCoordinate source_extent)
{
	// GL4 computes `outuv * screen_size - noise_origin` in lower-left
	// coordinates.  Convert both operands, including the scaled visible origin,
	// before subtracting so odd/asymmetric overscan retains its exact phase.
	const LogicalRect legacy_visible = TopLeftRectToLegacyGlBottomLeft(
		canonical_source_visible_rect, source_extent);
	const UvCoordinate canonical_uv = TopLeftPixelCenterToUv(ao_pixel,
		ao_extent);
	const UvCoordinate legacy_uv = TopLeftUvToLegacyGlUv(canonical_uv);
	UvCoordinate result = {};
	result.u = legacy_uv.u * ao_extent.x -
		static_cast<float>(legacy_visible.x) * ao_extent.x / source_extent.x;
	result.v = legacy_uv.v * ao_extent.y -
		static_cast<float>(legacy_visible.y) * ao_extent.y / source_extent.y;
	return result;
}

UvCoordinate MapLegacyGtaoNoiseUv(PixelCoordinate ao_pixel,
	PixelCoordinate ao_extent, LogicalRect canonical_source_visible_rect,
	PixelCoordinate source_extent)
{
	const UvCoordinate position = MapLegacyGtaoNoisePosition(ao_pixel,
		ao_extent, canonical_source_visible_rect, source_extent);
	return { position.u / 4.0f, position.v / 4.0f };
}

UvCoordinate LegacyStoredVelocityToCanonical(UvCoordinate stored_velocity)
{
	return { stored_velocity.u, -stored_velocity.v };
}

UvCoordinate CanonicalVelocityToLegacyStored(UvCoordinate canonical_velocity)
{
	return { canonical_velocity.u, -canonical_velocity.v };
}

HistoryReprojectionCoordinate ReprojectHistoryCanonical(
	UvCoordinate current_uv, UvCoordinate legacy_stored_velocity,
	PixelCoordinate history_extent)
{
	HistoryReprojectionCoordinate result = {};
	// Pinned GL4: previous_uv = outuv - stored_velocity.  Convert both to the
	// canonical upper-left basis first; only Y changes sign.
	result.canonical_velocity =
		LegacyStoredVelocityToCanonical(legacy_stored_velocity);
	result.previous_uv.u = current_uv.u - result.canonical_velocity.u;
	result.previous_uv.v = current_uv.v - result.canonical_velocity.v;
	result.in_bounds = result.previous_uv.u >= 0.0f &&
		result.previous_uv.v >= 0.0f && result.previous_uv.u <= 1.0f &&
		result.previous_uv.v <= 1.0f;
	result.nearest_texel = TopLeftUvToClampedTexel(result.previous_uv,
		history_extent);
	return result;
}

LogicalRect ComputeCanonicalPresentRect(PixelCoordinate logical_extent,
	PixelCoordinate drawable_extent)
{
	// Preserve GL4Renderer::UpdatePresentRect's f32 aspect comparison, +0.5
	// rounding, and lower-left centering.  Only after those operations convert
	// its Y origin to the canonical upper-left swapchain rectangle.
	LogicalRect result = { 0, 0, std::max(drawable_extent.x, 0),
		std::max(drawable_extent.y, 0) };
	if (logical_extent.x <= 0 || logical_extent.y <= 0 ||
		drawable_extent.x <= 0 || drawable_extent.y <= 0)
		return result;
	const float base_aspect = static_cast<float>(logical_extent.x) /
		logical_extent.y;
	const float true_aspect = static_cast<float>(drawable_extent.x) /
		drawable_extent.y;
	if (base_aspect <= 0.0f || true_aspect <= 0.0f ||
		fabsf(base_aspect - true_aspect) < 0.001f)
		return result;

	if (base_aspect < true_aspect)
	{
		int32_t width = static_cast<int32_t>(drawable_extent.y *
			base_aspect + 0.5f);
		width = ClampInt(width, 1, drawable_extent.x);
		result.x = (drawable_extent.x - width) / 2;
		result.width = width;
	}
	else
	{
		int32_t height = static_cast<int32_t>(drawable_extent.x /
			base_aspect + 0.5f);
		height = ClampInt(height, 1, drawable_extent.y);
		const int32_t legacy_bottom = (drawable_extent.y - height) / 2;
		result.y = drawable_extent.y - legacy_bottom - height;
		result.height = height;
	}
	return result;
}

UvCoordinate MapPresentPixelToSourceUv(PixelCoordinate drawable_pixel,
	LogicalRect canonical_present_rect)
{
	UvCoordinate result = {};
	result.u = (drawable_pixel.x - canonical_present_rect.x + 0.5f) /
		canonical_present_rect.width;
	result.v = (drawable_pixel.y - canonical_present_rect.y + 0.5f) /
		canonical_present_rect.height;
	return result;
}

float SpecialLineMappedDepth(float eye_z, float z_bias)
{
	return std::max(0.0f, std::min(1.0f, 1.0f - 1.0f / (eye_z + z_bias)));
}

float GenericFogAmount(float interpolated_biased_mapped_depth,
	float fog_near_eye_z, float fog_far_eye_z)
{
	const float mapped_near = GL4DepthFromEyeZ(fog_near_eye_z);
	const float mapped_far = GL4DepthFromEyeZ(fog_far_eye_z);
	const float denominator = std::max(mapped_far - mapped_near, 0.0001f);
	return std::max(0.0f, std::min(1.0f,
		(interpolated_biased_mapped_depth - mapped_near) / denominator));
}

float FogRoomVertexAlpha(const FogRoomAlphaInput &input)
{
	auto dot = [](const float a[3], const float b[3]) {
		return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
	};
	float magnitude = 1.0f;
	if (input.viewer_outside != 0)
	{
		const float distance = dot(input.vertex, input.room_plane) +
			input.room_plane_distance;
		const float t = input.room_eye_distance /
			(input.room_eye_distance - distance);
		float portal[3];
		for (uint32_t i = 0; i < 3; ++i)
			portal[i] = input.viewer_eye[i] + t *
				(input.vertex[i] - input.viewer_eye[i]);
		const float eye_distance = -dot(input.viewer_forward, portal);
		magnitude = dot(input.viewer_forward, input.vertex) + eye_distance;
	}
	else if (input.viewer_inside != 0)
	{
		magnitude = dot(input.room_plane, input.vertex) +
			input.room_plane_distance;
	}
	const float scalar = std::max(0.0f, std::min(1.0f,
		magnitude / input.fog_depth));
	return scalar * input.room_light_value;
}

static_assert(sizeof(kCoordinateSourceContract) / sizeof(kCoordinateSourceContract[0]) ==
	static_cast<size_t>(CoordinateSource::Count), "coordinate source contract mismatch");
static_assert(sizeof(kInterpolationContract) / sizeof(kInterpolationContract[0]) ==
	static_cast<size_t>(InterpolationSemantic::Count), "interpolation contract mismatch");
static_assert(sizeof(kEntryPointCoordinateContract) /
	sizeof(kEntryPointCoordinateContract[0]) ==
	static_cast<size_t>(LegacyEntryPoint::Count), "entry point coordinate contract mismatch");

} // namespace render
} // namespace piccu
