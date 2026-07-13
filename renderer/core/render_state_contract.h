/* Frozen GL4 sticky-state transition semantics. */
#pragma once

#include "render_contract.h"

namespace piccu
{
namespace render
{

enum class LegacyAlphaType : uint32_t
{
	Always = 0,
	Constant = 1,
	Texture = 2,
	ConstantTexture = 3,
	Vertex = 4,
	ConstantVertex = 5,
	TextureVertex = 6,
	ConstantTextureVertex = 7,
	LightmapBlend = 8,
	SaturateTexture = 9,
	SaturateVertex = 12,
	SaturateConstantVertex = 13,
	SaturateTextureVertex = 14,
	LightmapBlendVertex = 15,
	Specular = 32,
	LightmapBlendSaturate = 33,
};

enum class AlphaMultiplierSource : uint32_t
{
	AlphaByte = 0,
	Full255,
};

struct AlphaTypeTransitionContract
{
	LegacyAlphaType type;
	BlendClass blend_class;
	AlphaMultiplierSource multiplier_source;
	uint32_t force_alpha_byte_255;
	uint32_t force_texture_quality_2_and_perspective;
	uint32_t luminance_post_mask;
};

extern const AlphaTypeTransitionContract kAlphaTypeTransitionContract[];
extern const size_t kAlphaTypeTransitionContractCount;
const AlphaTypeTransitionContract *FindAlphaTypeTransitionContract(uint32_t type);

struct AlphaTypeTransitionInput
{
	uint32_t current_type;
	uint32_t requested_type;
	uint32_t current_alpha_byte;
	uint32_t active_texture_unit;
};

struct AlphaTypeTransitionDecision
{
	uint32_t valid_type;
	uint32_t redundant_early_return;
	uint32_t select_texture_unit_zero;
	uint32_t resulting_alpha_byte;
	uint32_t resulting_alpha_multiplier;
	BlendClass blend_class;
	uint32_t force_texture_quality_2;
	uint32_t force_texture_type_perspective;
	uint32_t store_type;
	uint32_t mark_shader_state_dirty;
	uint32_t configure_auxiliary_blend_masks;
	uint32_t luminance_post_mask;
};

AlphaTypeTransitionDecision EvaluateAlphaTypeTransition(
	const AlphaTypeTransitionInput &input);

struct LiteralAttachmentMasks
{
	uint32_t rgba[5];
};

// GL4 owns these as two independent pieces of sticky state.  In particular,
// post_mask_only is not sufficient to reconstruct either member.
struct LiteralMrtState
{
	uint32_t selected_draw_buffers;
	LiteralAttachmentMasks attachment_masks;
};

constexpr uint32_t kDefaultSceneSelectedDrawBuffers =
	kWriteColor | kWriteProtectionMask | kWriteAoClass;
constexpr uint32_t kColorOnlySelectedDrawBuffers = kWriteColor;

struct PostMaskOnlyTransitionDecision
{
	uint32_t flush_fonts_before_compare;
	uint32_t redundant_after_flush;
	uint32_t resulting_enabled;
	LiteralAttachmentMasks masks;
};

PostMaskOnlyTransitionDecision EvaluatePostMaskOnlyTransition(
	uint32_t currently_enabled, int32_t requested,
	const LiteralAttachmentMasks &current_masks);
LiteralAttachmentMasks ApplyConfigurePostMaskBlend(
	const LiteralAttachmentMasks &current_masks);

enum class MotionAlphaRule : uint32_t
{
	Never = 0,
	Always,
	Alpha255OrObjectAttached,
	OpaquePolyobjectOrCockpitOrObjectAttached,
	OpaquePolyobjectOrCockpitFlatOrObjectAttached,
	ObjectAttached,
};

struct MotionAlphaPredicateContract
{
	LegacyAlphaType alpha_type;
	MotionAlphaRule rule;
};

extern const MotionAlphaPredicateContract kMotionAlphaPredicateContract[];
extern const size_t kMotionAlphaPredicateContractCount;
const MotionAlphaPredicateContract *FindMotionAlphaPredicateContract(uint32_t type);

constexpr uint32_t kLegacyAoClassPolyobject = 2;
constexpr uint32_t kLegacyTextureTypeFlat = 0;
constexpr uint32_t kMotionObjectLegacyBlurMask = 0x80000000u;
constexpr uint32_t kMotionObjectFlagLegacyBlur = 1u << 0;
constexpr uint32_t kMotionObjectFlagForceCapture = 1u << 1;

enum MotionWriteBlockBits : uint32_t
{
	kMotionBlockedBySuppression = 1u << 0,
	kMotionBlockedByTarget = 1u << 1,
	kMotionBlockedByFrozenHistory = 1u << 2,
	kMotionBlockedByPostPresent = 1u << 3,
	kMotionBlockedByDepthTest = 1u << 4,
	kMotionBlockedByInactiveObject = 1u << 5,
	kMotionBlockedByCaptureLock = 1u << 6,
	kMotionBlockedByAlphaRule = 1u << 7,
};

struct MotionWritePredicateInput
{
	uint32_t suppression_depth;
	uint32_t pixel_target_enabled;
	uint32_t frozen;
	uint32_t post_present_pending;
	uint32_t zbuffer_enabled;
	uint32_t motion_object_active;
	uint32_t capture_locked;
	uint32_t cockpit_draw;
	uint32_t ao_class;
	uint32_t alpha_type;
	uint32_t alpha_value;
	uint32_t force_capture;
	uint32_t texture_type;
	uint32_t motion_object_id;
	uint32_t cockpit_motion_scope_active;
};

struct MotionWritePredicateDecision
{
	uint32_t motion_write;
	uint32_t object_id_write;
	uint32_t late_cockpit_draw_buffer_override;
	uint32_t opaque_polyobject_or_cockpit_vertex_alpha;
	uint32_t object_attached;
	MotionAlphaRule alpha_rule;
	uint32_t block_bits;
};

MotionWritePredicateDecision EvaluateMotionWritePredicate(
	const MotionWritePredicateInput &input);

struct MotionObjectBeginInput
{
	int32_t object_handle;
	uint32_t motion_object_flags;
	uint32_t framebuffer_available;
	uint32_t post_present_pending;
	uint32_t capture_locked;
	uint32_t cockpit_draw;
	uint32_t pixel_consumer_active;
	uint32_t velocity_texture_available;
};

struct MotionObjectScopeState
{
	uint32_t active;
	uint32_t cockpit_active;
	uint32_t force_capture;
	uint32_t object_id;
};

MotionObjectScopeState EvaluateBeginMotionObject(
	const MotionObjectBeginInput &input);

struct MotionObjectEndDecision
{
	MotionObjectScopeState resulting_scope;
	uint32_t capture_cockpit_previous_view_projection;
};

MotionObjectEndDecision EvaluateEndMotionObject(
	const MotionObjectScopeState &current_scope,
	uint32_t current_view_projection_valid);

enum class MotionSuppressionOperation : uint32_t
{
	Suspend = 0,
	Resume,
};

struct MotionSuppressionTransitionDecision
{
	uint32_t resulting_depth;
	uint32_t mark_shader_state_dirty;
	uint32_t unmatched_resume;
};

MotionSuppressionTransitionDecision EvaluateMotionSuppressionTransition(
	uint32_t current_depth, MotionSuppressionOperation operation,
	uint32_t pixel_motion_mode_enabled);

enum class MotionCaptureLockEvent : uint32_t
{
	CaptureWorldForLatePost = 0,
	PresentNextFramebuffer,
	FramebufferRebuild,
};

struct MotionCaptureLockTransitionInput
{
	uint32_t currently_locked;
	MotionCaptureLockEvent event;
	uint32_t pixel_target_enabled;
	uint32_t frozen;
};

struct MotionCaptureLockTransitionDecision
{
	uint32_t resulting_locked;
	uint32_t changed;
};

MotionCaptureLockTransitionDecision EvaluateMotionCaptureLockTransition(
	const MotionCaptureLockTransitionInput &input);

struct MotionRegionFillInput
{
	uint32_t pixel_motion_mode_enabled;
	uint32_t frozen;
	uint32_t post_present_pending;
	uint32_t framebuffer_available;
	uint32_t velocity_texture_available;
	uint32_t positive_clip_extent;
	int32_t object_handle;
};

struct MotionRegionFillDecision
{
	uint32_t execute;
	uint32_t flush_fonts_before_predicate;
	uint32_t write_mask;
	uint32_t protective_object_id;
};

MotionRegionFillDecision EvaluateMotionRegionFill(
	const MotionRegionFillInput &input);

enum class MrtDrawKind : uint32_t
{
	Polygon = 0,
	Primitive,
	FontFlush,
	SmallViewProtectionFill,
};

struct MrtDrawRoutingInput
{
	LiteralMrtState literal_state;
	MrtDrawKind draw_kind;
	RenderTargetClass target;
	uint32_t drawing_to_scene_framebuffer;
	uint32_t cockpit_scene_frame_active;
	uint32_t pixel_motion_mode_enabled;
	uint32_t zbuffer_enabled;
	uint32_t capture_locked;
	uint32_t cockpit_draw;
	uint32_t motion_write;
	uint32_t object_id_write;
};

struct MrtDrawRoutingDecision
{
	LiteralMrtState state_for_draw;
	LiteralMrtState state_after_draw;
	uint32_t logical_write_mask;
	uint32_t override_draw_buffers;
	uint32_t late_cockpit_draw_buffer_override;
	uint32_t legal_write_mask;
};

LiteralMrtState DefaultSceneMrtState();
bool IsLiteralMrtStateValid(const LiteralMrtState &state);
uint32_t DeriveLiteralMrtWriteMask(const LiteralMrtState &state);
MrtDrawRoutingDecision EvaluateMrtDrawRouting(
	const MrtDrawRoutingInput &input);

struct ZBiasTransitionInput
{
	float current;
	float requested;
};

struct ZBiasTransitionDecision
{
	float resulting;
	uint32_t changed;
	uint32_t affects_depth_and_perspective_w;
	uint32_t uses_polygon_offset;
	uint32_t marks_shader_state_dirty;
};

ZBiasTransitionDecision EvaluateZBiasTransition(
	const ZBiasTransitionInput &input);

enum class LegacySetterFamily : uint32_t
{
	AlphaType = 0,
	PostMaskOnly,
	FogState,
	Lighting,
	AoClass,
	AoSuppression,
	BloomSuppression,
	SpecularMode,
	SoftParticle,
	CockpitBacking,
	ColorModel,
	TextureType,
	Filtering,
	ZTest,
	OverlayMap,
	OverlayType,
	AlphaValue,
	AlphaFactor,
	FlatColor,
	FogBorders,
	FogColor,
	DynamicLightDirection,
	DynamicLightMap,
	Wrap,
	Mip,
	ZValues,
	ZBias,
	ZWrite,
	CoplanarOffset,
	Cull,
	DynamicLighting,
	Count,
};

struct SetterTransitionContract
{
	LegacySetterFamily family;
	uint32_t normalize_or_clamp_before_compare;
	uint32_t equality_returns_before_side_effects;
	uint32_t flush_fonts_before_compare;
	uint32_t truthy_marks_protection_dirty_before_compare;
	uint32_t always_assigns;
};

extern const SetterTransitionContract kSetterTransitionContract[];
extern const size_t kSetterTransitionContractCount;

struct LegacyPartialRestoreContract
{
	uint32_t restoring_alpha_type_does_not_restore_alpha_byte;
	uint32_t leaving_specular_does_not_restore_texture_state;
	uint32_t post_mask_boolean_does_not_derive_literal_masks;
	uint32_t helper_restores_only_explicitly_set_fields;
};

constexpr LegacyPartialRestoreContract kLegacyPartialRestoreContract =
	{ 1, 1, 1, 1 };

} // namespace render
} // namespace piccu
