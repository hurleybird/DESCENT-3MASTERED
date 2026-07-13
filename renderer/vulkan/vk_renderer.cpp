#include "vk_renderer.h"

#include "3d.h"
#include "bitmap.h"
#include "../HardwareInternal.h"
#include "../core/render_coordinate_contract.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#if defined(_MSC_VER)
#include <malloc.h>
#endif

using namespace piccu::render;
using namespace piccu::render::vk;

static_assert(!std::is_abstract<VulkanRenderer>::value,
	"VulkanRenderer must explicitly implement the complete IRenderer surface");
static_assert(sizeof(renderer_retained_terrain_cell) ==
	sizeof(TerrainEmitterCell), "public T2 cell ABI mismatch");
static_assert(sizeof(renderer_retained_terrain_work) ==
	sizeof(TerrainWorkItem), "public T2 work ABI mismatch");
static_assert(sizeof(renderer_retained_terrain_batch) ==
	sizeof(TerrainBatchInput), "public T2 batch ABI mismatch");
static_assert(sizeof(renderer_retained_terrain_view) ==
	sizeof(TerrainViewInput), "public T2 view ABI mismatch");

void *VulkanRenderer::operator new(std::size_t size)
{
#if defined(_MSC_VER)
	void *memory = _aligned_malloc(size, alignof(VulkanRenderer));
#else
	void *memory = std::malloc(size);
#endif
	if (!memory)
		throw std::bad_alloc();
	return memory;
}

void VulkanRenderer::operator delete(void *memory) noexcept
{
#if defined(_MSC_VER)
	_aligned_free(memory);
#else
	std::free(memory);
#endif
}

namespace
{

template <typename T>
T ClampValue(T value, T minimum, T maximum)
{
	return value < minimum ? minimum : (value > maximum ? maximum : value);
}

float ClampUnit(float value)
{
	return ClampValue(value, 0.0f, 1.0f);
}

void Identity(float matrix[16])
{
	std::memset(matrix, 0, sizeof(float) * 16);
	matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0f;
}

void Multiply4x4(float result[16], const float a[16], const float b[16])
{
	float temporary[16] = {};
	for (uint32_t column = 0; column < 4; ++column)
		for (uint32_t row = 0; row < 4; ++row)
			for (uint32_t k = 0; k < 4; ++k)
				temporary[column * 4 + row] +=
					a[k * 4 + row] * b[column * 4 + k];
	std::memcpy(result, temporary, sizeof(temporary));
}

bool Invert4x4(float output[16], const float input[16])
{
	double augmented[4][8] = {};
	for (uint32_t row = 0; row < 4; ++row)
		for (uint32_t column = 0; column < 4; ++column)
		{
			augmented[row][column] = input[column * 4 + row];
			augmented[row][column + 4] = row == column ? 1.0 : 0.0;
		}
	for (uint32_t column = 0; column < 4; ++column)
	{
		uint32_t pivot = column;
		for (uint32_t row = column + 1; row < 4; ++row)
			if (std::fabs(augmented[row][column]) >
				std::fabs(augmented[pivot][column]))
				pivot = row;
		if (std::fabs(augmented[pivot][column]) < 1.0e-12)
			return false;
		if (pivot != column)
			for (uint32_t i = 0; i < 8; ++i)
				std::swap(augmented[pivot][i], augmented[column][i]);
		const double divisor = augmented[column][column];
		for (uint32_t i = 0; i < 8; ++i)
			augmented[column][i] /= divisor;
		for (uint32_t row = 0; row < 4; ++row)
		{
			if (row == column)
				continue;
			const double factor = augmented[row][column];
			for (uint32_t i = 0; i < 8; ++i)
				augmented[row][i] -= factor * augmented[column][i];
		}
	}
	for (uint32_t row = 0; row < 4; ++row)
		for (uint32_t column = 0; column < 4; ++column)
			output[column * 4 + row] =
				static_cast<float>(augmented[row][column + 4]);
	return true;
}

uint32_t PackRgba(uint32_t red, uint32_t green, uint32_t blue, uint32_t alpha)
{
	return (red & 0xffu) | ((green & 0xffu) << 8) |
		((blue & 0xffu) << 16) | ((alpha & 0xffu) << 24);
}

uint64_t HashRetainedBytes(uint64_t hash, const void *bytes, size_t size)
{
	const uint8_t *source = static_cast<const uint8_t *>(bytes);
	for (size_t i = 0; i < size; ++i)
	{
		hash ^= source[i];
		hash *= UINT64_C(1099511628211);
	}
	return hash;
}

uint32_t ColorRed(ddgr_color color) { return GR_COLOR_RED(color); }
uint32_t ColorGreen(ddgr_color color) { return GR_COLOR_GREEN(color); }
uint32_t ColorBlue(ddgr_color color) { return GR_COLOR_BLUE(color); }

void ColorFloats(ddgr_color color, float output[4], float alpha = 1.0f)
{
	output[0] = ColorRed(color) / 255.0f;
	output[1] = ColorGreen(color) / 255.0f;
	output[2] = ColorBlue(color) / 255.0f;
	output[3] = alpha;
}

bool NormalizePreferred(const renderer_preferred_state *requested,
	renderer_preferred_state *normalized)
{
	if (!requested || !normalized || requested->width <= 0 ||
		requested->height <= 0)
		return false;
	*normalized = *requested;
	if (normalized->window_width <= 0)
		normalized->window_width = normalized->width;
	if (normalized->window_height <= 0)
		normalized->window_height = normalized->height;
	if (normalized->supersampling_factor != 1 &&
		normalized->supersampling_factor != 2 &&
		normalized->supersampling_factor != 4)
		normalized->supersampling_factor = 1;
	if (normalized->msaa_samples != 0 && normalized->msaa_samples != 2 &&
		normalized->msaa_samples != 4 && normalized->msaa_samples != 8)
		normalized->msaa_samples = 0;
	if (!(normalized->gamma > 0.0f) || !std::isfinite(normalized->gamma))
		normalized->gamma = 1.0f;
	normalized->gtao_sample_count = static_cast<ushort>(
		ClampValue<uint32_t>(normalized->gtao_sample_count, 1, 64));
	normalized->gtao_blur_radius = static_cast<ubyte>(
		ClampValue<uint32_t>(normalized->gtao_blur_radius, 0, 8));
	normalized->pixel_motion_blur_samples = static_cast<ubyte>(
		ClampValue<uint32_t>(normalized->pixel_motion_blur_samples, 3, 17));
	return true;
}

} // namespace

VulkanRenderer::VulkanRenderer(IVulkanRuntime *runtime)
	: runtime_(runtime), last_failure_(RuntimeFailure::None), initialized_(false)
{
	std::memset(&preferred_, 0, sizeof(preferred_));
	ResetFacadeState();
	scratch_vertices_.reserve(512);
	scratch_indices_.reserve(1536);
	scratch_source_points_.reserve(512);
	scratch_poly_batch_items_.reserve(128);
	scratch_retained_eligibility_.reserve(128);
	scratch_perspective_.reserve(512);
	scratch_motion_.reserve(512);
	scratch_specular_.reserve(512);
	room_aux_.reserve(256);
}

VulkanRenderer::~VulkanRenderer()
{
	Close();
}

void VulkanRenderer::AttachRuntime(IVulkanRuntime *runtime)
{
	if (initialized_ && runtime != runtime_)
	{
		Fail(RuntimeFailure::InvalidLifecycle, "AttachRuntime");
		return;
	}
	runtime_ = runtime;
}

void VulkanRenderer::Fail(RuntimeFailure failure, const char *operation) const
{
	last_failure_ = failure;
	if (runtime_)
		runtime_->ReportFailure(failure, operation);
}

RenderCaptureSegment *VulkanRenderer::Capture(const char *operation) const
{
	if (!initialized_)
	{
		Fail(RuntimeFailure::NotInitialized, operation);
		return nullptr;
	}
	if (!runtime_)
	{
		Fail(RuntimeFailure::MissingRuntime, operation);
		return nullptr;
	}
	RenderCaptureSegment *capture = runtime_->ActiveCapture();
	if (!capture || capture->GetLifecycle() != RenderCaptureSegment::Lifecycle::Capturing)
	{
		Fail(RuntimeFailure::CaptureUnavailable, operation);
		return nullptr;
	}
	return capture;
}

bool VulkanRenderer::Append(const CaptureCommand &command, const char *operation)
{
	RenderCaptureSegment *capture = Capture(operation);
	if (!capture || !capture->AppendCopy(command))
	{
		if (capture)
			Fail(RuntimeFailure::CaptureRejected, operation);
		return false;
	}
	return true;
}

void VulkanRenderer::ResetFacadeState()
{
	initialized_ = false;
	last_failure_ = RuntimeFailure::None;
	std::memset(&public_state_, 0, sizeof(public_state_));
	std::memset(&frame_dynamic_, 0, sizeof(frame_dynamic_));
	std::memset(&legacy_state_, 0, sizeof(legacy_state_));
	std::memset(&texture_shadow_, 0, sizeof(texture_shadow_));
	std::memset(&primitive_scratch_, 0, sizeof(primitive_scratch_));
	std::memset(&current_stats_, 0, sizeof(current_stats_));
	std::memset(&last_stats_, 0, sizeof(last_stats_));
	std::memset(&current_view_, 0, sizeof(current_view_));
	std::memset(&presentation_, 0, sizeof(presentation_));
	std::memset(&cockpit_backing_, 0, sizeof(cockpit_backing_));
	std::memset(dynamic_lights_, 0, sizeof(dynamic_lights_));
	std::memset(&specular_block_, 0, sizeof(specular_block_));
	std::memset(&terrain_fog_, 0, sizeof(terrain_fog_));

	Identity(legacy_state_.current_view);
	Identity(legacy_state_.previous_view);
	Identity(legacy_state_.current_object);
	Identity(legacy_state_.previous_object);
	Identity(legacy_state_.current_submodel);
	Identity(legacy_state_.previous_submodel);
	Identity(current_view_.projection);
	Identity(current_view_.view);
	Identity(current_view_.view_projection);
	Identity(current_view_.inverse_modelview);
	Identity(current_view_.inverse_view_projection);
	Identity(current_view_.previous_view_projection);
	Identity(current_view_.cockpit_previous_view_projection);

	public_state_.cur_bilinear_state = 1;
	public_state_.cur_zbuffer_state = 1;
	public_state_.cur_mip_state = 1;
	public_state_.cur_texture_type = TT_PERSPECTIVE;
	public_state_.cur_texture_quality = 2;
	public_state_.cur_color_model = CM_MONO;
	public_state_.cur_light_state = LS_NONE;
	public_state_.cur_alpha_type = AT_ALWAYS;
	public_state_.cur_alpha = 255;
	public_state_.cur_wrap_type = WT_WRAP;
	public_state_.cur_near_z = 0.0f;
	public_state_.cur_far_z = 1.0f;
	public_state_.gamma_value = 1.0f;

	legacy_state_.texture_type = TT_PERSPECTIVE;
	legacy_state_.bitmap_handle = -1;
	legacy_state_.bitmap_version = kInvalidId;
	legacy_state_.map_type = MAP_TYPE_BITMAP;
	legacy_state_.overlay_type = OT_NONE;
	legacy_state_.overlay_handle = -1;
	legacy_state_.overlay_version = kInvalidId;
	legacy_state_.specular_version = kInvalidId;
	legacy_state_.lighting_state = LS_NONE;
	legacy_state_.color_model = CM_MONO;
	legacy_state_.alpha_type = AT_ALWAYS;
	legacy_state_.alpha_value = 255;
	legacy_state_.alpha_factor = 1.0f;
	legacy_state_.blend_class = static_cast<uint32_t>(BlendClass::Opaque);
	legacy_state_.wrap_type = WT_WRAP;
	legacy_state_.filtering = 1;
	legacy_state_.mipping = 1;
	legacy_state_.depth_test_enabled = 1;
	legacy_state_.depth_write_enabled = 1;
	legacy_state_.depth_compare = 1; // LEQUAL in the compiler's normalized key.
	legacy_state_.reported_far_z = 1.0f;
	legacy_state_.selected_draw_buffers = kDefaultSceneSelectedDrawBuffers;
	for (uint32_t i = 0; i < 5; ++i)
		legacy_state_.attachment_color_masks[i] = kChannelRgba;
	legacy_state_.viewport = legacy_state_.scissor = kInvalidId;
	legacy_state_.ao_weight = 1.0f;
	legacy_state_.gamma = 1.0f;
	legacy_state_.cockpit_backing_state = kInvalidId;

	for (uint32_t i = 0; i < 4; ++i)
	{
		texture_shadow_.units[i].logical_handle = -1;
		texture_shadow_.units[i].version = kInvalidId;
		texture_shadow_.units[i].map_type = MAP_TYPE_UNKNOWN;
		texture_shadow_.units[i].sampler_index = 0;
		font_colors_[i] = 0x00ffffff;
	}

	active_target_ = RenderTargetClass::Scene;
	active_clip_ = { 0, 0, 0, 0 };
	active_layout_ = active_target_version_ = active_view_ = kInvalidId;
	std::memset(&active_version_record_, 0, sizeof(active_version_record_));
	presentation_signature_ = presentation_wsi_ = presentation_rect_ = kInvalidId;
	frame_interval_open_ = post_present_pending_ = cockpit_open_ = false;
	font_glyphs_pending_ = framebuffer_copy_state_ = soft_depth_acquired_ = false;
	presented_frame_serial_ = 1;
	view_interval_serial_ = font_enqueue_serial_ = font_flush_serial_ = 0;
	font_texture_width_ = font_texture_height_ = 0;
	cockpit_capture_serial_ = perf_marker_serial_ = 0;
	readback_request_serial_ = soft_depth_snapshot_serial_ = 0;
	common_depth_ = selected_pipeline_ = 0;
	retained_terrain_mesh_ = { kInvalidId, 0 };
	retained_terrain_source_id_ = retained_terrain_source_generation_ = 0;
	retained_terrain_mesh_generation_ = 0;
	retained_terrain_base_version_ = retained_terrain_lightmap_version_ =
		kInvalidId;
	std::memset(common_object_transforms_, 0,
		sizeof(common_object_transforms_));
	std::memset(common_object_transform_valid_, 0,
		sizeof(common_object_transform_valid_));
	Identity(common_object_transforms_[0]);
	common_object_transform_valid_[0] = true;
	dynamic_light_count_ = 0;
	have_specular_block_ = false;
	room_aux_.clear();
	retained_mesh_cache_.clear();
	current_room_ = -1;
}

CapturedPreferredState VulkanRenderer::CapturedPreferred() const
{
	CapturedPreferredState result = {};
	result.mipping = preferred_.mipping;
	result.filtering = preferred_.filtering;
	result.antialised = preferred_.antialised ? 1u : 0u;
	result.bit_depth = preferred_.bit_depth;
	result.gamma = preferred_.gamma;
	result.width = static_cast<uint32_t>(preferred_.width);
	result.height = static_cast<uint32_t>(preferred_.height);
	result.window_width = static_cast<uint32_t>(preferred_.window_width);
	result.window_height = static_cast<uint32_t>(preferred_.window_height);
	result.fullscreen = preferred_.fullscreen ? 1u : 0u;
	result.supersampling_factor = preferred_.supersampling_factor;
	result.msaa_samples = preferred_.msaa_samples;
	result.per_pixel_lighting = preferred_.per_pixel_lighting ? 1u : 0u;
	result.bloom_enabled = preferred_.bloom_enabled ? 1u : 0u;
	result.bloom_threshold = preferred_.bloom_threshold;
	result.bloom_intensity = preferred_.bloom_intensity;
	result.bloom_spread = preferred_.bloom_spread;
	result.gtao_enabled = preferred_.gtao_enabled ? 1u : 0u;
	result.gtao_resolution = preferred_.gtao_resolution;
	result.gtao_sample_count = preferred_.gtao_sample_count;
	result.gtao_blur_radius = preferred_.gtao_blur_radius;
	result.gtao_radius = preferred_.gtao_radius;
	result.gtao_intensity = preferred_.gtao_intensity;
	result.gtao_bias = preferred_.gtao_bias;
	result.gtao_overscan_percent = preferred_.gtao_overscan_percent;
	result.gtao_debug_preview = preferred_.gtao_debug_preview ? 1u : 0u;
	result.gtao_temporal_blend = preferred_.gtao_temporal_blend;
	result.gtao_temporal_depth_reject = preferred_.gtao_temporal_depth_reject;
	result.gtao_temporal_velocity_reject = preferred_.gtao_temporal_velocity_reject;
	result.gtao_temporal_debug_preview =
		preferred_.gtao_temporal_debug_preview ? 1u : 0u;
	result.gtao_terrain_occlusion = preferred_.gtao_terrain_occlusion;
	result.gtao_polyobject_occlusion = preferred_.gtao_polyobject_occlusion;
	result.gtao_mine_rock_occlusion = preferred_.gtao_mine_rock_occlusion;
	result.gtao_mine_occlusion = preferred_.gtao_mine_occlusion;
	result.motion_vector_mode = preferred_.motion_vector_mode;
	result.motion_vector_debug_preview =
		preferred_.motion_vector_debug_preview ? 1u : 0u;
	result.pixel_motion_blur_strength = preferred_.pixel_motion_blur_strength;
	result.combined_motion_blur = preferred_.combined_motion_blur ? 1u : 0u;
	result.combined_motion_blur_legacy_strength =
		preferred_.combined_motion_blur_legacy_strength;
	result.combined_motion_blur_legacy_frame_time =
		preferred_.combined_motion_blur_legacy_frame_time;
	result.combined_motion_blur_legacy_sphere_size =
		preferred_.combined_motion_blur_legacy_sphere_size;
	result.combined_motion_blur_legacy_copy_density =
		preferred_.combined_motion_blur_legacy_copy_density;
	result.combined_motion_blur_legacy_max_iterations =
		preferred_.combined_motion_blur_legacy_max_iterations;
	result.combined_motion_blur_legacy_alpha_exponent =
		preferred_.combined_motion_blur_legacy_alpha_exponent;
	result.pixel_motion_blur_periphery_strength =
		preferred_.pixel_motion_blur_periphery_strength;
	result.pixel_motion_blur_legacy_object_strength =
		preferred_.pixel_motion_blur_legacy_object_strength;
	result.pixel_motion_blur_center_suppression =
		preferred_.pixel_motion_blur_center_suppression;
	result.pixel_motion_blur_legacy_object_center_suppression =
		preferred_.pixel_motion_blur_legacy_object_center_suppression;
	result.pixel_motion_blur_samples = preferred_.pixel_motion_blur_samples;
	result.afterburner_fov_multiplier = preferred_.afterburner_fov_multiplier;
	result.afterburner_pixel_blur_multiplier =
		preferred_.afterburner_pixel_blur_multiplier;
	return result;
}

int VulkanRenderer::Init(oeApplication *app, renderer_preferred_state *pref_state)
{
	if (initialized_)
	{
		Fail(RuntimeFailure::InvalidLifecycle, "Init");
		return 0;
	}
	if (!runtime_)
	{
		Fail(RuntimeFailure::MissingRuntime, "Init");
		return 0;
	}
	renderer_preferred_state normalized = {};
	if (!NormalizePreferred(pref_state, &normalized))
	{
		Fail(RuntimeFailure::InvalidArgument, "Init");
		return 0;
	}
	ResetFacadeState();
	preferred_ = normalized;
	if (!runtime_->Initialize(app, CapturedPreferred()))
	{
		Fail(RuntimeFailure::PresentationFailed, "Init");
		return 0;
	}
	initialized_ = true;
	public_state_.initted = 1;
	public_state_.screen_width = preferred_.width;
	public_state_.screen_height = preferred_.height;
	public_state_.view_width = preferred_.window_width;
	public_state_.view_height = preferred_.window_height;
	public_state_.gamma_value = preferred_.gamma;
	legacy_state_.gamma = preferred_.gamma;
	RenderCaptureSegment *initial_capture = Capture("Init");
	if (!initial_capture)
	{
		runtime_->Shutdown();
		initialized_ = false;
		public_state_.initted = 0;
		return 0;
	}
	presented_frame_serial_ = initial_capture->PresentedFrameSerial();
	last_failure_ = RuntimeFailure::None;
	return 1;
}

void VulkanRenderer::Close()
{
	if (!initialized_)
		return;
	ReleaseRetainedMeshCache();
	if (runtime_ && retained_terrain_mesh_.id != kInvalidId)
	{
		IRetainedWorld *world = runtime_->RetainedWorldBridge();
		if (world) world->ReleaseMesh(retained_terrain_mesh_);
		retained_terrain_mesh_ = { kInvalidId, 0 };
	}
	if (runtime_)
		runtime_->Shutdown();
	retained_mesh_cache_.clear();
	public_state_.initted = 0;
	initialized_ = false;
	frame_interval_open_ = post_present_pending_ = cockpit_open_ = false;
}

RendererCapabilities VulkanRenderer::GetCapabilities() const
{
	RendererCapabilities capabilities = {};
	capabilities.backend = RENDERER_BACKEND_VULKAN;
	capabilities.retained_rooms = true;
	capabilities.retained_terrain = true;
	capabilities.retained_polymodels = true;
	capabilities.particle_instance_batch = true;
	capabilities.per_pixel_lighting = true;
	capabilities.split_and_field_specular = true;
	capabilities.post_processing = true;
	capabilities.pixel_motion_vectors = true;
	capabilities.late_close_screen_pass = true;
	capabilities.editor_readback = true;
	return capabilities;
}

void VulkanRenderer::SetFrameDynamicState(
	const renderer_frame_dynamic_state &state)
{
	if (!std::isfinite(state.frame_time) ||
		!std::isfinite(state.afterburner_scalar))
	{
		Fail(RuntimeFailure::InvalidArgument, "SetFrameDynamicState");
		return;
	}
	frame_dynamic_ = state;
}

int VulkanRenderer::SetPreferredState(renderer_preferred_state *pref_state)
{
	if (!initialized_ || !runtime_)
	{
		Fail(initialized_ ? RuntimeFailure::MissingRuntime :
			RuntimeFailure::NotInitialized, "SetPreferredState");
		return 0;
	}
	if (frame_interval_open_ || cockpit_open_ || post_present_pending_)
	{
		Fail(RuntimeFailure::InvalidLifecycle, "SetPreferredState");
		return 0;
	}
	renderer_preferred_state normalized = {};
	if (!NormalizePreferred(pref_state, &normalized))
	{
		Fail(RuntimeFailure::InvalidArgument, "SetPreferredState");
		return 0;
	}
	const renderer_preferred_state old = preferred_;
	preferred_ = normalized;
	if (!runtime_->ApplyPreferredState(CapturedPreferred()))
	{
		preferred_ = old;
		Fail(RuntimeFailure::PresentationFailed, "SetPreferredState");
		return 0;
	}
	public_state_.screen_width = preferred_.width;
	public_state_.screen_height = preferred_.height;
	public_state_.view_width = preferred_.window_width;
	public_state_.view_height = preferred_.window_height;
	SetGammaValue(preferred_.gamma);
	MotionCaptureLockTransitionInput lock = {};
	lock.currently_locked = legacy_state_.motion_capture_locked;
	lock.event = MotionCaptureLockEvent::FramebufferRebuild;
	legacy_state_.motion_capture_locked =
		EvaluateMotionCaptureLockTransition(lock).resulting_locked;
	return 1;
}

void VulkanRenderer::SetMipState(sbyte state)
{
	public_state_.cur_mip_state = state != 0;
	legacy_state_.mipping = state != 0;
}

void VulkanRenderer::SetFogState(sbyte on)
{
	public_state_.cur_fog_state = on != 0;
	legacy_state_.fog_enabled = on != 0;
}

void VulkanRenderer::SetFogBorders(float fog_near, float fog_far)
{
	if (!std::isfinite(fog_near) || !std::isfinite(fog_far) ||
		fog_near <= 0.0f || fog_far <= 0.0f)
	{
		Fail(RuntimeFailure::InvalidArgument, "SetFogBorders");
		return;
	}
	const float mapped_near = ClampUnit(1.0f - 1.0f / fog_near);
	const float mapped_far = ClampUnit(1.0f - 1.0f / fog_far);
	public_state_.cur_fog_start = mapped_near;
	public_state_.cur_fog_end = mapped_far;
	legacy_state_.fog_near_mapped = mapped_near;
	legacy_state_.fog_far_mapped = mapped_far;
	terrain_fog_.params[0] = fog_near;
	terrain_fog_.params[1] = fog_far;
}

void VulkanRenderer::SetFlatColor(ddgr_color color)
{
	public_state_.cur_color = color;
	legacy_state_.flat_color = color;
}

void VulkanRenderer::SetTextureType(texture_type type)
{
	if (type < TT_FLAT || type > TT_PERSPECTIVE_SPECIAL)
	{
		Fail(RuntimeFailure::InvalidArgument, "SetTextureType");
		return;
	}
	public_state_.cur_texture_type = type;
	public_state_.cur_texture_quality = type == TT_FLAT ? 0 : 2;
	legacy_state_.texture_type = static_cast<uint32_t>(type);
}

void VulkanRenderer::SetFiltering(sbyte state)
{
	public_state_.cur_bilinear_state = state != 0;
	legacy_state_.filtering = state != 0;
}

void VulkanRenderer::SetZBufferState(sbyte state)
{
	public_state_.cur_zbuffer_state = state != 0;
	legacy_state_.depth_test_enabled = state != 0;
}

void VulkanRenderer::SetZValues(float nearz, float farz)
{
	if (!std::isfinite(nearz) || !std::isfinite(farz))
	{
		Fail(RuntimeFailure::InvalidArgument, "SetZValues");
		return;
	}
	public_state_.cur_near_z = nearz;
	public_state_.cur_far_z = farz;
	legacy_state_.reported_near_z = nearz;
	legacy_state_.reported_far_z = farz;
}

void VulkanRenderer::SetOverlayMap(int handle)
{
	legacy_state_.overlay_handle = handle;
	legacy_state_.overlay_version = kInvalidId;
	texture_shadow_.units[1].logical_handle = handle;
	texture_shadow_.units[1].map_type = MAP_TYPE_LIGHTMAP;
}

void VulkanRenderer::SetOverlayType(ubyte type)
{
	if (type > OT_BLEND_SATURATE)
	{
		Fail(RuntimeFailure::InvalidArgument, "SetOverlayType");
		return;
	}
	legacy_state_.overlay_type = type;
}

void VulkanRenderer::SetFogColor(ddgr_color fogcolor)
{
	public_state_.cur_fog_color = fogcolor;
	legacy_state_.fog_color = fogcolor;
	ColorFloats(fogcolor, terrain_fog_.fog_color);
}

void VulkanRenderer::SetAlphaType(sbyte alphatype)
{
	AlphaTypeTransitionInput input = {};
	input.current_type = legacy_state_.alpha_type;
	input.requested_type = static_cast<uint32_t>(static_cast<ubyte>(alphatype));
	input.current_alpha_byte = legacy_state_.alpha_value;
	input.active_texture_unit = texture_shadow_.last_selected_unit;
	const AlphaTypeTransitionDecision decision = EvaluateAlphaTypeTransition(input);
	if (!decision.valid_type)
	{
		Fail(RuntimeFailure::InvalidArgument, "SetAlphaType");
		return;
	}
	if (decision.redundant_early_return)
		return;
	legacy_state_.alpha_value = decision.resulting_alpha_byte;
	legacy_state_.blend_class = static_cast<uint32_t>(decision.blend_class);
	legacy_state_.luminance_mask = decision.luminance_post_mask;
	legacy_state_.alpha_type = input.requested_type;
	public_state_.cur_alpha_type = alphatype;
	public_state_.cur_alpha = static_cast<int>(decision.resulting_alpha_byte);
	if (decision.force_texture_type_perspective)
		SetTextureType(TT_PERSPECTIVE);
	if (decision.select_texture_unit_zero)
		texture_shadow_.last_selected_unit = 0;
	if (decision.configure_auxiliary_blend_masks)
	{
		LiteralAttachmentMasks masks = {};
		for (uint32_t i = 0; i < 5; ++i)
			masks.rgba[i] = legacy_state_.attachment_color_masks[i];
		masks = ApplyConfigurePostMaskBlend(masks);
		for (uint32_t i = 0; i < 5; ++i)
			legacy_state_.attachment_color_masks[i] = masks.rgba[i];
	}
}

void VulkanRenderer::SetAlphaValue(ubyte val)
{
	public_state_.cur_alpha = val;
	legacy_state_.alpha_value = val;
}

void VulkanRenderer::SetAlphaFactor(float val)
{
	if (!std::isfinite(val))
	{
		Fail(RuntimeFailure::InvalidArgument, "SetAlphaFactor");
		return;
	}
	legacy_state_.alpha_factor = val;
}

float VulkanRenderer::GetAlphaFactor()
{
	return legacy_state_.alpha_factor;
}

void VulkanRenderer::SetWrapType(wrap_type val)
{
	if (val < WT_WRAP || val > WT_WRAP_V)
	{
		Fail(RuntimeFailure::InvalidArgument, "SetWrapType");
		return;
	}
	public_state_.cur_wrap_type = val;
	legacy_state_.wrap_type = static_cast<uint32_t>(val);
}

void VulkanRenderer::SetZBufferWriteMask(int state)
{
	legacy_state_.depth_write_enabled = state != 0;
}

void VulkanRenderer::SetCoplanarPolygonOffset(float factor)
{
	if (!std::isfinite(factor))
	{
		Fail(RuntimeFailure::InvalidArgument, "SetCoplanarPolygonOffset");
		return;
	}
	legacy_state_.coplanar_factor = factor;
	legacy_state_.coplanar_units = factor;
	legacy_state_.coplanar_enabled = factor != 0.0f;
}

void VulkanRenderer::SetCullFace(bool state)
{
	legacy_state_.cull_enabled = state ? 1u : 0u;
}

void VulkanRenderer::SetColorModel(color_model model)
{
	if (model != CM_MONO && model != CM_RGB)
	{
		Fail(RuntimeFailure::InvalidArgument, "SetColorModel");
		return;
	}
	public_state_.cur_color_model = model;
	legacy_state_.color_model = static_cast<uint32_t>(model);
}

void VulkanRenderer::SetLighting(light_state state)
{
	if (state < LS_NONE || state > LS_FLAT_GOURAUD)
	{
		Fail(RuntimeFailure::InvalidArgument, "SetLighting");
		return;
	}
	if (state == LS_PHONG && !preferred_.per_pixel_lighting)
		state = LS_GOURAUD;
	public_state_.cur_light_state = state;
	legacy_state_.lighting_state = static_cast<uint32_t>(state);
}

void VulkanRenderer::SetPerPixelLightingDirection(const vector *lightdir)
{
	if (!lightdir)
	{
		Fail(RuntimeFailure::InvalidArgument, "SetPerPixelLightingDirection");
		return;
	}
	legacy_state_.per_pixel_direction[0] = lightdir->x;
	legacy_state_.per_pixel_direction[1] = lightdir->y;
	legacy_state_.per_pixel_direction[2] = lightdir->z;
	legacy_state_.per_pixel_direction[3] = 0.0f;
}

void VulkanRenderer::SetPerPixelDynamicLighting(const vector *face_normal,
	int count, const renderer_per_pixel_light *lights)
{
	dynamic_light_count_ = 0;
	legacy_state_.dynamic_light_count = 0;
	if (!preferred_.per_pixel_lighting || count <= 0)
		return;
	if (!face_normal || !lights)
	{
		Fail(RuntimeFailure::InvalidArgument, "SetPerPixelDynamicLighting");
		return;
	}
	dynamic_light_count_ = static_cast<uint32_t>(
		std::min(count, static_cast<int>(kMaxDynamicLights)));
	legacy_state_.dynamic_light_count = dynamic_light_count_;
	legacy_state_.dynamic_face_normal[0] = face_normal->x;
	legacy_state_.dynamic_face_normal[1] = face_normal->y;
	legacy_state_.dynamic_face_normal[2] = face_normal->z;
	legacy_state_.dynamic_face_normal[3] = 0.0f;
	for (uint32_t i = 0; i < dynamic_light_count_; ++i)
	{
		GpuDynamicLight &output = dynamic_lights_[i];
		std::memset(&output, 0, sizeof(output));
		for (uint32_t channel = 0; channel < 3; ++channel)
		{
			output.position_radius[channel] = lights[i].position[channel];
			output.color_falloff[channel] = lights[i].color[channel];
			output.direction_dot_range[channel] = lights[i].direction[channel];
			output.specular_position_radius[channel] =
				lights[i].has_specular_position ?
				lights[i].specular_position[channel] : lights[i].position[channel];
		}
		output.position_radius[3] = lights[i].radius;
		output.color_falloff[3] = lights[i].falloff;
		output.direction_dot_range[3] = lights[i].dot_range;
		output.specular_position_radius[3] = lights[i].has_specular_position ?
			std::max(lights[i].radius, lights[i].specular_radius) :
			lights[i].radius;
		output.specular_and_flags[0] = lights[i].specular_scalar > 0.0f ?
			lights[i].specular_scalar : 1.0f;
		output.specular_and_flags[1] = lights[i].directional ? 1.0f : 0.0f;
		output.specular_and_flags[2] = lights[i].headlight ? 1.0f : 0.0f;
		output.specular_and_flags[3] =
			lights[i].has_specular_position ? 1.0f : 0.0f;
	}
}

void VulkanRenderer::SetPerPixelSpecularMode(int mode)
{
	legacy_state_.specular_mode = static_cast<uint32_t>(ClampValue(mode, 0, 2));
}

void VulkanRenderer::SetPerPixelSpecularMap(int handle)
{
	texture_shadow_.units[2].logical_handle = handle;
	texture_shadow_.units[2].map_type = MAP_TYPE_BITMAP;
	legacy_state_.specular_version = kInvalidId;
}

void VulkanRenderer::SetZBias(float z_bias)
{
	if (!std::isfinite(z_bias))
	{
		Fail(RuntimeFailure::InvalidArgument, "SetZBias");
		return;
	}
	legacy_state_.z_bias = z_bias;
}

void VulkanRenderer::SetBumpmapReadyState(int state, int)
{
	if (state != 0)
		Fail(RuntimeFailure::UnsupportedFeature, "SetBumpmapReadyState");
}

void VulkanRenderer::SetGammaValue(float val)
{
	if (!(val > 0.0f) || !std::isfinite(val))
	{
		Fail(RuntimeFailure::InvalidArgument, "SetGammaValue");
		return;
	}
	preferred_.gamma = val;
	public_state_.gamma_value = val;
	legacy_state_.gamma = val;
}

void VulkanRenderer::GetStatistics(tRendererStats *stats)
{
	if (!stats)
	{
		Fail(RuntimeFailure::InvalidArgument, "GetStatistics");
		return;
	}
	*stats = last_stats_;
}

void VulkanRenderer::GetProjectionParameters(int *width, int *height)
{
	if (!width || !height)
	{
		Fail(RuntimeFailure::InvalidArgument, "GetProjectionParameters");
		return;
	}
	*width = public_state_.clip_x2 - public_state_.clip_x1;
	*height = public_state_.clip_y2 - public_state_.clip_y1;
}

void VulkanRenderer::GetProjectionScreenParameters(int &screenLX, int &screenTY,
	int &screenW, int &screenH)
{
	screenLX = public_state_.clip_x1;
	screenTY = public_state_.clip_y1;
	screenW = public_state_.clip_x2 - public_state_.clip_x1 + 1;
	screenH = public_state_.clip_y2 - public_state_.clip_y1 + 1;
}

float VulkanRenderer::GetAspectRatio()
{
	if (public_state_.screen_height <= 0)
		return 1.0f;
	return (3.0f * public_state_.screen_width) /
		(4.0f * public_state_.screen_height);
}

void VulkanRenderer::GetRenderState(rendering_state *rstate)
{
	if (!rstate)
	{
		Fail(RuntimeFailure::InvalidArgument, "GetRenderState");
		return;
	}
	*rstate = public_state_;
}

void VulkanRenderer::DLLGetRenderState(DLLrendering_state *rstate)
{
	if (!rstate)
	{
		Fail(RuntimeFailure::InvalidArgument, "DLLGetRenderState");
		return;
	}
	std::memset(rstate, 0, sizeof(*rstate));
#define COPY_RENDER_STATE_FIELD(field) rstate->field = public_state_.field
	COPY_RENDER_STATE_FIELD(initted);
	COPY_RENDER_STATE_FIELD(cur_bilinear_state);
	COPY_RENDER_STATE_FIELD(cur_zbuffer_state);
	COPY_RENDER_STATE_FIELD(cur_fog_state);
	COPY_RENDER_STATE_FIELD(cur_mip_state);
	COPY_RENDER_STATE_FIELD(cur_texture_type);
	COPY_RENDER_STATE_FIELD(cur_color_model);
	COPY_RENDER_STATE_FIELD(cur_light_state);
	COPY_RENDER_STATE_FIELD(cur_alpha_type);
	COPY_RENDER_STATE_FIELD(cur_wrap_type);
	COPY_RENDER_STATE_FIELD(cur_fog_start);
	COPY_RENDER_STATE_FIELD(cur_fog_end);
	COPY_RENDER_STATE_FIELD(cur_near_z);
	COPY_RENDER_STATE_FIELD(cur_far_z);
	COPY_RENDER_STATE_FIELD(gamma_value);
	COPY_RENDER_STATE_FIELD(cur_alpha);
	COPY_RENDER_STATE_FIELD(cur_color);
	COPY_RENDER_STATE_FIELD(cur_fog_color);
	COPY_RENDER_STATE_FIELD(cur_texture_quality);
	COPY_RENDER_STATE_FIELD(clip_x1);
	COPY_RENDER_STATE_FIELD(clip_x2);
	COPY_RENDER_STATE_FIELD(clip_y1);
	COPY_RENDER_STATE_FIELD(clip_y2);
	COPY_RENDER_STATE_FIELD(screen_width);
	COPY_RENDER_STATE_FIELD(screen_height);
#undef COPY_RENDER_STATE_FIELD
}

int VulkanRenderer::LowVidMem()
{
	if (!initialized_ || !runtime_)
	{
		Fail(initialized_ ? RuntimeFailure::MissingRuntime :
			RuntimeFailure::NotInitialized, "LowVidMem");
		return 0;
	}
	return ClampValue(runtime_->VideoMemoryPressure(), 0, 2);
}

int VulkanRenderer::SupportsBumpmapping()
{
	return 0;
}

void VulkanRenderer::PreUploadTextureToCard(int handle, int map_type)
{
	if (!initialized_ || !runtime_ || handle < 0 ||
		!runtime_->NotifyTextureEvent(TextureEvent::PreUpload, handle, map_type))
		Fail(!initialized_ ? RuntimeFailure::NotInitialized :
			RuntimeFailure::TextureEventFailed, "PreUploadTextureToCard");
}

void VulkanRenderer::FreePreUploadedTexture(int handle, int map_type)
{
	if (!initialized_ || !runtime_ || handle < 0 ||
		!runtime_->NotifyTextureEvent(TextureEvent::ReleasePreUpload,
			handle, map_type))
		Fail(!initialized_ ? RuntimeFailure::NotInitialized :
			RuntimeFailure::TextureEventFailed, "FreePreUploadedTexture");
}

void VulkanRenderer::ResetCache()
{
	if (!initialized_ || !runtime_ ||
		!runtime_->NotifyTextureEvent(TextureEvent::ResetCache, -1,
			MAP_TYPE_UNKNOWN))
	{
		Fail(!initialized_ ? RuntimeFailure::NotInitialized :
			RuntimeFailure::TextureEventFailed, "ResetCache");
		return;
	}
	ReleaseRetainedMeshCache();
	ClearBoundTextures();
}

bool VulkanRenderer::BeginTarget(RenderTargetClass target, int x1, int y1,
	int x2, int y2, int clear_flags, const char *operation)
{
	// PostPresent is normatively color-only; legacy callers commonly leave the
	// default RF_CLEAR_ZBUFFER bit set even though there is no depth attachment.
	if (target == RenderTargetClass::PostPresent)
		clear_flags &= ~RF_CLEAR_ZBUFFER;
	else if (target == RenderTargetClass::CockpitScene)
		clear_flags = RF_CLEAR_ZBUFFER | RF_CLEAR_COLOR;
	if (frame_interval_open_ || x2 <= x1 || y2 <= y1 ||
		(clear_flags & ~(RF_CLEAR_ZBUFFER | RF_CLEAR_COLOR)) != 0)
	{
		Fail(frame_interval_open_ ? RuntimeFailure::InvalidLifecycle :
			RuntimeFailure::InvalidArgument, operation);
		return false;
	}
	RenderCaptureSegment *capture = Capture(operation);
	if (!capture)
		return false;
	TargetRequest request = {};
	request.target = target;
	request.logical_clip = { x1, y1, x2 - x1, y2 - y1 };
	request.clear_flags = static_cast<uint32_t>(clear_flags);
	request.preferred = CapturedPreferred();
	TargetDescription description = {};
	if (!runtime_->DescribeTarget(request, &description) ||
		description.layout.target != target ||
		description.version.target != target ||
		description.layout.logical_width == 0 ||
		description.layout.logical_height == 0)
	{
		Fail(RuntimeFailure::TargetDescriptionFailed, operation);
		return false;
	}
	const TargetLayoutId layout = capture->InternTargetLayout(description.layout);
	if (layout == kInvalidId)
	{
		Fail(RuntimeFailure::ResourceExhausted, operation);
		return false;
	}
	description.version.target_layout = layout;
	const TargetVersionId version = capture->InternTargetVersion(description.version);
	const ViewportId viewport = capture->InternViewport(description.viewport);
	const ViewStateId view = capture->InternView(description.view);
	if (version == kInvalidId || viewport == kInvalidId || view == kInvalidId)
	{
		Fail(RuntimeFailure::ResourceExhausted, operation);
		return false;
	}

	CaptureCommand command = {};
	command.schema_version = kCaptureSchemaVersion;
	command.type = CaptureCommandType::BeginFrameTarget;
	command.payload.begin_frame_target.target = target;
	command.payload.begin_frame_target.logical_clip = request.logical_clip;
	command.payload.begin_frame_target.physical_viewport = viewport;
	command.payload.begin_frame_target.clear_flags =
		static_cast<uint32_t>(clear_flags);
	command.payload.begin_frame_target.view_state = view;
	command.payload.begin_frame_target.active_target_version = version;
	if (!Append(command, operation))
		return false;

	active_target_ = target;
	active_clip_ = request.logical_clip;
	active_layout_ = layout;
	active_target_version_ = version;
	active_view_ = view;
	active_version_record_ = description.version;
	legacy_state_.depth_epoch = description.version.depth_epoch;
	current_view_ = description.view;
	frame_interval_open_ = true;
	soft_depth_acquired_ = false;
	legacy_state_.viewport = viewport;
	legacy_state_.scissor = viewport;
	legacy_state_.depth_write_enabled = 1;
	legacy_state_.cockpit_scene_active =
		target == RenderTargetClass::CockpitScene ? 1u : 0u;
	legacy_state_.ao_suppression = 0.0f;
	legacy_state_.bloom_suppression = 0.0f;
	legacy_state_.ao_class = 0;
	legacy_state_.ao_weight = 1.0f;
	if (target == RenderTargetClass::Scene)
	{
		const LiteralMrtState mrt = DefaultSceneMrtState();
		legacy_state_.selected_draw_buffers = mrt.selected_draw_buffers;
		for (uint32_t i = 0; i < 5; ++i)
			legacy_state_.attachment_color_masks[i] = mrt.attachment_masks.rgba[i];
	}
	else
	{
		legacy_state_.selected_draw_buffers = kColorOnlySelectedDrawBuffers;
		for (uint32_t i = 0; i < 5; ++i)
			legacy_state_.attachment_color_masks[i] =
				i == 0 ? kChannelRgba : 0;
	}
	if ((clear_flags & (RF_CLEAR_COLOR | RF_CLEAR_ZBUFFER)) != 0)
		if (!AdvanceTargetVersion((clear_flags & RF_CLEAR_COLOR) != 0,
			(clear_flags & RF_CLEAR_ZBUFFER) != 0, operation))
			return false;
	public_state_.clip_x1 = x1;
	public_state_.clip_y1 = y1;
	public_state_.clip_x2 = x2;
	public_state_.clip_y2 = y2;
	return true;
}

void VulkanRenderer::StartFrame(int x1, int y1, int x2, int y2,
	int clear_flags)
{
	RenderTargetClass target = RenderTargetClass::Scene;
	if (cockpit_open_)
		target = RenderTargetClass::CockpitScene;
	else if (post_present_pending_)
		target = RenderTargetClass::PostPresent;
	BeginTarget(target, x1, y1, x2, y2, clear_flags, "StartFrame");
}

void VulkanRenderer::EndFrame()
{
	if (!frame_interval_open_)
	{
		Fail(RuntimeFailure::InvalidLifecycle, "EndFrame");
		return;
	}
	FlushTextLayer();
	CaptureCommand command = {};
	command.schema_version = kCaptureSchemaVersion;
	command.type = CaptureCommandType::EndFrame;
	command.payload.end_frame.view_interval_serial = ++view_interval_serial_;
	if (Append(command, "EndFrame"))
		frame_interval_open_ = false;
}

void VulkanRenderer::CaptureBloomSource()
{
	if (!frame_interval_open_ || active_target_ != RenderTargetClass::Scene)
	{
		Fail(RuntimeFailure::InvalidLifecycle, "CaptureBloomSource");
		return;
	}
	FlushTextLayer();
	CaptureCommand command = {};
	command.schema_version = kCaptureSchemaVersion;
	command.type = CaptureCommandType::CaptureBloomSource;
	command.payload.capture_bloom_source.scene_target_version =
		active_target_version_;
	command.payload.capture_bloom_source.projection = active_view_;
	command.payload.capture_bloom_source.view_projection = active_view_;
	command.payload.capture_bloom_source.inverse_modelview = active_view_;
	command.payload.capture_bloom_source.visible_rect = active_clip_;
	if (Append(command, "CaptureBloomSource"))
	{
		MotionCaptureLockTransitionInput input = {};
		input.currently_locked = legacy_state_.motion_capture_locked;
		input.event = MotionCaptureLockEvent::CaptureWorldForLatePost;
		input.pixel_target_enabled =
			(preferred_.motion_vector_mode == RENDERER_MOTION_VECTOR_PIXEL ||
			 preferred_.motion_vector_debug_preview ||
			 preferred_.pixel_motion_blur_strength > 0.0f ||
			 preferred_.pixel_motion_blur_legacy_object_strength > 0.0f);
		input.frozen = frame_dynamic_.histories_frozen ? 1u : 0u;
		legacy_state_.motion_capture_locked =
			EvaluateMotionCaptureLockTransition(input).resulting_locked;
		AdvanceTargetVersion(true, false, "CaptureBloomSource");
	}
}

void VulkanRenderer::PerfGpuSceneMark(renderer_gpu_scene_mark mark)
{
	if (mark < RENDERER_GPU_SCENE_AFTER_MAIN_WORLD ||
		mark > RENDERER_GPU_SCENE_AFTER_UI)
	{
		Fail(RuntimeFailure::InvalidArgument, "PerfGpuSceneMark");
		return;
	}
	CaptureCommand command = {};
	command.schema_version = kCaptureSchemaVersion;
	command.type = CaptureCommandType::PerfMarker;
	command.payload.perf_marker.gpu_scene_mark = static_cast<uint32_t>(mark);
	command.payload.perf_marker.nesting_serial = ++perf_marker_serial_;
	Append(command, "PerfGpuSceneMark");
}

bool VulkanRenderer::DescribeAndInternPresentation(bool defer_bloom)
{
	RenderCaptureSegment *capture = Capture("DescribePresentation");
	if (!capture)
		return false;
	PresentationDescription description = {};
	if (!runtime_->DescribePresentation(CapturedPreferred(), &description) ||
		description.defer_bloom > 1)
	{
		Fail(RuntimeFailure::PresentationDescriptionFailed,
			"DescribePresentation");
		return false;
	}
	description.dynamic.paused = frame_dynamic_.paused ? 1u : 0u;
	description.dynamic.histories_frozen =
		frame_dynamic_.histories_frozen ? 1u : 0u;
	description.dynamic.frame_time = frame_dynamic_.frame_time;
	description.dynamic.afterburner_scalar = frame_dynamic_.afterburner_scalar;
	if (defer_bloom)
	{
		description.defer_bloom = 1;
		description.dynamic.defer_bloom = 1;
		description.dynamic.cockpit_deferral_active = 1;
	}
	const TargetLayoutId scene =
		capture->InternTargetLayout(description.scene_layout);
	const TargetLayoutId post =
		capture->InternTargetLayout(description.post_present_layout);
	const TargetLayoutId cockpit =
		capture->InternTargetLayout(description.cockpit_scene_layout);
	CapturedTargetSignature signature = {};
	signature.target_layout = scene;
	signature.post_present_layout = post;
	signature.cockpit_scene_layout = cockpit;
	signature.preferred = CapturedPreferred();
	signature.dynamic = description.dynamic;
	const RenderTargetSignatureId signature_id =
		capture->InternTargetSignature(signature);
	const WsiSignatureId wsi = capture->InternWsiSignature(description.wsi);
	const PresentRectId rect = capture->InternPresentRect(description.present_rect);
	if (scene == kInvalidId || post == kInvalidId || cockpit == kInvalidId ||
		signature_id == kInvalidId || wsi == kInvalidId || rect == kInvalidId)
	{
		Fail(RuntimeFailure::ResourceExhausted, "DescribePresentation");
		return false;
	}
	presentation_ = description;
	presentation_signature_ = signature_id;
	presentation_wsi_ = wsi;
	presentation_rect_ = rect;
	return true;
}

bool VulkanRenderer::BeginPostPresentFrame()
{
	return BeginPostPresentFrameInternal(false);
}

bool VulkanRenderer::BeginPostPresentFrameInternal(bool defer_bloom)
{
	if (post_present_pending_)
		return !defer_bloom || presentation_.defer_bloom != 0;
	if (frame_interval_open_ || cockpit_open_)
	{
		Fail(RuntimeFailure::InvalidLifecycle, "BeginPostPresentFrame");
		return false;
	}
	if (!DescribeAndInternPresentation(defer_bloom))
		return false;
	CaptureCommand command = {};
	command.schema_version = kCaptureSchemaVersion;
	command.type = CaptureCommandType::BeginPostPresent;
	command.payload.begin_post_present.defer_bloom = presentation_.defer_bloom;
	command.payload.begin_post_present.signature = presentation_signature_;
	if (!Append(command, "BeginPostPresentFrame"))
		return false;
	post_present_pending_ = true;
	return true;
}

bool VulkanRenderer::IsPostPresentFramePending() const
{
	return post_present_pending_;
}

void VulkanRenderer::StartPostPresentFrame(int x1, int y1, int x2, int y2,
	int clear_flags)
{
	if (!post_present_pending_ && !BeginPostPresentFrameInternal(false))
		return;
	BeginTarget(RenderTargetClass::PostPresent, x1, y1, x2, y2,
		clear_flags, "StartPostPresentFrame");
}

void VulkanRenderer::EndPostPresentFrame()
{
	if (!post_present_pending_ || cockpit_open_ ||
		(frame_interval_open_ && active_target_ != RenderTargetClass::PostPresent))
	{
		Fail(RuntimeFailure::InvalidLifecycle, "EndPostPresentFrame");
		return;
	}
	// A post-present draw interval is optional (for example, no cockpit or
	// message-console elements were queued). Match GL4: close it when present,
	// then perform the actual swap/present and advance to the next framebuffer.
	if (frame_interval_open_)
	{
		EndFrame();
		if (frame_interval_open_)
			return;
	}
	Flip();
}

bool VulkanRenderer::BeginCockpitFrame()
{
	// The legacy HUD enters its cockpit pass immediately after ending the
	// primary scene. GL's BeginCockpitFrame also opens the deferred post-present
	// interval here; callers are not required to do that separately.
	if (!post_present_pending_ && !BeginPostPresentFrameInternal(true))
		return false;
	if (frame_interval_open_ || cockpit_open_)
	{
		Fail(RuntimeFailure::InvalidLifecycle, "BeginCockpitFrame");
		return false;
	}
	RenderCaptureSegment *capture = Capture("BeginCockpitFrame");
	if (!capture)
		return false;
	const PayloadDataId backing = capture->CopyPayloadData(&cockpit_backing_,
		sizeof(cockpit_backing_), 4, kPayloadCockpitBacking);
	if (backing == kInvalidId)
	{
		Fail(RuntimeFailure::ResourceExhausted, "BeginCockpitFrame");
		return false;
	}
	if (++cockpit_capture_serial_ == 0)
		++cockpit_capture_serial_;
	CaptureCommand command = {};
	command.schema_version = kCaptureSchemaVersion;
	command.type = CaptureCommandType::BeginCockpitScene;
	command.payload.begin_cockpit_scene.logical_rect =
		{ 0, 0, preferred_.width, preferred_.height };
	command.payload.begin_cockpit_scene.backing_effect_state = backing;
	command.payload.begin_cockpit_scene.capture_serial = cockpit_capture_serial_;
	if (!Append(command, "BeginCockpitFrame"))
		return false;
	cockpit_open_ = true;
	return true;
}

void VulkanRenderer::EndCockpitFrame()
{
	if (!cockpit_open_ || frame_interval_open_)
	{
		Fail(RuntimeFailure::InvalidLifecycle, "EndCockpitFrame");
		return;
	}
	CaptureCommand command = {};
	command.schema_version = kCaptureSchemaVersion;
	command.type = CaptureCommandType::EndCockpitScene;
	command.payload.end_cockpit_scene.capture_serial = cockpit_capture_serial_;
	if (Append(command, "EndCockpitFrame"))
		cockpit_open_ = false;
}

void VulkanRenderer::ResetPerPresentedFrameState()
{
	// A caller must explicitly supply live values for every game frame.  This
	// prevents an afterburner/pause snapshot from leaking into menu or movie
	// frames that do not pass through GameRenderFrame.
	std::memset(&frame_dynamic_, 0, sizeof(frame_dynamic_));
	std::memcpy(legacy_state_.previous_view, legacy_state_.current_view,
		sizeof(legacy_state_.previous_view));
	std::memcpy(legacy_state_.previous_object, legacy_state_.current_object,
		sizeof(legacy_state_.previous_object));
	std::memcpy(current_view_.previous_view_projection,
		current_view_.view_projection,
		sizeof(current_view_.previous_view_projection));
	last_stats_ = current_stats_;
	std::memset(&current_stats_, 0, sizeof(current_stats_));
	frame_interval_open_ = false;
	post_present_pending_ = false;
	cockpit_open_ = false;
	font_glyphs_pending_ = false;
	soft_depth_acquired_ = false;
	MotionCaptureLockTransitionInput lock = {};
	lock.currently_locked = legacy_state_.motion_capture_locked;
	lock.event = MotionCaptureLockEvent::PresentNextFramebuffer;
	legacy_state_.motion_capture_locked =
		EvaluateMotionCaptureLockTransition(lock).resulting_locked;
	presentation_signature_ = presentation_wsi_ = presentation_rect_ = kInvalidId;
	active_layout_ = active_target_version_ = active_view_ = kInvalidId;
	++presented_frame_serial_;
}

void VulkanRenderer::Flip()
{
	if (frame_interval_open_ || cockpit_open_)
	{
		Fail(RuntimeFailure::InvalidLifecycle, "Flip");
		return;
	}
	if (!post_present_pending_ && !BeginPostPresentFrame())
		return;
	CaptureCommand command = {};
	command.schema_version = kCaptureSchemaVersion;
	command.type = CaptureCommandType::Present;
	command.payload.present.presented_frame_serial = presented_frame_serial_;
	command.payload.present.window_swapchain_signature = presentation_wsi_;
	command.payload.present.present_rect = presentation_rect_;
	if (!Append(command, "Flip"))
		return;
	if (!runtime_->SubmitPresentedFrame(presented_frame_serial_))
	{
		Fail(RuntimeFailure::PresentationFailed, "Flip");
		if (runtime_->DiscardFailedPresentedFrame(presented_frame_serial_))
		{
			ResetPerPresentedFrameState();
			RenderCaptureSegment *next = runtime_->ActiveCapture();
			if (next && next->GetLifecycle() ==
				RenderCaptureSegment::Lifecycle::Capturing)
				presented_frame_serial_ = next->PresentedFrameSerial();
		}
		return;
	}
	ResetPerPresentedFrameState();
	RenderCaptureSegment *next = runtime_->ActiveCapture();
	if (next && next->GetLifecycle() == RenderCaptureSegment::Lifecycle::Capturing)
		presented_frame_serial_ = next->PresentedFrameSerial();
}

bool VulkanRenderer::AppendColorClear(const LogicalRect &rect, bool whole_target,
	ddgr_color color, float alpha, uint32_t selected_attachments,
	const char *operation)
{
	if (!frame_interval_open_ || !std::isfinite(alpha))
	{
		Fail(frame_interval_open_ ? RuntimeFailure::InvalidArgument :
			RuntimeFailure::InvalidLifecycle, operation);
		return false;
	}
	CaptureCommand command = {};
	command.schema_version = kCaptureSchemaVersion;
	command.type = CaptureCommandType::ClearColor;
	ClearColorCommand &clear = command.payload.clear_color;
	clear.target = active_target_;
	clear.rect = rect;
	clear.whole_target = whole_target ? 1u : 0u;
	ColorFloats(color, clear.rgba, ClampUnit(alpha));
	clear.selected_attachments = selected_attachments;
	const uint32_t bits[5] = { kWriteColor, kWriteVelocity,
		kWriteProtectionMask, kWriteAoClass, kWriteObjectId };
	const uint32_t legal_channels[5] = { kChannelRgba,
		kChannelRed | kChannelGreen, kChannelRed | kChannelGreen,
		kChannelRed, kChannelRed };
	for (uint32_t i = 0; i < 5; ++i)
		clear.attachment_channel_masks[i] =
			(selected_attachments & bits[i]) ? legal_channels[i] : 0;
	if (!Append(command, operation))
		return false;
	if (selected_attachments != 0)
		if (!AdvanceTargetVersion((selected_attachments & kWriteColor) != 0,
			false, operation))
			return false;
	return true;
}

bool VulkanRenderer::AdvanceTargetVersion(bool color_written,
	bool depth_written, const char *operation)
{
	// PostPresent is normatively color-only even when legacy Z-write state is
	// still enabled. Never manufacture a depth epoch for that target.
	if (active_target_ == RenderTargetClass::PostPresent)
		depth_written = false;
	if (!color_written && !depth_written)
		return true;
	RenderCaptureSegment *capture = Capture(operation);
	if (!capture || active_layout_ == kInvalidId)
		return false;
	if (++active_version_record_.version == 0)
		++active_version_record_.version;
	if (color_written && ++active_version_record_.color_epoch == 0)
		++active_version_record_.color_epoch;
	if (depth_written && ++active_version_record_.depth_epoch == 0)
		++active_version_record_.depth_epoch;
	active_version_record_.target_layout = active_layout_;
	const TargetVersionId version =
		capture->InternTargetVersion(active_version_record_);
	if (version == kInvalidId)
	{
		Fail(RuntimeFailure::ResourceExhausted, operation);
		return false;
	}
	active_target_version_ = version;
	legacy_state_.depth_epoch = active_version_record_.depth_epoch;
	if (depth_written)
		soft_depth_acquired_ = false;
	return true;
}

bool VulkanRenderer::AppendDepthClear(const LogicalRect &rect, bool whole_target,
	const char *operation)
{
	if (!frame_interval_open_)
	{
		Fail(RuntimeFailure::InvalidLifecycle, operation);
		return false;
	}
	if (active_target_ == RenderTargetClass::PostPresent)
		return true; // Normative color-only target: no physical depth operation.
	CaptureCommand command = {};
	command.schema_version = kCaptureSchemaVersion;
	command.type = CaptureCommandType::ClearDepth;
	command.payload.clear_depth.target = active_target_;
	command.payload.clear_depth.rect = rect;
	command.payload.clear_depth.whole_target = whole_target ? 1u : 0u;
	command.payload.clear_depth.depth = 1.0f;
	if (!Append(command, operation))
		return false;
	return AdvanceTargetVersion(false, true, operation);
}

void VulkanRenderer::ClearScreen(ddgr_color color)
{
	FlushTextLayer();
	if (AppendColorClear(active_clip_, true, color, 0.0f, kWriteColor,
		"ClearScreen"))
		AppendDepthClear(active_clip_, true, "ClearScreen");
}

void VulkanRenderer::ClearZBuffer()
{
	FlushTextLayer();
	AppendDepthClear(active_clip_, true, "ClearZBuffer");
}

uint32_t VulkanRenderer::SamplerIndex(bool array_texture) const
{
	const uint32_t base = (legacy_state_.wrap_type % 3u) * 4u +
		(legacy_state_.mipping && preferred_.mipping ? 2u : 0u) +
		(legacy_state_.filtering && preferred_.filtering ? 1u : 0u);
	return base + (array_texture ? 16u : 0u);
}

bool VulkanRenderer::ResolveTexture(const TextureRequest &request,
	ResolvedTexture *resolved, const char *operation)
{
	RenderCaptureSegment *capture = Capture(operation);
	const size_t version_count = capture ? capture->TextureVersions().size() : 0;
	if (!capture || !resolved || !runtime_ ||
		!runtime_->ResolveTexture(request, resolved) ||
		resolved->version.id == kInvalidId || resolved->version.width == 0 ||
		resolved->version.height == 0 || resolved->version.depth_or_layers == 0 ||
		resolved->version.mip_count == 0)
	{
		Fail(RuntimeFailure::TextureResolutionFailed, operation);
		return false;
	}
	// TextureManager::Resolve owns capture registration. Registering the same
	// immutable version here repeated a linear lookup for every material slot.
	if (capture->TextureVersions().size() != version_count &&
		resolved->version.immutable_upload_payload != kInvalidId)
		++current_stats_.texture_uploads;
	return true;
}

bool VulkanRenderer::BuildMaterial(int handle, int map_type,
	CapturedMaterial *material)
{
	if (!material)
		return false;
	std::memset(material, 0, sizeof(*material));
	ResolvedTexture resolved[8] = {};
	TextureRequest requests[8] = {};
	const uint32_t effective_filtering =
		legacy_state_.filtering && preferred_.filtering;
	const uint32_t effective_mipping =
		legacy_state_.mipping && preferred_.mipping;
	for (uint32_t i = 0; i < 8; ++i)
	{
		requests[i].logical_handle = -1;
		requests[i].map_type = MAP_TYPE_UNKNOWN;
		requests[i].role = static_cast<TextureRole>(i);
		requests[i].wrap_type = legacy_state_.wrap_type;
		requests[i].filtering = effective_filtering;
		requests[i].mipping = effective_mipping;
	}
	requests[0].logical_handle =
		legacy_state_.texture_type == TT_FLAT ? -1 : handle;
	requests[0].map_type = static_cast<uint32_t>(map_type);
	if (legacy_state_.overlay_type != OT_NONE && legacy_state_.overlay_handle >= 0)
	{
		requests[1].logical_handle = legacy_state_.overlay_handle;
		requests[1].map_type = MAP_TYPE_LIGHTMAP;
	}
	if (legacy_state_.specular_mode == 2 &&
		texture_shadow_.units[2].logical_handle >= 0)
	{
		requests[2].logical_handle = texture_shadow_.units[2].logical_handle;
		requests[2].map_type = MAP_TYPE_BITMAP;
	}
	for (uint32_t i = 0; i < 8; ++i)
		if (!ResolveTexture(requests[i], &resolved[i], "BuildMaterial"))
			return false;
	for (uint32_t i = 0; i < 4; ++i)
	{
		material->image2d[i] = resolved[i].version.id;
		material->image2d_array[i] = resolved[i + 4].version.id;
		material->sampler[i] = resolved[i].sampler_index;
	}
	if (resolved[7].version.id != kInvalidId)
		material->sampler[3] = resolved[7].sampler_index;
	material->uv_params[0] = resolved[1].uv_scale[0] == 0.0f ?
		1.0f : resolved[1].uv_scale[0];
	material->uv_params[1] = resolved[1].uv_scale[1] == 0.0f ?
		1.0f : resolved[1].uv_scale[1];
	// The fixed named-pipeline IDs are part of the Vulkan shader contract.
	material->uv_params[2] = static_cast<float>(selected_pipeline_);
	material->uv_params[3] = legacy_state_.soft_particle_enabled ? 2.0f : 0.0f;
	if (handle >= 0)
	{
		legacy_state_.bitmap_handle = handle;
		legacy_state_.bitmap_version = resolved[0].version.id;
		legacy_state_.map_type = static_cast<uint32_t>(map_type);
		texture_shadow_.units[0].logical_handle = handle;
		texture_shadow_.units[0].map_type = static_cast<uint32_t>(map_type);
	}
	if (legacy_state_.overlay_type != OT_NONE && legacy_state_.overlay_handle >= 0)
		legacy_state_.overlay_version = resolved[1].version.id;
	if (legacy_state_.specular_mode == 2 &&
		texture_shadow_.units[2].logical_handle >= 0)
		legacy_state_.specular_version = resolved[2].version.id;
	for (uint32_t i = 0; i < 4; ++i)
		texture_shadow_.units[i].sampler_index = material->sampler[i];
	if (handle >= 0)
		texture_shadow_.units[0].version = resolved[0].version.id;
	if (legacy_state_.overlay_type != OT_NONE && legacy_state_.overlay_handle >= 0)
		texture_shadow_.units[1].version = resolved[1].version.id;
	if (legacy_state_.specular_mode == 2 &&
		texture_shadow_.units[2].logical_handle >= 0)
		texture_shadow_.units[2].version = resolved[2].version.id;
	return true;
}

float VulkanRenderer::AOWeight(uint32_t ao_class) const
{
	switch (ao_class)
	{
	case RENDERER_AO_CLASS_TERRAIN:
		return ClampUnit(preferred_.gtao_terrain_occlusion);
	case RENDERER_AO_CLASS_POLYOBJECT:
		return ClampUnit(preferred_.gtao_polyobject_occlusion);
	case RENDERER_AO_CLASS_MINE_ROCK:
		return ClampUnit(preferred_.gtao_mine_rock_occlusion);
	case RENDERER_AO_CLASS_MINE:
		return ClampUnit(preferred_.gtao_mine_occlusion);
	default:
		return 1.0f;
	}
}

bool VulkanRenderer::BuildDrawState(PrimitiveSourceKind source,
	uint32_t category, uint32_t category_3d,
	CapturedShaderRasterState *output, MrtDrawRoutingDecision *routing)
{
	if (!output || !routing || active_layout_ == kInvalidId ||
		active_version_record_.samples == 0)
		return false;
	MotionWritePredicateInput motion = {};
	motion.suppression_depth = legacy_state_.motion_suppression_depth;
	motion.pixel_target_enabled =
		(preferred_.motion_vector_mode == RENDERER_MOTION_VECTOR_PIXEL ||
		 preferred_.motion_vector_debug_preview ||
		 preferred_.pixel_motion_blur_strength > 0.0f ||
		 preferred_.pixel_motion_blur_legacy_object_strength > 0.0f) ? 1u : 0u;
	motion.frozen = frame_dynamic_.histories_frozen ? 1u : 0u;
	motion.post_present_pending =
		active_target_ == RenderTargetClass::PostPresent ? 1u : 0u;
	motion.zbuffer_enabled = legacy_state_.depth_test_enabled;
	motion.motion_object_active = legacy_state_.motion_object_active;
	motion.capture_locked = legacy_state_.motion_capture_locked;
	motion.cockpit_draw = (active_target_ == RenderTargetClass::CockpitScene ||
		category_3d == RENDERER_DRAW_CALL_3D_COCKPIT) ? 1u : 0u;
	motion.ao_class = legacy_state_.ao_class;
	motion.alpha_type = legacy_state_.alpha_type;
	motion.alpha_value = legacy_state_.alpha_value;
	motion.force_capture =
		(legacy_state_.motion_flags & kMotionObjectFlagForceCapture) != 0;
	motion.texture_type = legacy_state_.texture_type;
	motion.motion_object_id = legacy_state_.motion_object_id;
	motion.cockpit_motion_scope_active =
		motion.cockpit_draw && legacy_state_.motion_object_active;
	const MotionWritePredicateDecision motion_decision =
		EvaluateMotionWritePredicate(motion);

	MrtDrawRoutingInput mrt = {};
	mrt.target = active_target_;
	mrt.draw_kind = MrtDrawKind::Polygon;
	mrt.literal_state.selected_draw_buffers = legacy_state_.selected_draw_buffers;
	for (uint32_t i = 0; i < 5; ++i)
		mrt.literal_state.attachment_masks.rgba[i] =
			legacy_state_.attachment_color_masks[i];
	mrt.drawing_to_scene_framebuffer =
		active_target_ == RenderTargetClass::Scene ? 1u : 0u;
	mrt.cockpit_scene_frame_active =
		active_target_ == RenderTargetClass::CockpitScene ? 1u : 0u;
	mrt.pixel_motion_mode_enabled = motion.pixel_target_enabled;
	mrt.zbuffer_enabled = legacy_state_.depth_test_enabled;
	mrt.capture_locked = legacy_state_.motion_capture_locked;
	mrt.cockpit_draw = motion.cockpit_draw;
	mrt.motion_write = motion_decision.motion_write;
	mrt.object_id_write = motion_decision.object_id_write;
	*routing = EvaluateMrtDrawRouting(mrt);
	if (!routing->legal_write_mask)
	{
		Fail(RuntimeFailure::CaptureRejected, "BuildDrawState.MRT");
		return false;
	}

	std::memset(output, 0, sizeof(*output));
	CapturedShaderState &shader = output->shader;
	const bool textured = legacy_state_.texture_type != TT_FLAT;
	const bool specular = preferred_.per_pixel_lighting &&
		(legacy_state_.alpha_type == AT_SPECULAR ||
		 legacy_state_.specular_mode != 0);
	if (textured) shader.shader_flags |= kShaderTextured;
	if (legacy_state_.overlay_type != OT_NONE) shader.shader_flags |= kShaderLightmapped;
	if (legacy_state_.fog_enabled) shader.shader_flags |= kShaderGenericFog;
	if (legacy_state_.lighting_state == LS_PHONG) shader.shader_flags |= kShaderPhong;
	if (dynamic_light_count_ != 0) shader.shader_flags |= kShaderDynamicLights;
	if (specular) shader.shader_flags |= kShaderPerPixelSpecular;
	if (specular && legacy_state_.specular_mode == 2)
		shader.shader_flags |= kShaderSpecularMask;
	if (specular && have_specular_block_ && specular_block_.field_mode != 0.0f)
		shader.shader_flags |= kShaderFieldSpecular;
	if (legacy_state_.soft_particle_enabled) shader.shader_flags |= kShaderSoftParticle;
	if (legacy_state_.luminance_mask) shader.shader_flags |= kShaderLuminancePostMask;
	if (legacy_state_.post_mask_only) shader.shader_flags |= kShaderPostMaskOnly;
	if (motion_decision.motion_write) shader.shader_flags |= kShaderMotionWrite;
	if (motion_decision.object_id_write) shader.shader_flags |= kShaderObjectIdWrite;
	if (motion.cockpit_draw) shader.shader_flags |= kShaderCockpit;
	if (legacy_state_.texture_type == TT_LINEAR_SPECIAL ||
		legacy_state_.texture_type == TT_PERSPECTIVE_SPECIAL)
		shader.shader_flags |= kShaderSpecialTextureFlatColor;
	shader.texture_type = legacy_state_.texture_type;
	shader.overlay_type = legacy_state_.overlay_type;
	shader.lighting_color_model = (legacy_state_.lighting_state & 0xffffu) |
		((legacy_state_.color_model & 0xffffu) << 16);
	shader.alpha_type = legacy_state_.alpha_type;
	shader.alpha_value = legacy_state_.alpha_value;
	shader.blend_class = legacy_state_.blend_class;
	shader.draw_classification = (category & 0xffffu) |
		((category_3d & 0xffffu) << 16);
	shader.alpha_factor = legacy_state_.alpha_factor;
	shader.z_bias = legacy_state_.z_bias;
	shader.fog_near_mapped = legacy_state_.fog_near_mapped;
	shader.fog_far_mapped = legacy_state_.fog_far_mapped;
	ColorFloats(legacy_state_.flat_color, shader.flat_color);
	ColorFloats(legacy_state_.fog_color, shader.fog_color);
	std::memcpy(shader.light_direction, legacy_state_.per_pixel_direction,
		sizeof(shader.light_direction));
	shader.post_values[0] = legacy_state_.ao_suppression;
	shader.post_values[1] = legacy_state_.bloom_suppression;
	shader.post_values[2] = legacy_state_.ao_weight;
	shader.post_values[3] = 0.0f;
	shader.dynamic_light_count = dynamic_light_count_;
	shader.motion_object_id = motion_decision.object_id_write ?
		legacy_state_.motion_object_id : 0;
	shader.motion_flags = legacy_state_.motion_flags;
	shader.ao_class = legacy_state_.ao_class;
	shader.state_flags2 = legacy_state_.wrap_type & kStateWrapMask;
	if (legacy_state_.filtering && preferred_.filtering)
		shader.state_flags2 |= kStateFiltering;
	if (legacy_state_.mipping && preferred_.mipping)
		shader.state_flags2 |= kStateMipping;
	if (legacy_state_.lighting_state == LS_PHONG)
		shader.state_flags2 |= 1u << 4;
	else if (dynamic_light_count_ != 0)
		shader.state_flags2 |= 2u << 4;
	if ((shader.shader_flags & kShaderFieldSpecular) != 0)
		shader.state_flags2 |= kStateFieldMode;

	output->target_layout = active_layout_;
	output->sample_count = active_version_record_.samples;
	output->mrt_write_mask = routing->logical_write_mask;
	output->raster_family = RasterFamily::Ordinary;
	output->cull_enabled = legacy_state_.cull_enabled;
	output->front_face = legacy_state_.front_face;
	output->depth_test_enabled = active_target_ == RenderTargetClass::PostPresent ?
		0u : legacy_state_.depth_test_enabled;
	output->depth_write_enabled = active_target_ == RenderTargetClass::PostPresent ?
		0u : legacy_state_.depth_write_enabled;
	output->depth_compare = legacy_state_.depth_compare;
	output->depth_bias_enabled = legacy_state_.coplanar_enabled;
	output->depth_bias_factor = legacy_state_.coplanar_factor;
	output->depth_bias_units = legacy_state_.coplanar_units;
	output->viewport = legacy_state_.viewport;
	output->scissor = legacy_state_.scissor;
	legacy_state_.draw_classification =
		{ category, category_3d, source, 0 };
	return true;
}

bool VulkanRenderer::BuildPayload(g3Point *const *points,
	uint32_t vertex_count, const CapturedShaderRasterState &state,
	PayloadRef *payload)
{
	if (!payload)
		return false;
	*payload = kInvalidId;
	RenderCaptureSegment *capture = Capture("BuildPayload");
	if (!capture)
		return false;
	CapturedPayloadBinding binding = EmptyPayloadBinding();
	if (points)
	{
		scratch_perspective_.assign(vertex_count, PerspectiveVertexPayload{});
		for (uint32_t i = 0; i < vertex_count; ++i)
		{
			const g3Point &point = *points[i];
			const float denominator = point.p3_z + legacy_state_.z_bias;
			const float q = denominator != 0.0f ? 1.0f / denominator : 0.0f;
			const bool textured = legacy_state_.texture_type != TT_FLAT;
			const bool overlay = legacy_state_.overlay_type != OT_NONE;
			scratch_perspective_[i].value_q[0] = textured ? point.p3_u * q : 0.0f;
			scratch_perspective_[i].value_q[1] = textured ? point.p3_v * q : 0.0f;
			scratch_perspective_[i].value_q[2] = overlay ? point.p3_u2 * q : 0.0f;
			scratch_perspective_[i].value_q[3] = q;
		}
		binding.perspective_vertices = capture->CopyPayloadData(
			scratch_perspective_.data(),
			vertex_count * sizeof(PerspectiveVertexPayload), 16,
			kPayloadPerspectiveVertices);
		if (binding.perspective_vertices == kInvalidId)
			return false;
		binding.validity_flags |= kPayloadHasPerspectiveVertices;
	}

	const uint32_t primary_payload =
		(state.shader.state_flags2 & kStatePrimaryPayloadMask) >> 4;
	if (primary_payload != 0)
	{
		scratch_specular_.assign(vertex_count, SpecularVertexPayload{});
		for (uint32_t i = 0; i < vertex_count; ++i)
		{
			if (!points)
				continue;
			const g3Point &point = *points[i];
			SpecularVertexPayload &output = scratch_specular_[i];
			const float denominator = point.p3_z + legacy_state_.z_bias;
			const float q = denominator != 0.0f ? 1.0f / denominator : 0.0f;
			const vector &normal = point.p3_specular_normal_valid ?
				point.p3_specular_normal : point.p3_vecPreRot;
			if (!std::isfinite(normal.x) || !std::isfinite(normal.y) ||
				!std::isfinite(normal.z))
			{
				Fail(RuntimeFailure::InvalidArgument, "BuildPayload.Specular");
				return false;
			}
			output.normal_or_position_q[0] = normal.x * q;
			output.normal_or_position_q[1] = normal.y * q;
			output.normal_or_position_q[2] = normal.z * q;
			output.normal_or_position_q[3] = q;
			const uint32_t field_count = std::min<uint32_t>(
				point.p3_specular_field_count, kMaxSpecularSources);
			for (uint32_t field = 0; field < field_count; ++field)
			{
				if (!std::isfinite(point.p3_specular_field_centers[field].x) ||
					!std::isfinite(point.p3_specular_field_centers[field].y) ||
					!std::isfinite(point.p3_specular_field_centers[field].z) ||
					!std::isfinite(point.p3_specular_field_colors[field].x) ||
					!std::isfinite(point.p3_specular_field_colors[field].y) ||
					!std::isfinite(point.p3_specular_field_colors[field].z))
				{
					Fail(RuntimeFailure::InvalidArgument, "BuildPayload.FieldSpecular");
					return false;
				}
				output.field_center_q[field][0] =
					point.p3_specular_field_centers[field].x * q;
				output.field_center_q[field][1] =
					point.p3_specular_field_centers[field].y * q;
				output.field_center_q[field][2] =
					point.p3_specular_field_centers[field].z * q;
				output.field_center_q[field][3] = q;
				output.field_color[field][0] =
					point.p3_specular_field_colors[field].x;
				output.field_color[field][1] =
					point.p3_specular_field_colors[field].y;
				output.field_color[field][2] =
					point.p3_specular_field_colors[field].z;
			}
		}
		binding.specular_vertices = capture->CopyPayloadData(
			scratch_specular_.data(),
			vertex_count * sizeof(SpecularVertexPayload), 16,
			kPayloadSpecularVertices);
		if (binding.specular_vertices == kInvalidId)
			return false;
		binding.validity_flags |= kPayloadHasSpecularVertices;
	}

	const bool specular_position_payload =
		(state.shader.shader_flags & kShaderPerPixelSpecular) != 0;
	const bool room_position_payload = current_room_ >= 0 &&
		static_cast<size_t>(current_room_) < room_aux_.size();
	if (((state.shader.shader_flags & kShaderMotionWrite) != 0 ||
		specular_position_payload || room_position_payload) && points)
	{
		bool have_motion_payload = false;
		scratch_motion_.assign(vertex_count, MotionVertexPayload{});
		for (uint32_t i = 0; i < vertex_count; ++i)
		{
			const g3Point &point = *points[i];
			if (specular_position_payload || room_position_payload)
			{
				const float denominator = point.p3_z + legacy_state_.z_bias;
				const float q = denominator != 0.0f ? 1.0f / denominator : 0.0f;
				vector view_position = point.p3_vecPreRot - View_position;
				view_position = view_position * Unscaled_matrix;
				if (!std::isfinite(view_position.x) ||
					!std::isfinite(view_position.y) ||
					!std::isfinite(view_position.z) || !std::isfinite(q))
				{
					Fail(RuntimeFailure::InvalidArgument,
						"BuildPayload.SpecularPosition");
					return false;
				}
				have_motion_payload = true;
				MotionVertexPayload &motion = scratch_motion_[i];
				motion.current_q[0] = view_position.x * q;
				motion.current_q[1] = view_position.y * q;
				motion.current_q[2] = view_position.z * q;
				motion.current_q[3] = q;
				continue;
			}
			if (!point.p3_motion_world_valid && !point.p3_motion_prev_world_valid)
				continue;
			if ((point.p3_motion_world_valid &&
				(!std::isfinite(point.p3_motion_world_pos.x) ||
				 !std::isfinite(point.p3_motion_world_pos.y) ||
				 !std::isfinite(point.p3_motion_world_pos.z))) ||
				(point.p3_motion_prev_world_valid &&
				(!std::isfinite(point.p3_motion_prev_world_pos.x) ||
				 !std::isfinite(point.p3_motion_prev_world_pos.y) ||
				 !std::isfinite(point.p3_motion_prev_world_pos.z))))
			{
				Fail(RuntimeFailure::InvalidArgument, "BuildPayload.Motion");
				return false;
			}
			have_motion_payload = true;
			MotionVertexPayload &motion = scratch_motion_[i];
			motion.current_q[0] = point.p3_motion_world_pos.x;
			motion.current_q[1] = point.p3_motion_world_pos.y;
			motion.current_q[2] = point.p3_motion_world_pos.z;
			motion.current_q[3] = point.p3_motion_world_valid ? 1.0f : 0.0f;
			motion.previous_q[0] = point.p3_motion_prev_world_pos.x;
			motion.previous_q[1] = point.p3_motion_prev_world_pos.y;
			motion.previous_q[2] = point.p3_motion_prev_world_pos.z;
			motion.previous_q[3] = point.p3_motion_prev_world_valid ? 1.0f : 0.0f;
		}
		if (have_motion_payload)
		{
			binding.motion_vertices = capture->CopyPayloadData(
				scratch_motion_.data(), vertex_count * sizeof(MotionVertexPayload),
				16, kPayloadMotionVertices);
			if (binding.motion_vertices == kInvalidId)
				return false;
			binding.validity_flags |= kPayloadHasMotionVertices;
		}
	}

	if (dynamic_light_count_ != 0)
	{
		binding.dynamic_lights = capture->CopyPayloadData(dynamic_lights_,
			dynamic_light_count_ * sizeof(GpuDynamicLight), 16,
			kPayloadDynamicLights);
		if (binding.dynamic_lights == kInvalidId)
			return false;
		binding.validity_flags |= kPayloadHasDynamicLights;
	}
	if ((state.shader.shader_flags &
		(kShaderPerPixelSpecular | kShaderFieldSpecular)) != 0)
	{
		GpuSpecularBlock block = have_specular_block_ ?
			specular_block_ : GpuSpecularBlock{};
		binding.specular_block = capture->CopyPayloadData(&block, sizeof(block),
			16, kPayloadSpecularBlock);
		if (binding.specular_block == kInvalidId)
			return false;
		binding.validity_flags |= kPayloadHasSpecularBlock;
	}
	if (current_room_ >= 0 && static_cast<size_t>(current_room_) < room_aux_.size())
	{
		binding.world_aux = capture->CopyPayloadData(&room_aux_[current_room_],
			sizeof(GpuWorldAux), 16, kPayloadWorldAux);
		if (binding.world_aux == kInvalidId)
			return false;
		binding.validity_flags |= kPayloadHasWorldAux;
	}
	if (binding.validity_flags == 0)
		return true;
	*payload = capture->InternPayloadBinding(binding);
	return *payload != kInvalidId;
}

bool VulkanRenderer::BuildPointVertex(const g3Point &point, BaseVertex *vertex)
{
	if (!vertex)
		return false;
	if (!std::isfinite(point.p3_sx) || !std::isfinite(point.p3_sy) ||
		!std::isfinite(point.p3_z) ||
		(legacy_state_.texture_type != TT_FLAT &&
		 (!std::isfinite(point.p3_u) || !std::isfinite(point.p3_v))) ||
		(legacy_state_.overlay_type != OT_NONE &&
		 (!std::isfinite(point.p3_u2) || !std::isfinite(point.p3_v2))))
		return false;
	std::memset(vertex, 0, sizeof(*vertex));
	vertex->position[0] = point.p3_sx;
	vertex->position[1] = point.p3_sy;
	vertex->position[2] = point.p3_z;
	if (legacy_state_.texture_type != TT_FLAT)
	{
		vertex->uv0[0] = point.p3_u;
		vertex->uv0[1] = point.p3_v;
	}
	if (legacy_state_.overlay_type != OT_NONE)
	{
		vertex->uv1[0] = point.p3_u2;
		vertex->uv1[1] = point.p3_v2;
	}
	uint32_t red = 255, green = 255, blue = 255;
	if (legacy_state_.lighting_state == LS_FLAT_GOURAUD ||
		legacy_state_.texture_type == TT_FLAT)
	{
		red = ColorRed(legacy_state_.flat_color);
		green = ColorGreen(legacy_state_.flat_color);
		blue = ColorBlue(legacy_state_.flat_color);
	}
	else if (legacy_state_.lighting_state != LS_NONE)
	{
		if (legacy_state_.color_model == CM_MONO)
		{
			if (!std::isfinite(point.p3_l))
				return false;
			red = green = blue = static_cast<uint32_t>(
				ClampUnit(point.p3_l) * 255.0f);
		}
		else
		{
			if (!std::isfinite(point.p3_r) || !std::isfinite(point.p3_g) ||
				!std::isfinite(point.p3_b))
				return false;
			red = static_cast<uint32_t>(ClampUnit(point.p3_r) * 255.0f);
			green = static_cast<uint32_t>(ClampUnit(point.p3_g) * 255.0f);
			blue = static_cast<uint32_t>(ClampUnit(point.p3_b) * 255.0f);
		}
	}
	const AlphaTypeTransitionContract *alpha_contract =
		FindAlphaTypeTransitionContract(legacy_state_.alpha_type);
	float alpha = alpha_contract && alpha_contract->multiplier_source ==
		AlphaMultiplierSource::Full255 ? 255.0f :
		static_cast<float>(legacy_state_.alpha_value);
	if ((legacy_state_.alpha_type & ATF_VERTEX) != 0)
	{
		if (!std::isfinite(point.p3_a))
			return false;
		alpha *= point.p3_a;
	}
	alpha *= legacy_state_.alpha_factor;
	const uint32_t alpha8 = static_cast<uint32_t>(
		ClampValue(alpha, 0.0f, 255.0f));
	vertex->rgba8 = PackRgba(red, green, blue, alpha8);

	primitive_scratch_.position[0] = point.p3_sx;
	primitive_scratch_.position[1] = point.p3_sy;
	primitive_scratch_.position[2] = point.p3_z;
	primitive_scratch_.rgba8 = vertex->rgba8;
	if (legacy_state_.texture_type != TT_FLAT)
	{
		primitive_scratch_.tex_coord[0] = point.p3_u;
		primitive_scratch_.tex_coord[1] = point.p3_v;
	}
	if (legacy_state_.overlay_type != OT_NONE)
	{
		primitive_scratch_.tex_coord2[0] = point.p3_u2;
		primitive_scratch_.tex_coord2[1] = point.p3_v2;
	}
	return true;
}

bool VulkanRenderer::BuildRetainedPointVertex(const g3Point &point,
	const vector &object_position, BaseVertex *vertex)
{
	if (!std::isfinite(object_position.x) ||
		!std::isfinite(object_position.y) ||
		!std::isfinite(object_position.z) ||
		!BuildPointVertex(point, vertex))
		return false;
	vertex->position[0] = object_position.x;
	vertex->position[1] = object_position.y;
	vertex->position[2] = object_position.z;
	return true;
}

void VulkanRenderer::ReleaseRetainedMeshCache()
{
	IRetainedWorld *world = runtime_ ? runtime_->RetainedWorldBridge() : nullptr;
	if (world)
		for (const RetainedMeshCacheEntry &entry : retained_mesh_cache_)
			world->ReleaseMesh(entry.range.mesh);
	retained_mesh_cache_.clear();
}

bool VulkanRenderer::EmitRetainedTerrain(
	const renderer_retained_terrain_submission &submission)
{
	if (!frame_interval_open_ || !submission.cells || submission.cell_count == 0 ||
		!submission.work || submission.work_count == 0 || !submission.batches ||
		submission.batch_count == 0 || !submission.base_bitmap_handles ||
		submission.base_bitmap_count != submission.batch_count)
	{
		Fail(!frame_interval_open_ ? RuntimeFailure::InvalidLifecycle :
			RuntimeFailure::InvalidArgument, "DrawRetainedTerrain");
		return false;
	}
	for (uint32_t page = 0; page < 4; ++page)
		if (submission.dynamic_light_count[page] >
			RENDERER_MAX_PER_PIXEL_DYNAMIC_LIGHTS)
		{
			Fail(RuntimeFailure::InvalidArgument,
				"DrawRetainedTerrain.Lights");
			return false;
		}
	RenderCaptureSegment *capture = Capture("DrawRetainedTerrain");
	if (!capture) return false;
	TerrainArrayRequest array_request = {};
	array_request.base_bitmap_handles = submission.base_bitmap_handles;
	array_request.base_bitmap_count = submission.base_bitmap_count;
	std::memcpy(array_request.lightmap_handles, submission.lightmap_handles,
		sizeof(array_request.lightmap_handles));
	array_request.filtering = legacy_state_.filtering && preferred_.filtering;
	array_request.mipping = legacy_state_.mipping && preferred_.mipping;
	ResolvedTexture base_array = {}, lightmap_array = {};
	if (!runtime_->ResolveTerrainTextureArrays(array_request, &base_array,
		&lightmap_array))
	{
		Fail(RuntimeFailure::TextureResolutionFailed,
			"DrawRetainedTerrain.Arrays");
		return false;
	}
	TextureRequest diagnostic_request = {};
	diagnostic_request.logical_handle = -1;
	diagnostic_request.map_type = MAP_TYPE_BITMAP;
	diagnostic_request.role = TextureRole::Base2D;
	ResolvedTexture diagnostic_2d = {}, diagnostic_array = {};
	if (!runtime_->ResolveTexture(diagnostic_request, &diagnostic_2d))
		return false;
	diagnostic_request.role = TextureRole::ReservedArray;
	if (!runtime_->ResolveTexture(diagnostic_request, &diagnostic_array))
		return false;

	IRetainedWorld *world = runtime_->RetainedWorldBridge();
	if (!world)
	{
		Fail(RuntimeFailure::UnsupportedFeature, "DrawRetainedTerrain.World");
		return false;
	}
	const bool source_changed = retained_terrain_source_id_ !=
		submission.source_id;
	const bool content_changed = source_changed ||
		retained_terrain_source_generation_ != submission.source_generation ||
		retained_terrain_base_version_ != base_array.version.id ||
		retained_terrain_lightmap_version_ != lightmap_array.version.id;
	if (content_changed || retained_terrain_mesh_.id == kInvalidId)
	{
		if (++retained_terrain_mesh_generation_ == 0)
			++retained_terrain_mesh_generation_;
		RetainedTerrainUpload upload = {};
		upload.source.kind = RetainedSourceKind::Terrain;
		upload.source.source_id = submission.source_id;
		upload.source.source_generation = retained_terrain_mesh_generation_;
		upload.cells = reinterpret_cast<const TerrainEmitterCell *>(submission.cells);
		upload.cell_count = submission.cell_count;
		upload.full_draw_work = reinterpret_cast<const TerrainWorkItem *>(submission.work);
		upload.full_draw_work_count = submission.work_count;
		upload.batches = reinterpret_cast<const TerrainBatchInput *>(submission.batches);
		upload.batch_count = submission.batch_count;
		upload.base_texture_array = base_array.version.id;
		upload.lightmap_array = lightmap_array.version.id;
		MeshHandle replacement = { kInvalidId, 0 };
		bool uploaded = false;
		if (!source_changed && retained_terrain_mesh_.id != kInvalidId)
			uploaded = world->ReplaceTerrain(retained_terrain_mesh_, upload,
				&replacement);
		else
		{
			if (retained_terrain_mesh_.id != kInvalidId)
				world->ReleaseMesh(retained_terrain_mesh_);
			uploaded = world->CreateTerrain(upload, &replacement);
		}
		if (!uploaded)
		{
			Fail(RuntimeFailure::ResourceExhausted,
				"DrawRetainedTerrain.Upload");
			return false;
		}
		retained_terrain_mesh_ = replacement;
		retained_terrain_source_id_ = submission.source_id;
		retained_terrain_source_generation_ = submission.source_generation;
		retained_terrain_base_version_ = base_array.version.id;
		retained_terrain_lightmap_version_ = lightmap_array.version.id;
	}

	CapturedShaderRasterState state = {};
	MrtDrawRoutingDecision routing = {};
	const uint32_t saved_motion_flags = legacy_state_.motion_flags;
	legacy_state_.motion_flags |= kMotionObjectFlagForceCapture;
	const bool state_ok = BuildDrawState(PrimitiveSourceKind::TerrainEmitter,
		RENDERER_DRAW_CALL_3D,
		static_cast<uint32_t>(rend_Get3DDrawCallCategory()), &state, &routing);
	legacy_state_.motion_flags = saved_motion_flags;
	if (!state_ok) return false;
	const uint32_t preserved_flags = state.shader.shader_flags &
		(kShaderMotionWrite | kShaderAoCaptureWeight);
	state.shader.shader_flags = preserved_flags | kShaderTextured |
		kShaderLightmapped | kShaderTerrain;
	if (legacy_state_.fog_enabled)
		state.shader.shader_flags |= kShaderGenericFog |
			kShaderTerrainFogBloomSuppression;
	state.shader.texture_type = kTerrainMaterialContract.texture_type_linear;
	state.shader.overlay_type = 1;
	state.shader.lighting_color_model = kTerrainMaterialContract.lighting_none |
		(kTerrainMaterialContract.color_model_rgb << 16u);
	state.shader.alpha_type = kTerrainMaterialContract.alpha_type_constant_texture;
	state.shader.alpha_value = kTerrainMaterialContract.alpha_value;
	state.shader.blend_class = static_cast<uint32_t>(
		kTerrainMaterialContract.blend_class);
	state.shader.alpha_factor = 1.0f;
	state.shader.ao_class = kTerrainMaterialContract.ao_class_encoded_value;
	state.shader.post_values[0] = state.shader.post_values[1] = 0.0f;
	state.shader.state_flags2 &= kStateFiltering | kStateMipping;
	state.cull_enabled = 0;
	if (submission.scissor_enabled)
	{
		if (submission.scissor_right < submission.scissor_left ||
			submission.scissor_bottom < submission.scissor_top ||
			state.scissor >= capture->Viewports().size())
			return false;
		CapturedViewport scissor = capture->Viewports()[state.scissor];
		const int32_t factor = static_cast<int32_t>(
			std::max(1u, scissor.ssaa_factor));
		const int32_t overscan_x = scissor.physical_rect.x -
			scissor.logical_rect.x * factor;
		const int32_t overscan_y = scissor.physical_rect.y -
			scissor.logical_rect.y * factor;
		scissor.logical_rect = { submission.scissor_left,
			submission.scissor_top,
			submission.scissor_right - submission.scissor_left + 1,
			submission.scissor_bottom - submission.scissor_top + 1 };
		scissor.physical_rect = {
			scissor.logical_rect.x * factor + overscan_x,
			scissor.logical_rect.y * factor + overscan_y,
			scissor.logical_rect.width * factor,
			scissor.logical_rect.height * factor };
		scissor.scissor_enabled = 1;
		state.scissor = capture->InternViewport(scissor);
		if (state.scissor == kInvalidId) return false;
	}

	GpuWorldAux aux = terrain_fog_;
	scratch_terrain_lights_.clear();
	for (uint32_t page = 0; page < 4; ++page)
	{
		const uint32_t first = static_cast<uint32_t>(scratch_terrain_lights_.size());
		const uint32_t count = submission.dynamic_light_count[page];
		aux.indices[page] = (first << 8u) | count;
		for (uint32_t local = 0; local < count; ++local)
		{
			const renderer_per_pixel_light &source =
				submission.dynamic_lights[page *
					RENDERER_MAX_PER_PIXEL_DYNAMIC_LIGHTS + local];
			GpuDynamicLight light = {};
			for (uint32_t channel = 0; channel < 3; ++channel)
			{
				light.position_radius[channel] = source.position[channel];
				light.color_falloff[channel] = source.color[channel];
				light.direction_dot_range[channel] = source.direction[channel];
			}
			light.position_radius[3] = source.radius;
			light.color_falloff[3] = source.falloff;
			light.direction_dot_range[3] = source.dot_range;
			light.specular_and_flags[1] = source.directional ? 1.0f : 0.0f;
			scratch_terrain_lights_.push_back(light);
		}
	}
	state.shader.dynamic_light_count = static_cast<uint32_t>(
		scratch_terrain_lights_.size());
	if (!scratch_terrain_lights_.empty())
		state.shader.shader_flags |= kShaderDynamicLights;

	CapturedPayloadBinding binding = EmptyPayloadBinding();
	auto copy_payload = [&](const void *bytes, uint64_t byte_size,
		CapturedPayloadSemantic semantic, PayloadDataId *id) {
		if (byte_size == 0 || byte_size > UINT32_MAX) return false;
		*id = capture->CopyPayloadData(bytes, static_cast<uint32_t>(byte_size),
			16, semantic);
		return *id != kInvalidId;
	};
	if (!copy_payload(submission.cells,
			uint64_t(submission.cell_count) * sizeof(*submission.cells),
			kPayloadTerrainCells, &binding.terrain_cells) ||
		!copy_payload(submission.work,
			uint64_t(submission.work_count) * sizeof(*submission.work),
			kPayloadTerrainWorkList, &binding.terrain_work_items) ||
		!copy_payload(submission.batches,
			uint64_t(submission.batch_count) * sizeof(*submission.batches),
			kPayloadTerrainBatches, &binding.terrain_batches) ||
		!copy_payload(&submission.view, sizeof(submission.view),
			kPayloadTerrainViewInput, &binding.terrain_view_input))
	{
		Fail(RuntimeFailure::ResourceExhausted,
			"DrawRetainedTerrain.PayloadInputs");
		return false;
	}
	binding.validity_flags |= kPayloadHasTerrainCells |
		kPayloadHasTerrainWorkItems | kPayloadHasTerrainBatches |
		kPayloadHasTerrainViewInput;
	if (!scratch_terrain_lights_.empty())
	{
		if (!copy_payload(scratch_terrain_lights_.data(),
				uint64_t(scratch_terrain_lights_.size()) * sizeof(GpuDynamicLight),
				kPayloadDynamicLights, &binding.dynamic_lights))
			return false;
		binding.validity_flags |= kPayloadHasDynamicLights;
	}
	if (!copy_payload(&aux, sizeof(aux), kPayloadWorldAux, &binding.world_aux))
		return false;
	binding.validity_flags |= kPayloadHasWorldAux;
	const PayloadRef payload = capture->InternPayloadBinding(binding);
	if (payload == kInvalidId)
	{
		Fail(RuntimeFailure::ResourceExhausted,
			"DrawRetainedTerrain.PayloadBinding");
		return false;
	}

	CapturedMaterial material = {};
	for (uint32_t i = 0; i < 4; ++i)
	{
		material.image2d[i] = diagnostic_2d.version.id;
		material.image2d_array[i] = diagnostic_array.version.id;
		material.sampler[i] = diagnostic_2d.sampler_index;
	}
	material.image2d_array[0] = base_array.version.id;
	material.image2d_array[1] = lightmap_array.version.id;
	material.sampler[0] = base_array.sampler_index;
	material.sampler[1] = lightmap_array.sampler_index;
	CapturedTransform transform = {};
	Identity(transform.current_model);
	Identity(transform.previous_model);
	const StateId state_id = capture->InternState(state);
	const MaterialRef material_id = capture->InternMaterial(material);
	const TransformId transform_id = capture->InternTransform(transform);
	if (state_id == kInvalidId || material_id == kInvalidId ||
		transform_id == kInvalidId)
		return false;
	CaptureCommand command = {};
	command.schema_version = kCaptureSchemaVersion;
	command.type = CaptureCommandType::DrawRetained;
	DrawRetainedCommand &draw = command.payload.draw_retained;
	draw.mesh = retained_terrain_mesh_;
	draw.geometry_mode = GeometryMode::T2Terrain;
	draw.perspective_payload = { kInvalidId, 0 };
	draw.motion_payload = { kInvalidId, 0 };
	draw.specular_payload = { kInvalidId, 0 };
	draw.state = state_id;
	draw.transform = transform_id;
	draw.material = material_id;
	draw.optional_payload = payload;
	draw.view = active_view_;
	draw.classification = { RENDERER_DRAW_CALL_3D,
		static_cast<uint32_t>(rend_Get3DDrawCallCategory()),
		PrimitiveSourceKind::TerrainEmitter, 0 };
	if (!Append(command, "DrawRetainedTerrain") ||
		!AdvanceTargetVersion((state.mrt_write_mask & kWriteColor) != 0,
			state.depth_write_enabled != 0, "DrawRetainedTerrain"))
		return false;
	legacy_state_.selected_draw_buffers =
		routing.state_after_draw.selected_draw_buffers;
	for (uint32_t i = 0; i < 5; ++i)
		legacy_state_.attachment_color_masks[i] =
			routing.state_after_draw.attachment_masks.rgba[i];
	return true;
}

bool VulkanRenderer::EmitRetainedBatch(int handle,
	const renderer_retained_poly_batch_item *items, int count, int map_type)
{
	if (!frame_interval_open_ || !items || count <= 0)
	{
		Fail(!frame_interval_open_ ? RuntimeFailure::InvalidLifecycle :
			RuntimeFailure::InvalidArgument, "DrawRetainedPolygon3DBatch");
		return false;
	}
	IRetainedWorld *world = runtime_ ? runtime_->RetainedWorldBridge() : nullptr;
	if (!world)
	{
		Fail(RuntimeFailure::UnsupportedFeature,
			"DrawRetainedPolygon3DBatch.World");
		return false;
	}

	scratch_retained_eligibility_.assign(static_cast<size_t>(count),
		T1EligibilityResult{});
	std::vector<T1EligibilityResult> &eligibility =
		scratch_retained_eligibility_;
	uint32_t retained_count = 0;
	for (int item_index = 0; item_index < count; ++item_index)
	{
		const renderer_retained_poly_batch_item &item = items[item_index];
		T1SourceKind source = T1SourceKind::Other;
		switch (item.source_kind)
		{
		case RENDERER_RETAINED_ROOM_BASE:
			source = T1SourceKind::OrdinaryRoomBase;
			break;
		case RENDERER_RETAINED_ROOM_POSTRENDER:
			source = T1SourceKind::OrdinaryRoomPostrender;
			break;
		case RENDERER_RETAINED_ROOM_SPECULAR:
			source = T1SourceKind::OrdinaryRoomSpecular;
			break;
		case RENDERER_RETAINED_STATIC_POLYMODEL:
			source = T1SourceKind::OrdinaryStaticPolymodel;
			break;
		default:
			break;
		}
		uint32_t cc_and = 0xffu;
		uint32_t cc_or = 0;
		bool finite_positive = item.pointlist != nullptr &&
			item.world_positions != nullptr && item.nv >= 3;
		if (item.pointlist && item.nv > 0)
			for (int vertex = 0; vertex < item.nv; ++vertex)
			{
				const g3Point *point = item.pointlist[vertex];
				if (!point || !std::isfinite(point->p3_z) || point->p3_z <= 0.0f)
					finite_positive = false;
				if (point)
				{
					cc_and &= point->p3_codes;
					cc_or |= point->p3_codes;
				}
			}
		else
			cc_and = 0;
		T1EligibilityInput input = {};
		input.source = source;
		input.z_bias = legacy_state_.z_bias;
		input.cc_and = cc_and;
		input.cc_or = cc_or;
		input.all_z_finite_and_positive = finite_positive ? 1u : 0u;
		input.exclusion_bits = item.exclusion_bits;
		input.payload_representable = item.payload_representable ? 1u : 0u;
		input.retained_range_available = 1;
		eligibility[static_cast<size_t>(item_index)] =
			EvaluateT1Eligibility(input);
		if (eligibility[static_cast<size_t>(item_index)].eligible)
			++retained_count;
	}
	auto emit_stream_fallback = [&]() {
		scratch_poly_batch_items_.clear();
		try
		{
			scratch_poly_batch_items_.reserve(static_cast<size_t>(count));
			for (int item_index = 0; item_index < count; ++item_index)
				if (!eligibility[static_cast<size_t>(item_index)].whole_primitive_rejected)
					scratch_poly_batch_items_.push_back({
						items[item_index].pointlist, items[item_index].nv });
		}
		catch (const std::bad_alloc &)
		{
			// Preserve the exact fallback even under memory pressure.
			for (int item_index = 0; item_index < count; ++item_index)
				if (!eligibility[static_cast<size_t>(item_index)].whole_primitive_rejected)
					DrawPolygon3D(handle, items[item_index].pointlist,
						items[item_index].nv, map_type);
			return;
		}
		if (!scratch_poly_batch_items_.empty())
			DrawPolygon3DBatch(handle, scratch_poly_batch_items_.data(),
				static_cast<int>(scratch_poly_batch_items_.size()), map_type);
	};

	if (retained_count == 0)
	{
		emit_stream_fallback();
		return true;
	}
	CapturedShaderRasterState prototype_state = {};
	MrtDrawRoutingDecision prototype_routing = {};
	if (!BuildDrawState(PrimitiveSourceKind::PolygonFan,
		RENDERER_DRAW_CALL_3D,
		static_cast<uint32_t>(rend_Get3DDrawCallCategory()),
		&prototype_state, &prototype_routing))
		return false;
	// Vertex-varying Phong/dynamic-light/specular payloads need their frozen T1
	// stream.  Engine batch sites currently mark those entries unrepresentable;
	// retain this guard so a future caller cannot silently omit one.
	if ((prototype_state.shader.state_flags2 & kStatePrimaryPayloadMask) != 0 ||
		(prototype_state.shader.shader_flags &
			(kShaderPerPixelSpecular | kShaderFieldSpecular)) != 0)
	{
		emit_stream_fallback();
		return true;
	}

	const bool retain_motion = (prototype_state.shader.shader_flags &
		kShaderMotionWrite) != 0;
	try
	{
		retained_mesh_cache_.reserve(retained_mesh_cache_.size() + retained_count);
	}
	catch (const std::bad_alloc &)
	{
		// Retention is an optimization seam.  The ordered T0 path remains exact.
		emit_stream_fallback();
		return true;
	}

	auto same_identity = [](const RetainedFaceToken &left,
		const RetainedFaceToken &right) {
		return left.source.kind == right.source.kind &&
			left.source.source_id == right.source.source_id &&
			left.subobject == right.subobject && left.face == right.face &&
			left.classification == right.classification;
	};
	auto bytes_equal = [](const void *left, const void *right, size_t bytes) {
		return bytes == 0 || std::memcmp(left, right, bytes) == 0;
	};

	for (int item_index = 0; item_index < count; ++item_index)
	{
		const T1EligibilityResult &item_eligibility =
			eligibility[static_cast<size_t>(item_index)];
		if (item_eligibility.whole_primitive_rejected)
			continue;
		const renderer_retained_poly_batch_item &item = items[item_index];
		if (!item_eligibility.eligible)
		{
			DrawPolygon3D(handle, item.pointlist, item.nv, map_type);
			continue;
		}

		RetainedFaceToken token = {};
		token.source.kind = item.source_kind ==
			RENDERER_RETAINED_STATIC_POLYMODEL ?
			RetainedSourceKind::Polymodel : RetainedSourceKind::Room;
		token.source.source_id = item.source_id;
		token.source.source_generation = item.source_generation;
		token.subobject = item.subobject;
		token.face = item.face;
		token.classification = item.classification;
		if (token.source.source_id == kInvalidId)
		{
			DrawPolygon3D(handle, item.pointlist, item.nv, map_type);
			continue;
		}

		scratch_vertices_.clear();
		scratch_vertices_.reserve(static_cast<size_t>(item.nv));
		scratch_indices_.clear();
		scratch_indices_.reserve(static_cast<size_t>(item.nv - 2) * 3u);
		scratch_motion_.clear();
		if (retain_motion)
			scratch_motion_.reserve(static_cast<size_t>(item.nv));
		bool valid_vertices = true;
		for (int vertex = 0; vertex < item.nv; ++vertex)
		{
			BaseVertex output = {};
			if (!BuildRetainedPointVertex(*item.pointlist[vertex],
				item.world_positions[vertex], &output))
			{
				valid_vertices = false;
				break;
			}
			scratch_vertices_.push_back(output);
			if (retain_motion)
			{
				const g3Point &point = *item.pointlist[vertex];
				MotionVertexPayload motion = {};
				const vector &current = point.p3_motion_world_valid ?
					point.p3_motion_world_pos : item.world_positions[vertex];
				const vector &previous = point.p3_motion_prev_world_valid ?
					point.p3_motion_prev_world_pos : current;
				motion.current_q[0] = current.x;
				motion.current_q[1] = current.y;
				motion.current_q[2] = current.z;
				motion.current_q[3] = 1.0f;
				motion.previous_q[0] = previous.x;
				motion.previous_q[1] = previous.y;
				motion.previous_q[2] = previous.z;
				motion.previous_q[3] = 1.0f;
				scratch_motion_.push_back(motion);
			}
		}
		if (!valid_vertices)
		{
			DrawPolygon3D(handle, item.pointlist, item.nv, map_type);
			continue;
		}
		for (uint32_t triangle = 0;
			triangle < static_cast<uint32_t>(item.nv - 2); ++triangle)
		{
			scratch_indices_.push_back(0);
			scratch_indices_.push_back(triangle + 1);
			scratch_indices_.push_back(triangle + 2);
		}

		uint64_t fingerprint = UINT64_C(1469598103934665603);
		fingerprint = HashRetainedBytes(fingerprint, scratch_vertices_.data(),
			scratch_vertices_.size() * sizeof(BaseVertex));
		fingerprint = HashRetainedBytes(fingerprint, scratch_indices_.data(),
			scratch_indices_.size() * sizeof(uint32_t));
		fingerprint = HashRetainedBytes(fingerprint, scratch_motion_.data(),
			scratch_motion_.size() * sizeof(MotionVertexPayload));

		auto cached = std::find_if(retained_mesh_cache_.begin(),
			retained_mesh_cache_.end(), [&](const RetainedMeshCacheEntry &entry) {
				return same_identity(entry.token, token);
			});
		const bool exact_content = cached != retained_mesh_cache_.end() &&
			cached->token.source.source_generation ==
				token.source.source_generation &&
			cached->content_fingerprint == fingerprint &&
			cached->vertices.size() == scratch_vertices_.size() &&
			cached->indices.size() == scratch_indices_.size() &&
			cached->motion.size() == scratch_motion_.size() &&
			bytes_equal(cached->vertices.data(), scratch_vertices_.data(),
				scratch_vertices_.size() * sizeof(BaseVertex)) &&
			bytes_equal(cached->indices.data(), scratch_indices_.data(),
				scratch_indices_.size() * sizeof(uint32_t)) &&
			bytes_equal(cached->motion.data(), scratch_motion_.data(),
				scratch_motion_.size() * sizeof(MotionVertexPayload));
		RetainedRange range = {};
		if (exact_content)
		{
			range = cached->range;
			cached->last_used_frame = presented_frame_serial_;
		}
		else
		{
			// Replacing a handle already referenced by this capture would make the
			// earlier command stale before compilation.  Preserve ordering and use
			// T0 for only this conflicting occurrence.
			if (cached != retained_mesh_cache_.end() &&
				cached->last_used_frame == presented_frame_serial_)
			{
				DrawPolygon3D(handle, item.pointlist, item.nv, map_type);
				continue;
			}

			RetainedFaceRangeUpload uploaded = {};
			uploaded.token = token;
			uploaded.first_index = 0;
			uploaded.index_count =
				static_cast<uint32_t>(scratch_indices_.size());
			uploaded.base_vertex = 0;
			uploaded.perspective_payload = { kInvalidId, 0 };
			uploaded.motion_payload = retain_motion ?
				Span32{ 0, static_cast<uint32_t>(scratch_motion_.size()) } :
				Span32{ kInvalidId, 0 };
			uploaded.specular_payload = { kInvalidId, 0 };
			RetainedMeshUpload upload = {};
			upload.source = token.source;
			upload.vertices = scratch_vertices_.data();
			upload.vertex_count =
				static_cast<uint32_t>(scratch_vertices_.size());
			upload.indices = scratch_indices_.data();
			upload.index_count = static_cast<uint32_t>(scratch_indices_.size());
			if (retain_motion)
			{
				upload.motion_payload = scratch_motion_.data();
				upload.motion_payload_count =
					static_cast<uint32_t>(scratch_motion_.size());
			}

			RetainedMeshCacheEntry replacement;
			try
			{
				replacement.token = token;
				replacement.content_fingerprint = fingerprint;
				replacement.last_used_frame = presented_frame_serial_;
				replacement.vertices = scratch_vertices_;
				replacement.indices = scratch_indices_;
				replacement.motion = scratch_motion_;
			}
			catch (const std::bad_alloc &)
			{
				DrawPolygon3D(handle, item.pointlist, item.nv, map_type);
				continue;
			}

			MeshHandle mesh = { kInvalidId, 0 };
			bool uploaded_ok = false;
			bool old_mesh_released = false;
			if (cached == retained_mesh_cache_.end())
				uploaded_ok = world->CreateMesh(upload, &uploaded, 1, &mesh);
			else if (token.source.source_generation >
				cached->token.source.source_generation)
			{
				uploaded_ok = world->ReplaceMesh(cached->range.mesh, upload,
					&uploaded, 1, &mesh);
				old_mesh_released = uploaded_ok;
			}
			else
			{
				world->ReleaseMesh(cached->range.mesh);
				old_mesh_released = true;
				uploaded_ok = world->CreateMesh(upload, &uploaded, 1, &mesh);
				if (!uploaded_ok)
				{
					retained_mesh_cache_.erase(cached);
					cached = retained_mesh_cache_.end();
				}
			}
			if (!uploaded_ok || !world->ResolveFace(token, &range))
			{
				if (uploaded_ok)
					world->ReleaseMesh(mesh);
				if (old_mesh_released && cached != retained_mesh_cache_.end())
					retained_mesh_cache_.erase(cached);
				DrawPolygon3D(handle, item.pointlist, item.nv, map_type);
				continue;
			}
			replacement.range = range;
			if (cached == retained_mesh_cache_.end())
				retained_mesh_cache_.push_back(std::move(replacement));
			else
				*cached = std::move(replacement);
		}

		CapturedMaterial material = {};
		CapturedShaderRasterState state = {};
		MrtDrawRoutingDecision routing = {};
		if (!BuildMaterial(handle, map_type, &material) ||
			!BuildDrawState(PrimitiveSourceKind::PolygonFan,
				RENDERER_DRAW_CALL_3D,
				static_cast<uint32_t>(rend_Get3DDrawCallCategory()),
				&state, &routing))
			return false;
		PayloadRef payload = kInvalidId;
		if (!BuildPayload(nullptr, 0, state, &payload))
		{
			Fail(RuntimeFailure::ResourceExhausted,
				"DrawRetainedPolygon3DBatch.Payload");
			return false;
		}
		CapturedTransform transform = {};
		// The engine bridge freezes room/model positions in world space at the
		// exact live flush.  This also keeps cockpit-wide model batches correct
		// after their instance stack has already unwound.
		Identity(transform.current_model);
		Identity(transform.previous_model);
		RenderCaptureSegment *capture = Capture("DrawRetainedPolygon3DBatch");
		if (!capture)
			return false;
		const StateId state_id = capture->InternState(state);
		const MaterialRef material_id = capture->InternMaterial(material);
		const TransformId transform_id = capture->InternTransform(transform);
		if (state_id == kInvalidId || material_id == kInvalidId ||
			transform_id == kInvalidId)
		{
			Fail(RuntimeFailure::ResourceExhausted,
				"DrawRetainedPolygon3DBatch.Tables");
			return false;
		}
		CaptureCommand command = {};
		command.schema_version = kCaptureSchemaVersion;
		command.type = CaptureCommandType::DrawRetained;
		DrawRetainedCommand &draw = command.payload.draw_retained;
		draw.mesh = range.mesh;
		draw.first_index = range.first_index;
		draw.index_count = range.index_count;
		draw.base_vertex = range.base_vertex;
		draw.geometry_mode = GeometryMode::T1Retained;
		draw.perspective_payload = range.perspective_payload;
		draw.motion_payload = range.motion_payload;
		draw.specular_payload = range.specular_payload;
		draw.state = state_id;
		draw.transform = transform_id;
		draw.material = material_id;
		draw.optional_payload = payload;
		draw.view = active_view_;
		draw.classification = { RENDERER_DRAW_CALL_3D,
			static_cast<uint32_t>(rend_Get3DDrawCallCategory()),
			PrimitiveSourceKind::PolygonFan, item.classification };
		if (!Append(command, "DrawRetainedPolygon3DBatch") ||
			!AdvanceTargetVersion((state.mrt_write_mask & kWriteColor) != 0,
				state.depth_write_enabled != 0,
				"DrawRetainedPolygon3DBatch"))
			return false;
		legacy_state_.selected_draw_buffers =
			routing.state_after_draw.selected_draw_buffers;
		for (uint32_t attachment = 0; attachment < 5; ++attachment)
			legacy_state_.attachment_color_masks[attachment] =
				routing.state_after_draw.attachment_masks.rgba[attachment];
		current_stats_.vert_count += item.nv;
		++current_stats_.poly_count;
	}
	return true;
}

bool VulkanRenderer::EmitStream(const BaseVertex *vertices,
	uint32_t vertex_count, const uint32_t *indices, uint32_t index_count,
	DepthInterpretation depth, PrimitiveSourceKind source, uint32_t category,
	uint32_t category_3d, int handle, int map_type,
	g3Point *const *source_points, const char *operation)
{
	if (!frame_interval_open_ || !vertices || vertex_count == 0 ||
		(index_count != 0 && (!indices || index_count % 3 != 0)))
	{
		Fail(!frame_interval_open_ ? RuntimeFailure::InvalidLifecycle :
			RuntimeFailure::InvalidArgument, operation);
		return false;
	}
	RenderCaptureSegment *capture = Capture(operation);
	if (!capture)
		return false;
	if (legacy_state_.soft_particle_enabled && !soft_depth_acquired_)
	{
		if (active_target_ != RenderTargetClass::Scene)
		{
			Fail(RuntimeFailure::InvalidLifecycle, operation);
			return false;
		}
		CaptureCommand acquire = {};
		acquire.schema_version = kCaptureSchemaVersion;
		acquire.type = CaptureCommandType::AcquireSoftDepth;
		acquire.payload.acquire_soft_depth.scene_target_version =
			active_target_version_;
		acquire.payload.acquire_soft_depth.depth_epoch =
			active_version_record_.depth_epoch;
		if (++soft_depth_snapshot_serial_ == 0)
			++soft_depth_snapshot_serial_;
		acquire.payload.acquire_soft_depth.snapshot_id =
			soft_depth_snapshot_serial_;
		if (!Append(acquire, operation))
			return false;
		soft_depth_acquired_ = true;
	}
	CapturedMaterial material = {};
	if (!BuildMaterial(handle, map_type, &material))
		return false;
	CapturedShaderRasterState state = {};
	MrtDrawRoutingDecision routing = {};
	if (!BuildDrawState(source, category, category_3d, &state, &routing))
		return false;
	PayloadRef payload = kInvalidId;
	if (!BuildPayload(source_points, vertex_count, state, &payload))
	{
		Fail(RuntimeFailure::ResourceExhausted, operation);
		return false;
	}
	CapturedTransform transform = {};
	std::memcpy(transform.current_model, legacy_state_.current_object,
		sizeof(transform.current_model));
	std::memcpy(transform.previous_model, legacy_state_.previous_object,
		sizeof(transform.previous_model));
	const StateId state_id = capture->InternState(state);
	const MaterialRef material_id = capture->InternMaterial(material);
	const TransformId transform_id = capture->InternTransform(transform);
	const StreamGeometryRef geometry = capture->CopyStreamGeometry(vertices,
		vertex_count, indices, index_count, nullptr, 0, depth);
	if (state_id == kInvalidId || material_id == kInvalidId ||
		transform_id == kInvalidId || geometry.vertices.offset == kInvalidId)
	{
		Fail(RuntimeFailure::ResourceExhausted, operation);
		return false;
	}
	CaptureCommand command = {};
	command.schema_version = kCaptureSchemaVersion;
	command.type = CaptureCommandType::DrawStream;
	command.payload.draw_stream.geometry = geometry;
	command.payload.draw_stream.state = state_id;
	command.payload.draw_stream.transform = transform_id;
	command.payload.draw_stream.material = material_id;
	command.payload.draw_stream.optional_payload = payload;
	command.payload.draw_stream.view = active_view_;
	command.payload.draw_stream.classification =
		{ category, category_3d, source, 0 };
	if (!Append(command, operation))
		return false;
	if (!AdvanceTargetVersion((state.mrt_write_mask & kWriteColor) != 0,
		state.depth_write_enabled != 0, operation))
		return false;
	legacy_state_.selected_draw_buffers =
		routing.state_after_draw.selected_draw_buffers;
	for (uint32_t i = 0; i < 5; ++i)
		legacy_state_.attachment_color_masks[i] =
			routing.state_after_draw.attachment_masks.rgba[i];
	current_stats_.vert_count += static_cast<int>(vertex_count);
	if (source == PrimitiveSourceKind::PolygonFan ||
		source == PrimitiveSourceKind::Polygon2D)
		++current_stats_.poly_count;
	else if (source == PrimitiveSourceKind::ParticleInstances)
		current_stats_.poly_count += static_cast<int>(index_count / 6);
	else
		current_stats_.poly_count += index_count != 0 ?
			static_cast<int>(index_count / 3) : 1;
	return true;
}

void VulkanRenderer::DrawPolygon3D(int handle, g3Point **points, int count,
	int map_type)
{
	if (!points || count < 3 || count > 65535)
	{
		Fail(RuntimeFailure::InvalidArgument, "DrawPolygon3D");
		return;
	}
	scratch_vertices_.resize(static_cast<size_t>(count));
	for (int i = 0; i < count; ++i)
	{
		if (!points[i] || !BuildPointVertex(*points[i], &scratch_vertices_[i]))
		{
			Fail(RuntimeFailure::InvalidArgument, "DrawPolygon3D");
			return;
		}
	}
	scratch_indices_.resize(static_cast<size_t>(count - 2) * 3);
	for (uint32_t triangle = 0; triangle < static_cast<uint32_t>(count - 2);
		++triangle)
	{
		scratch_indices_[triangle * 3] = 0;
		scratch_indices_[triangle * 3 + 1] = triangle + 1;
		scratch_indices_[triangle * 3 + 2] = triangle + 2;
	}
	EmitStream(scratch_vertices_.data(), static_cast<uint32_t>(count),
		scratch_indices_.data(), static_cast<uint32_t>(scratch_indices_.size()),
		DepthInterpretation::EyeZLegacyMapped, PrimitiveSourceKind::PolygonFan,
		RENDERER_DRAW_CALL_3D,
		static_cast<uint32_t>(rend_Get3DDrawCallCategory()), handle, map_type,
		points, "DrawPolygon3D");
}

void VulkanRenderer::DrawPolygon3DBatch(int handle,
	const renderer_poly_batch_item *items, int count, int map_type)
{
	if (!items || count <= 0)
	{
		Fail(RuntimeFailure::InvalidArgument, "DrawPolygon3DBatch");
		return;
	}

	uint64_t vertex_count = 0;
	uint64_t index_count = 0;
	uint32_t polygon_count = 0;
	for (int item_index = 0; item_index < count; ++item_index)
	{
		const renderer_poly_batch_item &item = items[item_index];
		// Match GL4's live batch contract: malformed/oversized faces are skipped.
		if (!item.pointlist || item.nv < 3 || item.nv >= 100)
			continue;
		vertex_count += static_cast<uint32_t>(item.nv);
		index_count += static_cast<uint32_t>(item.nv - 2) * 3u;
		++polygon_count;
	}
	if (polygon_count == 0)
		return;
	if (vertex_count > UINT32_MAX || index_count > UINT32_MAX)
	{
		Fail(RuntimeFailure::ResourceExhausted, "DrawPolygon3DBatch.Size");
		return;
	}

	scratch_vertices_.clear();
	scratch_indices_.clear();
	scratch_source_points_.clear();
	try
	{
		scratch_vertices_.reserve(static_cast<size_t>(vertex_count));
		scratch_indices_.reserve(static_cast<size_t>(index_count));
		scratch_source_points_.reserve(static_cast<size_t>(vertex_count));
	}
	catch (const std::bad_alloc &)
	{
		Fail(RuntimeFailure::ResourceExhausted, "DrawPolygon3DBatch.Reserve");
		return;
	}

	for (int item_index = 0; item_index < count; ++item_index)
	{
		const renderer_poly_batch_item &item = items[item_index];
		if (!item.pointlist || item.nv < 3 || item.nv >= 100)
			continue;
		const uint32_t base_vertex =
			static_cast<uint32_t>(scratch_vertices_.size());
		for (int vertex = 0; vertex < item.nv; ++vertex)
		{
			g3Point *point = item.pointlist[vertex];
			BaseVertex output = {};
			if (!point || !BuildPointVertex(*point, &output))
			{
				Fail(RuntimeFailure::InvalidArgument,
					"DrawPolygon3DBatch.Vertex");
				return;
			}
			scratch_vertices_.push_back(output);
			scratch_source_points_.push_back(point);
		}
		for (uint32_t triangle = 0;
			triangle < static_cast<uint32_t>(item.nv - 2); ++triangle)
		{
			scratch_indices_.push_back(base_vertex);
			scratch_indices_.push_back(base_vertex + triangle + 1);
			scratch_indices_.push_back(base_vertex + triangle + 2);
		}
	}

	if (EmitStream(scratch_vertices_.data(),
		static_cast<uint32_t>(scratch_vertices_.size()),
		scratch_indices_.data(), static_cast<uint32_t>(scratch_indices_.size()),
		DepthInterpretation::EyeZLegacyMapped, PrimitiveSourceKind::PolygonFan,
		RENDERER_DRAW_CALL_3D,
		static_cast<uint32_t>(rend_Get3DDrawCallCategory()), handle, map_type,
		scratch_source_points_.data(), "DrawPolygon3DBatch"))
	{
		// EmitStream accounts for one polygon; compatibility statistics count
		// the original faces represented by this one logical batch record.
		current_stats_.poly_count += static_cast<int>(polygon_count - 1);
	}
}

void VulkanRenderer::DrawRetainedPolygon3DBatch(int handle,
	const renderer_retained_poly_batch_item *items, int count, int map_type)
{
	EmitRetainedBatch(handle, items, count, map_type);
}

bool VulkanRenderer::DrawRetainedTerrain(
	const renderer_retained_terrain_submission *submission)
{
	return submission && EmitRetainedTerrain(*submission);
}

bool VulkanRenderer::SupportsParticleInstanceBatch() const
{
	return true;
}

bool VulkanRenderer::CanDrawParticleInstanceBatch() const
{
	return initialized_ && frame_interval_open_;
}

bool VulkanRenderer::DrawParticleInstanceBatch(int handle,
	const renderer_particle_instance *items, int count, int map_type)
{
	if (!CanDrawParticleInstanceBatch() || !items || count <= 0 ||
		static_cast<uint64_t>(count) * 4 > UINT32_MAX ||
		static_cast<uint64_t>(count) * 6 > UINT32_MAX)
	{
		Fail(!frame_interval_open_ ? RuntimeFailure::InvalidLifecycle :
			RuntimeFailure::InvalidArgument, "DrawParticleInstanceBatch");
		return false;
	}
	const uint32_t vertex_count = static_cast<uint32_t>(count) * 4;
	const uint32_t index_count = static_cast<uint32_t>(count) * 6;
	scratch_vertices_.assign(vertex_count, BaseVertex{});
	scratch_indices_.resize(index_count);
	for (int instance = 0; instance < count; ++instance)
	{
		const renderer_particle_instance &input = items[instance];
		const float corners[4][2] = {
			{ -1.0f, -1.0f }, { 1.0f, -1.0f },
			{ 1.0f, 1.0f }, { -1.0f, 1.0f },
		};
		const float uvs[4][2] = {
			{ input.u0, input.v0 }, { input.u1, input.v0 },
			{ input.u1, input.v1 }, { input.u0, input.v1 },
		};
		const uint32_t rgba = PackRgba(
			static_cast<uint32_t>(ClampUnit(input.r) * 255.0f),
			static_cast<uint32_t>(ClampUnit(input.g) * 255.0f),
			static_cast<uint32_t>(ClampUnit(input.b) * 255.0f),
			static_cast<uint32_t>(ClampUnit(input.a * legacy_state_.alpha_factor) *
				255.0f));
		const uint32_t first = static_cast<uint32_t>(instance) * 4;
		for (uint32_t corner = 0; corner < 4; ++corner)
		{
			const float local_x = corners[corner][0] * input.half_width;
			const float local_y = corners[corner][1] * input.half_height;
			BaseVertex &vertex = scratch_vertices_[first + corner];
			vertex.position[0] = input.screen_x +
				local_x * input.cos_rot - local_y * input.sin_rot;
			vertex.position[1] = input.screen_y +
				local_x * input.sin_rot + local_y * input.cos_rot;
			vertex.position[2] = input.eye_z;
			vertex.rgba8 = rgba;
			vertex.uv0[0] = uvs[corner][0];
			vertex.uv0[1] = uvs[corner][1];
		}
		const uint32_t index = static_cast<uint32_t>(instance) * 6;
		const uint32_t local[6] = { 0, 1, 2, 0, 2, 3 };
		for (uint32_t i = 0; i < 6; ++i)
			scratch_indices_[index + i] = first + local[i];
	}
	return EmitStream(scratch_vertices_.data(), vertex_count,
		scratch_indices_.data(), index_count,
		DepthInterpretation::EyeZLegacyMapped, PrimitiveSourceKind::ParticleInstances,
		RENDERER_DRAW_CALL_3D,
		static_cast<uint32_t>(rend_Get3DDrawCallCategory()), handle, map_type,
		nullptr, "DrawParticleInstanceBatch");
}

void VulkanRenderer::DrawPolygon2D(int handle, g3Point **points, int count)
{
	if (!points || count < 3 || count > 65535)
	{
		Fail(RuntimeFailure::InvalidArgument, "DrawPolygon2D");
		return;
	}
	scratch_vertices_.resize(static_cast<size_t>(count));
	for (int i = 0; i < count; ++i)
	{
		if (!points[i] || !BuildPointVertex(*points[i], &scratch_vertices_[i]))
		{
			Fail(RuntimeFailure::InvalidArgument, "DrawPolygon2D");
			return;
		}
	}
	scratch_indices_.resize(static_cast<size_t>(count - 2) * 3);
	for (uint32_t triangle = 0; triangle < static_cast<uint32_t>(count - 2);
		++triangle)
	{
		scratch_indices_[triangle * 3] = 0;
		scratch_indices_[triangle * 3 + 1] = triangle + 1;
		scratch_indices_[triangle * 3 + 2] = triangle + 2;
	}
	EmitStream(scratch_vertices_.data(), static_cast<uint32_t>(count),
		scratch_indices_.data(), static_cast<uint32_t>(scratch_indices_.size()),
		DepthInterpretation::AlreadyMapped, PrimitiveSourceKind::Polygon2D,
		RENDERER_DRAW_CALL_2D, RENDERER_DRAW_CALL_3D_OTHER, handle,
		MAP_TYPE_BITMAP, points, "DrawPolygon2D");
}

void VulkanRenderer::BeginMotionObject(int object_handle, int motion_object_flags)
{
	MotionObjectBeginInput input = {};
	input.object_handle = object_handle;
	input.motion_object_flags = static_cast<uint32_t>(motion_object_flags);
	input.framebuffer_available = frame_interval_open_ &&
		active_target_ != RenderTargetClass::PostPresent;
	input.post_present_pending =
		active_target_ == RenderTargetClass::PostPresent;
	input.capture_locked = legacy_state_.motion_capture_locked;
	input.cockpit_draw = active_target_ == RenderTargetClass::CockpitScene ||
		rend_Get3DDrawCallCategory() == RENDERER_DRAW_CALL_3D_COCKPIT;
	input.pixel_consumer_active =
		(preferred_.motion_vector_mode == RENDERER_MOTION_VECTOR_PIXEL ||
		 preferred_.motion_vector_debug_preview ||
		 preferred_.pixel_motion_blur_strength > 0.0f ||
		 preferred_.pixel_motion_blur_legacy_object_strength > 0.0f);
	input.velocity_texture_available = input.pixel_consumer_active;
	const MotionObjectScopeState scope = EvaluateBeginMotionObject(input);
	legacy_state_.motion_object_active = scope.active;
	legacy_state_.motion_object_id = scope.object_id;
	legacy_state_.motion_flags = scope.force_capture ?
		kMotionObjectFlagForceCapture : 0;
	if ((motion_object_flags & RENDERER_MOTION_OBJECT_LEGACY_BLUR) != 0)
		legacy_state_.motion_flags |= kMotionObjectFlagLegacyBlur;
}

void VulkanRenderer::EndMotionObject()
{
	MotionObjectScopeState scope = {};
	scope.active = legacy_state_.motion_object_active;
	scope.cockpit_active = active_target_ == RenderTargetClass::CockpitScene &&
		legacy_state_.motion_object_active;
	scope.force_capture =
		(legacy_state_.motion_flags & kMotionObjectFlagForceCapture) != 0;
	scope.object_id = legacy_state_.motion_object_id;
	const MotionObjectEndDecision decision = EvaluateEndMotionObject(scope, 1);
	if (decision.capture_cockpit_previous_view_projection)
		std::memcpy(current_view_.cockpit_previous_view_projection,
			current_view_.view_projection,
			sizeof(current_view_.cockpit_previous_view_projection));
	legacy_state_.motion_object_active = 0;
	legacy_state_.motion_object_id = 0;
	legacy_state_.motion_flags = 0;
}

void VulkanRenderer::SuspendMotionVectorWrites()
{
	const MotionSuppressionTransitionDecision decision =
		EvaluateMotionSuppressionTransition(legacy_state_.motion_suppression_depth,
			MotionSuppressionOperation::Suspend,
			preferred_.motion_vector_mode == RENDERER_MOTION_VECTOR_PIXEL);
	legacy_state_.motion_suppression_depth = decision.resulting_depth;
}

void VulkanRenderer::ResumeMotionVectorWrites()
{
	const MotionSuppressionTransitionDecision decision =
		EvaluateMotionSuppressionTransition(legacy_state_.motion_suppression_depth,
			MotionSuppressionOperation::Resume,
			preferred_.motion_vector_mode == RENDERER_MOTION_VECTOR_PIXEL);
	legacy_state_.motion_suppression_depth = decision.resulting_depth;
	if (decision.unmatched_resume)
		Fail(RuntimeFailure::InvalidLifecycle, "ResumeMotionVectorWrites");
}

void VulkanRenderer::FillMotionVectorRegion(int object_handle)
{
	FlushTextLayer();
	MotionRegionFillInput input = {};
	input.object_handle = object_handle;
	input.pixel_motion_mode_enabled =
		preferred_.motion_vector_mode == RENDERER_MOTION_VECTOR_PIXEL;
	input.frozen = frame_dynamic_.histories_frozen ? 1u : 0u;
	input.post_present_pending = active_target_ == RenderTargetClass::PostPresent;
	input.framebuffer_available = frame_interval_open_ &&
		active_target_ == RenderTargetClass::Scene;
	input.velocity_texture_available = input.pixel_motion_mode_enabled;
	input.positive_clip_extent = active_clip_.width > 0 && active_clip_.height > 0;
	const MotionRegionFillDecision decision = EvaluateMotionRegionFill(input);
	if (!decision.execute)
		return; // Normative predicate says this call emits no work.

	const uint32_t old_active = legacy_state_.motion_object_active;
	const uint32_t old_id = legacy_state_.motion_object_id;
	const uint32_t old_flags = legacy_state_.motion_flags;
	legacy_state_.motion_object_active = 1;
	legacy_state_.motion_object_id = decision.protective_object_id;
	legacy_state_.motion_flags = kMotionObjectFlagForceCapture;
	BaseVertex vertices[4] = {};
	const float left = static_cast<float>(active_clip_.x);
	const float top = static_cast<float>(active_clip_.y);
	const float right = left + active_clip_.width;
	const float bottom = top + active_clip_.height;
	const float xy[4][2] = { {left,top}, {right,top}, {right,bottom}, {left,bottom} };
	for (uint32_t i = 0; i < 4; ++i)
	{
		vertices[i].position[0] = xy[i][0];
		vertices[i].position[1] = xy[i][1];
		vertices[i].rgba8 = 0xffffffffu;
	}
	const uint32_t indices[6] = { 0, 1, 2, 0, 2, 3 };
	RenderCaptureSegment *capture = Capture("FillMotionVectorRegion");
	CapturedMaterial material = {};
	CapturedShaderRasterState state = {};
	MrtDrawRoutingDecision routing = {};
	if (capture && BuildMaterial(-1, MAP_TYPE_UNKNOWN, &material) &&
		BuildDrawState(PrimitiveSourceKind::Editor,
			RENDERER_DRAW_CALL_PRIMITIVE, RENDERER_DRAW_CALL_3D_OTHER,
			&state, &routing))
	{
		state.mrt_write_mask = decision.write_mask;
		state.shader.shader_flags &= ~(kShaderTextured | kShaderMotionWrite);
		state.shader.shader_flags |= kShaderObjectIdWrite;
		state.shader.motion_object_id = decision.protective_object_id;
		CapturedTransform transform = {};
		Identity(transform.current_model);
		Identity(transform.previous_model);
		const StateId state_id = capture->InternState(state);
		const MaterialRef material_id = capture->InternMaterial(material);
		const TransformId transform_id = capture->InternTransform(transform);
		const StreamGeometryRef geometry = capture->CopyStreamGeometry(vertices, 4,
			indices, 6, nullptr, 0, DepthInterpretation::Irrelevant);
		if (state_id == kInvalidId || material_id == kInvalidId ||
			transform_id == kInvalidId || geometry.vertices.offset == kInvalidId)
		{
			Fail(RuntimeFailure::ResourceExhausted, "FillMotionVectorRegion");
			legacy_state_.motion_object_active = old_active;
			legacy_state_.motion_object_id = old_id;
			legacy_state_.motion_flags = old_flags;
			return;
		}
		CaptureCommand command = {};
		command.schema_version = kCaptureSchemaVersion;
		command.type = CaptureCommandType::DrawStream;
		command.payload.draw_stream.geometry = geometry;
		command.payload.draw_stream.state = state_id;
		command.payload.draw_stream.transform = transform_id;
		command.payload.draw_stream.material = material_id;
		command.payload.draw_stream.optional_payload = kInvalidId;
		command.payload.draw_stream.view = active_view_;
		command.payload.draw_stream.classification = {
			RENDERER_DRAW_CALL_PRIMITIVE, RENDERER_DRAW_CALL_3D_OTHER,
			PrimitiveSourceKind::Editor, 0 };
		if (Append(command, "FillMotionVectorRegion"))
		{
			current_stats_.poly_count += 2;
			current_stats_.vert_count += 4;
		}
	}
	legacy_state_.motion_object_active = old_active;
	legacy_state_.motion_object_id = old_id;
	legacy_state_.motion_flags = old_flags;
}

bool VulkanRenderer::GetMotionVectorSample(const vector *current_world,
	const vector *previous_world, float *current_u, float *current_v,
	float *velocity_u, float *velocity_v)
{
	if (!runtime_ || !current_world || !previous_world || !current_u ||
		!current_v || !velocity_u || !velocity_v)
	{
		Fail(RuntimeFailure::InvalidArgument, "GetMotionVectorSample");
		return false;
	}
	const float current[3] = { current_world->x, current_world->y,
		current_world->z };
	const float previous[3] = { previous_world->x, previous_world->y,
		previous_world->z };
	float result[4] = {};
	if (!runtime_->QueryMotionVectorSample(current, previous, result))
		return false;
	*current_u = result[0];
	*current_v = result[1];
	*velocity_u = result[2];
	*velocity_v = result[3];
	return true;
}

void VulkanRenderer::SetAOSuppression(float value)
{
	if (!std::isfinite(value))
	{
		Fail(RuntimeFailure::InvalidArgument, "SetAOSuppression");
		return;
	}
	legacy_state_.ao_suppression = ClampUnit(value);
}

void VulkanRenderer::SetBloomSuppression(float value)
{
	if (!std::isfinite(value))
	{
		Fail(RuntimeFailure::InvalidArgument, "SetBloomSuppression");
		return;
	}
	legacy_state_.bloom_suppression = ClampUnit(value);
}

void VulkanRenderer::SetAOClass(int value)
{
	legacy_state_.ao_class = static_cast<uint32_t>(ClampValue(value,
		static_cast<int>(RENDERER_AO_CLASS_DEFAULT),
		static_cast<int>(RENDERER_AO_CLASS_MINE)));
	legacy_state_.ao_weight = AOWeight(legacy_state_.ao_class);
}

void VulkanRenderer::SetPostMaskOnly(int state)
{
	FlushTextLayer();
	LiteralAttachmentMasks masks = {};
	for (uint32_t i = 0; i < 5; ++i)
		masks.rgba[i] = legacy_state_.attachment_color_masks[i];
	const PostMaskOnlyTransitionDecision decision =
		EvaluatePostMaskOnlyTransition(legacy_state_.post_mask_only, state, masks);
	legacy_state_.post_mask_only = decision.resulting_enabled;
	for (uint32_t i = 0; i < 5; ++i)
		legacy_state_.attachment_color_masks[i] = decision.masks.rgba[i];
}

void VulkanRenderer::SetSoftParticleState(int state)
{
	legacy_state_.soft_particle_enabled = state != 0;
}

void VulkanRenderer::NotifyDepthBufferWrite()
{
	if (frame_interval_open_ && active_target_ == RenderTargetClass::Scene &&
		legacy_state_.depth_test_enabled && legacy_state_.depth_write_enabled)
		AdvanceTargetVersion(false, true, "NotifyDepthBufferWrite");
}

void VulkanRenderer::SetCockpitBackingEffect(
	const renderer_cockpit_backing_effect *effect)
{
	if (!effect)
	{
		cockpit_backing_ = {};
		return;
	}
	const float values[] = { effect->alpha, effect->darkness,
		effect->scanline_strength, effect->scanline_spacing,
		effect->scanline_thickness, effect->scanline_phase };
	for (uint32_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
		if (!std::isfinite(values[i]))
		{
			Fail(RuntimeFailure::InvalidArgument, "SetCockpitBackingEffect");
			return;
		}
	cockpit_backing_.enabled = effect->enabled != 0;
	cockpit_backing_.alpha = ClampUnit(effect->alpha);
	cockpit_backing_.darkness = ClampUnit(effect->darkness);
	cockpit_backing_.scanlines_enabled = effect->scanlines_enabled != 0;
	cockpit_backing_.scanline_strength = effect->scanline_strength;
	cockpit_backing_.scanline_spacing = effect->scanline_spacing;
	cockpit_backing_.scanline_thickness = effect->scanline_thickness;
	cockpit_backing_.scanline_phase = effect->scanline_phase;
}

void VulkanRenderer::DrawScaledBitmap(int x1, int y1, int x2, int y2,
	int bitmap, float u0, float v0, float u1, float v1, int color,
	float *alphas)
{
	g3Point points[4] = {};
	g3Point *point_list[4] = { &points[0], &points[1], &points[2], &points[3] };
	const float positions[4][2] = {
		{ static_cast<float>(x1), static_cast<float>(y1) },
		{ static_cast<float>(x2), static_cast<float>(y1) },
		{ static_cast<float>(x2), static_cast<float>(y2) },
		{ static_cast<float>(x1), static_cast<float>(y2) },
	};
	const float uv[4][2] = { {u0,v0}, {u1,v0}, {u1,v1}, {u0,v1} };
	for (uint32_t i = 0; i < 4; ++i)
	{
		points[i].p3_sx = positions[i][0];
		points[i].p3_sy = positions[i][1];
		points[i].p3_z = 1.0f;
		points[i].p3_u = uv[i][0];
		points[i].p3_v = uv[i][1];
		points[i].p3_l = 1.0f;
		points[i].p3_a = alphas ? alphas[i] : 1.0f;
		points[i].p3_flags = PF_PROJECTED;
		if (color != -1)
		{
			points[i].p3_r = ColorRed(color) / 255.0f;
			points[i].p3_g = ColorGreen(color) / 255.0f;
			points[i].p3_b = ColorBlue(color) / 255.0f;
		}
	}
	SetTextureType(TT_LINEAR);
	DrawPolygon2D(bitmap, point_list, 4);
}

void VulkanRenderer::DrawScaledBitmapWithZ(int x1, int y1, int x2, int y2,
	int bitmap, float u0, float v0, float u1, float v1, float zval,
	int color, float *alphas)
{
	g3Point points[4] = {};
	g3Point *point_list[4] = { &points[0], &points[1], &points[2], &points[3] };
	const float positions[4][2] = {
		{ static_cast<float>(x1), static_cast<float>(y1) },
		{ static_cast<float>(x2), static_cast<float>(y1) },
		{ static_cast<float>(x2), static_cast<float>(y2) },
		{ static_cast<float>(x1), static_cast<float>(y2) },
	};
	const float uv[4][2] = { {u0,v0}, {u1,v0}, {u1,v1}, {u0,v1} };
	for (uint32_t i = 0; i < 4; ++i)
	{
		points[i].p3_sx = positions[i][0];
		points[i].p3_sy = positions[i][1];
		points[i].p3_z = zval;
		points[i].p3_u = uv[i][0];
		points[i].p3_v = uv[i][1];
		points[i].p3_l = 1.0f;
		points[i].p3_a = alphas ? alphas[i] : 1.0f;
		points[i].p3_flags = PF_PROJECTED;
		if (color != -1)
		{
			points[i].p3_r = ColorRed(color) / 255.0f;
			points[i].p3_g = ColorGreen(color) / 255.0f;
			points[i].p3_b = ColorBlue(color) / 255.0f;
		}
	}
	SetTextureType(TT_LINEAR);
	DrawPolygon3D(bitmap, point_list, 4, MAP_TYPE_BITMAP);
}

void VulkanRenderer::DrawChunkedBitmap(chunked_bitmap *chunk, int x, int y,
	ubyte alpha)
{
	if (!chunk || !chunk->bm_array || chunk->w <= 0 || chunk->h <= 0)
	{
		Fail(RuntimeFailure::InvalidArgument, "DrawChunkedBitmap");
		return;
	}
	const int piece_width = bm_w(chunk->bm_array[0], 0);
	const int piece_height = bm_h(chunk->bm_array[0], 0);
	int screen_width = 0, screen_height = 0;
	GetProjectionParameters(&screen_width, &screen_height);
	(void)alpha; // GL4's canonical unscaled chunk path draws opaque pieces.
	SetZBufferState(0);
	for (int row = 0; row < chunk->h; ++row)
		for (int column = 0; column < chunk->w; ++column)
		{
			const int dx = x + piece_width * column;
			const int dy = y + piece_height * row;
			const int width = std::max(0, std::min(piece_width,
				screen_width - dx));
			const int height = std::max(0, std::min(piece_height,
				screen_height - dy));
			if (width == 0 || height == 0)
				continue;
			DrawSimpleBitmap(chunk->bm_array[row * chunk->w + column], dx, dy);
		}
	SetZBufferState(1);
}

void VulkanRenderer::DrawScaledChunkedBitmap(chunked_bitmap *chunk, int x,
	int y, int new_width, int new_height, ubyte alpha)
{
	if (!chunk || !chunk->bm_array || chunk->w <= 0 || chunk->h <= 0 ||
		chunk->pw <= 0 || chunk->ph <= 0 || new_width <= 0 || new_height <= 0)
	{
		Fail(RuntimeFailure::InvalidArgument, "DrawScaledChunkedBitmap");
		return;
	}
	const float scale_x = static_cast<float>(new_width) / chunk->pw;
	const float scale_y = static_cast<float>(new_height) / chunk->ph;
	const int source_piece_width = bm_w(chunk->bm_array[0], 0);
	const int source_piece_height = bm_h(chunk->bm_array[0], 0);
	const int piece_width = std::max(1,
		static_cast<int>(scale_x * source_piece_width));
	const int piece_height = std::max(1,
		static_cast<int>(scale_y * source_piece_height));
	SetOverlayType(OT_NONE);
	SetLighting(LS_NONE);
	SetColorModel(CM_MONO);
	SetZBufferState(0);
	SetAlphaType(AT_CONSTANT_TEXTURE);
	SetAlphaValue(alpha);
	SetWrapType(WT_WRAP);
	for (int row = 0; row < chunk->h; ++row)
		for (int column = 0; column < chunk->w; ++column)
		{
			const int dx = x + piece_width * column;
			const int dy = y + piece_height * row;
			const int width = std::max(0, std::min(piece_width,
				public_state_.screen_width - dx));
			const int height = std::max(0, std::min(piece_height,
				public_state_.screen_height - dy));
			if (width == 0 || height == 0)
				continue;
			DrawScaledBitmap(dx, dy, dx + width, dy + height,
				chunk->bm_array[row * chunk->w + column], 0.0f, 0.0f,
				static_cast<float>(width) / piece_width,
				static_cast<float>(height) / piece_height);
		}
	SetZBufferState(1);
}

void VulkanRenderer::DrawSimpleBitmap(int bitmap, int x, int y)
{
	if (bitmap < 0)
	{
		Fail(RuntimeFailure::InvalidArgument, "DrawSimpleBitmap");
		return;
	}
	SetAlphaType(AT_CONSTANT_TEXTURE);
	SetAlphaValue(255);
	SetLighting(LS_NONE);
	SetColorModel(CM_MONO);
	SetOverlayType(OT_NONE);
	SetFiltering(0);
	DrawScaledBitmap(x, y, x + bm_w(bitmap, 0), y + bm_h(bitmap, 0),
		bitmap, 0.0f, 0.0f, 1.0f, 1.0f);
	SetFiltering(1);
}

void VulkanRenderer::FillRect(ddgr_color color, int x1, int y1, int x2,
	int y2)
{
	if (!frame_interval_open_ || x2 <= x1 || y2 <= y1)
	{
		Fail(!frame_interval_open_ ? RuntimeFailure::InvalidLifecycle :
			RuntimeFailure::InvalidArgument, "FillRect");
		return;
	}
	FlushTextLayer();
	const LogicalRect rect = { x1 + active_clip_.x, y1 + active_clip_.y,
		x2 - x1, y2 - y1 };
	if (AppendColorClear(rect, false, color, 0.0f, kWriteColor, "FillRect"))
		AppendDepthClear(rect, false, "FillRect");
}

void VulkanRenderer::SetPixel(ddgr_color color, int x, int y)
{
	BaseVertex vertex = {};
	vertex.position[0] = static_cast<float>(x);
	vertex.position[1] = static_cast<float>(y);
	vertex.rgba8 = PackRgba(ColorRed(color), ColorGreen(color),
		ColorBlue(color), 255);
	vertex.uv0[0] = primitive_scratch_.tex_coord[0];
	vertex.uv0[1] = primitive_scratch_.tex_coord[1];
	vertex.uv1[0] = primitive_scratch_.tex_coord2[0];
	vertex.uv1[1] = primitive_scratch_.tex_coord2[1];
	primitive_scratch_.position[0] = vertex.position[0];
	primitive_scratch_.position[1] = vertex.position[1];
	primitive_scratch_.position[2] = 0.0f;
	primitive_scratch_.rgba8 = vertex.rgba8;
	EmitStream(&vertex, 1, nullptr, 0, DepthInterpretation::Irrelevant,
		PrimitiveSourceKind::Point, RENDERER_DRAW_CALL_PRIMITIVE,
		RENDERER_DRAW_CALL_3D_OTHER, -1, MAP_TYPE_UNKNOWN, nullptr,
		"SetPixel");
}

ddgr_color VulkanRenderer::GetPixel(int x, int y)
{
	if (!frame_interval_open_)
	{
		Fail(RuntimeFailure::InvalidLifecycle, "GetPixel");
		return 0;
	}
	FlushTextLayer();
	if (++readback_request_serial_ == 0)
		++readback_request_serial_;
	CaptureCommand command = {};
	command.schema_version = kCaptureSchemaVersion;
	command.type = CaptureCommandType::ReadPixel;
	command.payload.read_pixel.source = active_target_ == RenderTargetClass::Scene ?
		ImageSemantic::SceneColor : (active_target_ == RenderTargetClass::PostPresent ?
			ImageSemantic::PostPresent : ImageSemantic::CockpitScene);
	command.payload.read_pixel.x = x;
	command.payload.read_pixel.y = y;
	command.payload.read_pixel.format = ReadbackFormat::RawRgba8;
	command.payload.read_pixel.request = readback_request_serial_;
	if (!Append(command, "GetPixel"))
		return 0;
	ddgr_color color = 0;
	ReadbackCompletion completion = {};
	completion.request = readback_request_serial_;
	completion.destination = ReadbackDestination::CpuColor;
	completion.cpu_bytes = &color;
	completion.cpu_byte_size = sizeof(color);
	completion.bitmap_handle = -1;
	if (!runtime_->CompleteReadback(completion))
	{
		Fail(RuntimeFailure::ReadbackFailed, "GetPixel");
		return 0;
	}
	return color;
}

void VulkanRenderer::SetCharacterParameters(ddgr_color color1,
	ddgr_color color2, ddgr_color color3, ddgr_color color4)
{
	font_colors_[0] = color1;
	font_colors_[1] = color2;
	font_colors_[2] = color3;
	font_colors_[3] = color4;
}

void VulkanRenderer::DrawFontCharacter(int bitmap, int x1, int y1, int x2,
	int y2, float u, float v, float width, float height)
{
	if (!frame_interval_open_ || bitmap < 0 || x2 <= x1 || y2 <= y1)
	{
		Fail(!frame_interval_open_ ? RuntimeFailure::InvalidLifecycle :
			RuntimeFailure::InvalidArgument, "DrawFontCharacter");
		return;
	}
	TextureRequest request = {};
	request.logical_handle = bitmap;
	request.map_type = MAP_TYPE_BITMAP;
	request.role = TextureRole::FontArray;
	request.wrap_type = WT_CLAMP;
	request.filtering = legacy_state_.filtering;
	request.mipping = 0;
	ResolvedTexture texture = {};
	if (!ResolveTexture(request, &texture, "DrawFontCharacter") ||
		texture.array_layer >= texture.version.depth_or_layers ||
		texture.font_bucket > 1)
	{
		Fail(RuntimeFailure::TextureResolutionFailed, "DrawFontCharacter");
		return;
	}
	if (font_glyphs_pending_ && font_texture_width_ != 0 &&
		(font_texture_width_ != texture.version.width ||
		 font_texture_height_ != texture.version.height))
		FlushTextLayer();
	font_texture_width_ = texture.version.width;
	font_texture_height_ = texture.version.height;
	CaptureCommand command = {};
	command.schema_version = kCaptureSchemaVersion;
	command.type = CaptureCommandType::EnqueueFontGlyph;
	EnqueueFontGlyphCommand &glyph = command.payload.enqueue_font_glyph;
	glyph.texture_version = texture.version.id;
	glyph.texture_layer = texture.array_layer;
	const float positions[4][2] = {
		{static_cast<float>(x1),static_cast<float>(y1)},
		{static_cast<float>(x2),static_cast<float>(y1)},
		{static_cast<float>(x2),static_cast<float>(y2)},
		{static_cast<float>(x1),static_cast<float>(y2)},
	};
	const float uv[4][2] = { {u,v}, {u+width,v},
		{u+width,v+height}, {u,v+height} };
	const uint32_t triangle_vertices[6] = { 0, 1, 2, 0, 2, 3 };
	const uint32_t font_rgba = PackRgba(ColorRed(legacy_state_.flat_color),
		ColorGreen(legacy_state_.flat_color), ColorBlue(legacy_state_.flat_color),
		255);
	for (uint32_t i = 0; i < 6; ++i)
	{
		const uint32_t corner = triangle_vertices[i];
		BaseVertex &vertex = glyph.vertices[i];
		vertex.position[0] = positions[corner][0];
		vertex.position[1] = positions[corner][1];
		// GL4's batched font path uses the renderer's current flat color.  The
		// legacy character-parameter gradients are not consumed by that path.
		vertex.rgba8 = font_rgba;
		vertex.uv0[0] = uv[corner][0];
		vertex.uv0[1] = uv[corner][1];
	}
	glyph.rgba8 = glyph.vertices[0].rgba8;
	const AlphaTypeTransitionContract *alpha_contract =
		FindAlphaTypeTransitionContract(legacy_state_.alpha_type);
	const float alpha_byte = alpha_contract &&
		alpha_contract->multiplier_source == AlphaMultiplierSource::Full255 ?
		255.0f : static_cast<float>(legacy_state_.alpha_value);
	glyph.alpha = ClampUnit((alpha_byte / 255.0f) * legacy_state_.alpha_factor);
	glyph.bucket = legacy_state_.blend_class ==
		static_cast<uint32_t>(BlendClass::Saturate) ? 1u : 0u;
	glyph.enqueue_serial = ++font_enqueue_serial_;
	if (Append(command, "DrawFontCharacter"))
		font_glyphs_pending_ = true;
}

void VulkanRenderer::FlushTextLayer()
{
	if (!font_glyphs_pending_)
		return; // Normative empty-font flush emits no command.
	if (active_view_ == kInvalidId)
	{
		Fail(RuntimeFailure::InvalidLifecycle, "FlushTextLayer");
		return;
	}
	CaptureCommand command = {};
	command.schema_version = kCaptureSchemaVersion;
	command.type = CaptureCommandType::FlushFontBatches;
	command.payload.flush_font_batches.target = active_target_;
	command.payload.flush_font_batches.view_state = active_view_;
	command.payload.flush_font_batches.flush_serial = ++font_flush_serial_;
	if (Append(command, "FlushTextLayer"))
	{
		font_glyphs_pending_ = false;
		AdvanceTargetVersion(true, false, "FlushTextLayer");
	}
}

void VulkanRenderer::DrawLine(int x1, int y1, int x2, int y2)
{
	BaseVertex vertices[2] = {};
	vertices[0].position[0] = static_cast<float>(x1 + 1);
	vertices[0].position[1] = static_cast<float>(y1 + 1);
	vertices[1].position[0] = static_cast<float>(x2 + 1);
	vertices[1].position[1] = static_cast<float>(y2 + 1);
	const uint32_t rgba = PackRgba(ColorRed(legacy_state_.flat_color),
		ColorGreen(legacy_state_.flat_color), ColorBlue(legacy_state_.flat_color),
		legacy_state_.alpha_value);
	vertices[0].rgba8 = vertices[1].rgba8 = rgba;
	EmitStream(vertices, 2, nullptr, 0, DepthInterpretation::Irrelevant,
		PrimitiveSourceKind::Line, RENDERER_DRAW_CALL_PRIMITIVE,
		RENDERER_DRAW_CALL_3D_OTHER, -1, MAP_TYPE_UNKNOWN, nullptr, "DrawLine");
}

void VulkanRenderer::FillCircle(ddgr_color, int, int, int)
{
	// Normative hardware-renderer behavior: circles are deliberate no-ops.
}

void VulkanRenderer::DrawCircle(int, int, int)
{
	// Normative hardware-renderer behavior: circles are deliberate no-ops.
}

void VulkanRenderer::DrawSpecialLine(g3Point *p0, g3Point *p1)
{
	if (!p0 || !p1)
	{
		Fail(RuntimeFailure::InvalidArgument, "DrawSpecialLine");
		return;
	}
	g3Point *points[2] = { p0, p1 };
	BaseVertex vertices[2] = {};
	BuildPointVertex(*p0, &vertices[0]);
	BuildPointVertex(*p1, &vertices[1]);
	EmitStream(vertices, 2, nullptr, 0, DepthInterpretation::EyeZLegacyMapped,
		PrimitiveSourceKind::SpecialLine, RENDERER_DRAW_CALL_PRIMITIVE,
		static_cast<uint32_t>(rend_Get3DDrawCallCategory()),
		legacy_state_.bitmap_handle, legacy_state_.map_type, points,
		"DrawSpecialLine");
}

void VulkanRenderer::DrawSpecialLineBatch(const renderer_line_batch_item *items,
	int count)
{
	if (!items || count < 0)
	{
		Fail(RuntimeFailure::InvalidArgument, "DrawSpecialLineBatch");
		return;
	}
	for (int i = 0; i < count; ++i)
		DrawSpecialLine(items[i].p0, items[i].p1);
}

void VulkanRenderer::CopyBitmapToFramebuffer(int bitmap, int x, int y)
{
	if (!framebuffer_copy_state_)
	{
		Fail(RuntimeFailure::InvalidLifecycle, "CopyBitmapToFramebuffer");
		return;
	}
	DrawSimpleBitmap(bitmap, x, y);
}

void VulkanRenderer::SetFrameBufferCopyState(bool state)
{
	framebuffer_copy_state_ = state;
}

void VulkanRenderer::Screenshot(int bitmap)
{
	if (bitmap < 0 || active_clip_.width <= 0 || active_clip_.height <= 0)
	{
		Fail(RuntimeFailure::InvalidArgument, "Screenshot");
		return;
	}
	FlushTextLayer();
	if (++readback_request_serial_ == 0)
		++readback_request_serial_;
	CaptureCommand command = {};
	command.schema_version = kCaptureSchemaVersion;
	command.type = CaptureCommandType::ReadImage;
	command.payload.read_image.source = post_present_pending_ ?
		ImageSemantic::PostPresent : ImageSemantic::SceneColor;
	command.payload.read_image.rect = active_clip_;
	command.payload.read_image.row_order = ReadbackRowOrder::TopDown;
	command.payload.read_image.format = ReadbackFormat::Rgb565;
	command.payload.read_image.request = readback_request_serial_;
	if (!Append(command, "Screenshot"))
		return;
	ReadbackCompletion completion = {};
	completion.request = readback_request_serial_;
	completion.destination = ReadbackDestination::BitmapHandle;
	completion.bitmap_handle = bitmap;
	if (!runtime_->CompleteReadback(completion))
		Fail(RuntimeFailure::ReadbackFailed, "Screenshot");
}

int VulkanRenderer::SaveScreenshotPNG(const char *filename)
{
	if (!filename || !filename[0] || active_clip_.width <= 0 ||
		active_clip_.height <= 0)
	{
		Fail(RuntimeFailure::InvalidArgument, "SaveScreenshotPNG");
		return 0;
	}
	FlushTextLayer();
	if (++readback_request_serial_ == 0)
		++readback_request_serial_;
	CaptureCommand command = {};
	command.schema_version = kCaptureSchemaVersion;
	command.type = CaptureCommandType::ReadImage;
	command.payload.read_image.source = post_present_pending_ ?
		ImageSemantic::PostPresent : ImageSemantic::SceneColor;
	command.payload.read_image.rect = active_clip_;
	command.payload.read_image.row_order = ReadbackRowOrder::TopDown;
	command.payload.read_image.format = ReadbackFormat::Rgb8TopDown;
	command.payload.read_image.request = readback_request_serial_;
	if (!Append(command, "SaveScreenshotPNG"))
		return 0;
	ReadbackCompletion completion = {};
	completion.request = readback_request_serial_;
	completion.destination = ReadbackDestination::PngPath;
	completion.bitmap_handle = -1;
	completion.png_path = filename;
	if (!runtime_->CompleteReadback(completion))
	{
		Fail(RuntimeFailure::ReadbackFailed, "SaveScreenshotPNG");
		return 0;
	}
	return 1;
}

void VulkanRenderer::UpdateCommon(float *projection, float *modelview, int depth)
{
	if (!projection || !modelview || depth < 0 ||
		static_cast<uint32_t>(depth) >= kCommonTransformDepths)
	{
		Fail(RuntimeFailure::InvalidArgument, "UpdateCommon");
		return;
	}
	for (uint32_t i = 0; i < 16; ++i)
		if (!std::isfinite(projection[i]) || !std::isfinite(modelview[i]))
		{
			Fail(RuntimeFailure::InvalidArgument, "UpdateCommon");
			return;
		}
	if (depth == 0)
	{
		std::memcpy(legacy_state_.current_view, modelview,
			sizeof(legacy_state_.current_view));
		std::memcpy(current_view_.projection, projection,
			sizeof(current_view_.projection));
		std::memcpy(current_view_.view, modelview, sizeof(current_view_.view));
		std::memcpy(current_view_.unscaled_view, modelview,
			sizeof(current_view_.unscaled_view));
		Multiply4x4(current_view_.view_projection, projection, modelview);
		current_view_.matrix_scale[0] = Matrix_scale.x;
		current_view_.matrix_scale[1] = Matrix_scale.y;
		current_view_.matrix_scale[2] = Matrix_scale.z;
		if (!Invert4x4(current_view_.inverse_modelview, modelview) ||
			!Invert4x4(current_view_.inverse_view_projection,
				current_view_.view_projection))
		{
			Fail(RuntimeFailure::InvalidArgument, "UpdateCommon.Inverse");
			return;
		}
		// Retained room vertices are world-space, while the frame view already
		// contains the world-to-eye transform.
		Identity(legacy_state_.current_object);
		if (frame_interval_open_)
		{
			RenderCaptureSegment *capture = Capture("UpdateCommon");
			if (!capture ||
				(active_view_ = capture->InternView(current_view_)) == kInvalidId)
			{
				Fail(RuntimeFailure::CaptureRejected, "UpdateCommon.View");
				return;
			}
		}
	}
	else
	{
		// gTransformModelView is view*object at instance depths.  T1 shaders
		// consume frame_view separately, so retain only the object transform.
		Multiply4x4(legacy_state_.current_object,
			current_view_.inverse_modelview, modelview);
	}
	std::memcpy(legacy_state_.previous_object,
		legacy_state_.current_object, sizeof(legacy_state_.previous_object));
	std::memcpy(common_object_transforms_[depth],
		legacy_state_.current_object, sizeof(common_object_transforms_[depth]));
	common_object_transform_valid_[depth] = true;
	SetCommonDepth(depth);
}

void VulkanRenderer::SetCommonDepth(int depth)
{
	if (depth < 0 || static_cast<uint32_t>(depth) >= kCommonTransformDepths)
	{
		Fail(RuntimeFailure::InvalidArgument, "SetCommonDepth");
		return;
	}
	common_depth_ = static_cast<uint32_t>(depth);
	if (common_object_transform_valid_[common_depth_])
	{
		std::memcpy(legacy_state_.current_object,
			common_object_transforms_[common_depth_],
			sizeof(legacy_state_.current_object));
		std::memcpy(legacy_state_.previous_object,
			legacy_state_.current_object,
			sizeof(legacy_state_.previous_object));
	}
}

uint32_t VulkanRenderer::GetPipelineByName(const char *name)
{
	if (!initialized_ || !runtime_ || !name || !name[0])
	{
		Fail(!initialized_ ? RuntimeFailure::NotInitialized :
			RuntimeFailure::InvalidArgument, "GetPipelineByName");
		return 0;
	}
	uint32_t pipeline = 0;
	if (!runtime_->ResolvePipeline(name, &pipeline) || pipeline == 0)
	{
		Fail(RuntimeFailure::PipelineUnavailable, "GetPipelineByName");
		return 0;
	}
	return pipeline;
}

void VulkanRenderer::BindPipeline(uint32_t handle)
{
	if (!initialized_ || !runtime_ || handle == 0 ||
		!runtime_->SelectPipeline(handle))
	{
		Fail(!initialized_ ? RuntimeFailure::NotInitialized :
			RuntimeFailure::PipelineUnavailable, "BindPipeline");
		return;
	}
	selected_pipeline_ = handle;
	legacy_state_.transform_mode = handle;
}

void VulkanRenderer::UpdateSpecular(SpecularBlock *specular)
{
	if (!specular)
	{
		Fail(RuntimeFailure::InvalidArgument, "UpdateSpecular");
		return;
	}
	std::memset(&specular_block_, 0, sizeof(specular_block_));
	specular_block_.count = ClampValue(specular->num_speculars, 0,
		static_cast<int>(kMaxSpecularSources));
	specular_block_.exponent = specular->exponent;
	specular_block_.strength = specular->strength;
	specular_block_.lightmap_mix = specular->lightmap_mix;
	specular_block_.alpha_strength = specular->alpha_strength;
	specular_block_.field_mode = specular->pad0;
	specular_block_.debug_tint = specular->debug_tint;
	specular_block_.debug_authored = specular->debug_authored;
	for (int source = 0; source < specular_block_.count; ++source)
	{
		std::memcpy(specular_block_.sources[source].center,
			specular->speculars[source].bright_center,
			sizeof(specular_block_.sources[source].center));
		std::memcpy(specular_block_.sources[source].color,
			specular->speculars[source].color,
			sizeof(specular_block_.sources[source].color));
	}
	have_specular_block_ = true;
}

void VulkanRenderer::UpdateFogBrightness(RoomBlock *rooms, int room_count)
{
	if (room_count < 0 || (room_count != 0 && !rooms))
	{
		Fail(RuntimeFailure::InvalidArgument, "UpdateFogBrightness");
		return;
	}
	room_aux_.assign(static_cast<size_t>(room_count), GpuWorldAux{});
	for (int room = 0; room < room_count; ++room)
	{
		GpuWorldAux &output = room_aux_[room];
		std::memcpy(output.fog_color, rooms[room].fog_color,
			sizeof(output.fog_color));
		std::memcpy(output.fog_plane, rooms[room].fog_plane,
			sizeof(output.fog_plane));
		output.params[0] = rooms[room].fog_distance;
		output.params[2] = rooms[room].brightness;
		output.params[3] = rooms[room].not_in_room ? 1.0f : 0.0f;
	}
	if (current_room_ >= room_count)
		current_room_ = -1;
}

void VulkanRenderer::SetCurrentRoomNum(int roomblocknum)
{
	if (roomblocknum < 0 || static_cast<size_t>(roomblocknum) >= room_aux_.size())
	{
		Fail(RuntimeFailure::InvalidArgument, "SetCurrentRoomNum");
		return;
	}
	current_room_ = roomblocknum;
}

void VulkanRenderer::UpdateTerrainFog(float color[4], float start, float end)
{
	if (!color || !std::isfinite(start) || !std::isfinite(end))
	{
		Fail(RuntimeFailure::InvalidArgument, "UpdateTerrainFog");
		return;
	}
	for (uint32_t i = 0; i < 4; ++i)
		if (!std::isfinite(color[i]))
		{
			Fail(RuntimeFailure::InvalidArgument, "UpdateTerrainFog");
			return;
		}
	std::memset(&terrain_fog_, 0, sizeof(terrain_fog_));
	std::memcpy(terrain_fog_.fog_color, color, sizeof(float) * 4);
	terrain_fog_.params[0] = start;
	terrain_fog_.params[1] = end;
}

void VulkanRenderer::UseShaderTest()
{
	const uint32_t pipeline = GetPipelineByName("shader-test");
	if (pipeline != 0)
		BindPipeline(pipeline);
}

void VulkanRenderer::EndShaderTest()
{
	RestoreLegacy();
}

void VulkanRenderer::BindBitmap(int handle)
{
	if (handle < 0)
	{
		Fail(RuntimeFailure::InvalidArgument, "BindBitmap");
		return;
	}
	legacy_state_.bitmap_handle = handle;
	legacy_state_.bitmap_version = kInvalidId;
	legacy_state_.map_type = MAP_TYPE_BITMAP;
	texture_shadow_.units[0].logical_handle = handle;
	texture_shadow_.units[0].version = kInvalidId;
	texture_shadow_.units[0].map_type = MAP_TYPE_BITMAP;
	texture_shadow_.last_selected_unit = 0;
}

void VulkanRenderer::BindLightmap(int handle)
{
	if (handle < 0)
	{
		Fail(RuntimeFailure::InvalidArgument, "BindLightmap");
		return;
	}
	SetOverlayMap(handle);
	texture_shadow_.last_selected_unit = 1;
}

void VulkanRenderer::RestoreLegacy()
{
	if (!initialized_ || !runtime_ || !runtime_->SelectPipeline(0))
	{
		Fail(!initialized_ ? RuntimeFailure::NotInitialized :
			RuntimeFailure::PipelineUnavailable, "RestoreLegacy");
		return;
	}
	selected_pipeline_ = 0;
	legacy_state_.transform_mode = 0;
}

void VulkanRenderer::GetScreenSize(int &screen_width, int &screen_height)
{
	screen_width = public_state_.screen_width;
	screen_height = public_state_.screen_height;
}

double VulkanRenderer::GetDisplayRefreshRate()
{
	if (!initialized_ || !runtime_)
	{
		Fail(initialized_ ? RuntimeFailure::MissingRuntime :
			RuntimeFailure::NotInitialized, "GetDisplayRefreshRate");
		return 0.0;
	}
	return runtime_->DisplayRefreshRate();
}

void VulkanRenderer::ClearBoundTextures()
{
	for (uint32_t unit = 0; unit < 4; ++unit)
	{
		texture_shadow_.units[unit].logical_handle = -1;
		texture_shadow_.units[unit].version = kInvalidId;
		texture_shadow_.units[unit].map_type = MAP_TYPE_UNKNOWN;
		texture_shadow_.units[unit].sampler_index = 0;
	}
	texture_shadow_.last_selected_unit = 0;
	legacy_state_.bitmap_handle = -1;
	legacy_state_.bitmap_version = kInvalidId;
	legacy_state_.overlay_handle = -1;
	legacy_state_.overlay_version = kInvalidId;
	legacy_state_.specular_version = kInvalidId;
}
