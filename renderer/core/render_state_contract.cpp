#include "render_state_contract.h"

namespace piccu
{
namespace render
{

const AlphaTypeTransitionContract kAlphaTypeTransitionContract[] = {
	{ LegacyAlphaType::Always, BlendClass::Opaque, AlphaMultiplierSource::Full255, 1, 0, 0 },
	{ LegacyAlphaType::Constant, BlendClass::Alpha, AlphaMultiplierSource::AlphaByte, 0, 0, 0 },
	{ LegacyAlphaType::Texture, BlendClass::Opaque, AlphaMultiplierSource::Full255, 1, 0, 0 },
	{ LegacyAlphaType::ConstantTexture, BlendClass::Alpha, AlphaMultiplierSource::AlphaByte, 0, 0, 0 },
	{ LegacyAlphaType::Vertex, BlendClass::Alpha, AlphaMultiplierSource::Full255, 0, 0, 0 },
	{ LegacyAlphaType::ConstantVertex, BlendClass::Alpha, AlphaMultiplierSource::AlphaByte, 0, 0, 0 },
	{ LegacyAlphaType::TextureVertex, BlendClass::Alpha, AlphaMultiplierSource::Full255, 0, 0, 0 },
	{ LegacyAlphaType::ConstantTextureVertex, BlendClass::Alpha, AlphaMultiplierSource::AlphaByte, 0, 0, 0 },
	{ LegacyAlphaType::LightmapBlend, BlendClass::Multiply, AlphaMultiplierSource::AlphaByte, 0, 0, 0 },
	{ LegacyAlphaType::SaturateTexture, BlendClass::Saturate, AlphaMultiplierSource::AlphaByte, 0, 0, 1 },
	{ LegacyAlphaType::SaturateVertex, BlendClass::Saturate, AlphaMultiplierSource::Full255, 0, 0, 1 },
	{ LegacyAlphaType::SaturateConstantVertex, BlendClass::Saturate, AlphaMultiplierSource::AlphaByte, 0, 0, 1 },
	{ LegacyAlphaType::SaturateTextureVertex, BlendClass::Saturate, AlphaMultiplierSource::Full255, 0, 0, 1 },
	{ LegacyAlphaType::LightmapBlendVertex, BlendClass::Multiply, AlphaMultiplierSource::Full255, 0, 0, 0 },
	{ LegacyAlphaType::Specular, BlendClass::Saturate, AlphaMultiplierSource::Full255, 0, 1, 0 },
	{ LegacyAlphaType::LightmapBlendSaturate, BlendClass::Saturate, AlphaMultiplierSource::AlphaByte, 0, 0, 1 },
};

const size_t kAlphaTypeTransitionContractCount =
	sizeof(kAlphaTypeTransitionContract) / sizeof(kAlphaTypeTransitionContract[0]);

const AlphaTypeTransitionContract *FindAlphaTypeTransitionContract(uint32_t type)
{
	for (size_t i = 0; i < kAlphaTypeTransitionContractCount; ++i)
		if (static_cast<uint32_t>(kAlphaTypeTransitionContract[i].type) == type)
			return &kAlphaTypeTransitionContract[i];
	return nullptr;
}

AlphaTypeTransitionDecision EvaluateAlphaTypeTransition(
	const AlphaTypeTransitionInput &input)
{
	AlphaTypeTransitionDecision decision = {};
	decision.resulting_alpha_byte = input.current_alpha_byte & 0xffu;
	const AlphaTypeTransitionContract *contract =
		FindAlphaTypeTransitionContract(input.requested_type);
	if (contract == nullptr)
		return decision;
	decision.valid_type = 1;
	decision.blend_class = contract->blend_class;
	decision.luminance_post_mask = contract->luminance_post_mask;
	if (input.current_type == input.requested_type)
	{
		decision.redundant_early_return = 1;
		decision.resulting_alpha_multiplier =
			contract->multiplier_source == AlphaMultiplierSource::Full255 ?
			255 : decision.resulting_alpha_byte;
		return decision;
	}
	decision.select_texture_unit_zero = input.active_texture_unit != 0;
	if (contract->force_alpha_byte_255)
		decision.resulting_alpha_byte = 255;
	decision.resulting_alpha_multiplier =
		contract->multiplier_source == AlphaMultiplierSource::Full255 ?
		255 : decision.resulting_alpha_byte;
	decision.force_texture_quality_2 = contract->force_texture_quality_2_and_perspective;
	decision.force_texture_type_perspective = contract->force_texture_quality_2_and_perspective;
	decision.store_type = 1;
	decision.mark_shader_state_dirty = 1;
	decision.configure_auxiliary_blend_masks = 1;
	return decision;
}

PostMaskOnlyTransitionDecision EvaluatePostMaskOnlyTransition(
	uint32_t currently_enabled, int32_t requested,
	const LiteralAttachmentMasks &current_masks)
{
	PostMaskOnlyTransitionDecision decision = {};
	decision.flush_fonts_before_compare = 1;
	decision.resulting_enabled = requested != 0;
	decision.masks = current_masks;
	if ((currently_enabled != 0) == (requested != 0))
	{
		decision.redundant_after_flush = 1;
		return decision;
	}
	if (requested != 0)
	{
		decision.masks.rgba[0] = 0;
		decision.masks.rgba[1] = 0;
		decision.masks.rgba[2] = kChannelRgba;
		decision.masks.rgba[3] = 0;
	}
	else
	{
		for (uint32_t i = 0; i < 4; ++i)
			decision.masks.rgba[i] = kChannelRgba;
	}
	return decision;
}

LiteralAttachmentMasks ApplyConfigurePostMaskBlend(
	const LiteralAttachmentMasks &current_masks)
{
	LiteralAttachmentMasks result = current_masks;
	result.rgba[1] = kChannelRgba;
	result.rgba[3] = kChannelRgba;
	result.rgba[4] = kChannelRgba;
	return result;
}

const MotionAlphaPredicateContract kMotionAlphaPredicateContract[] = {
	{ LegacyAlphaType::Always, MotionAlphaRule::Always },
	{ LegacyAlphaType::Constant, MotionAlphaRule::Alpha255OrObjectAttached },
	{ LegacyAlphaType::Texture, MotionAlphaRule::Always },
	{ LegacyAlphaType::ConstantTexture, MotionAlphaRule::Alpha255OrObjectAttached },
	{ LegacyAlphaType::Vertex, MotionAlphaRule::ObjectAttached },
	{ LegacyAlphaType::ConstantVertex,
		MotionAlphaRule::OpaquePolyobjectOrCockpitFlatOrObjectAttached },
	{ LegacyAlphaType::TextureVertex,
		MotionAlphaRule::OpaquePolyobjectOrCockpitOrObjectAttached },
	{ LegacyAlphaType::ConstantTextureVertex, MotionAlphaRule::Never },
	{ LegacyAlphaType::LightmapBlend, MotionAlphaRule::Never },
	{ LegacyAlphaType::SaturateTexture, MotionAlphaRule::ObjectAttached },
	{ LegacyAlphaType::SaturateVertex, MotionAlphaRule::ObjectAttached },
	{ LegacyAlphaType::SaturateConstantVertex, MotionAlphaRule::ObjectAttached },
	{ LegacyAlphaType::SaturateTextureVertex, MotionAlphaRule::ObjectAttached },
	{ LegacyAlphaType::LightmapBlendVertex, MotionAlphaRule::Never },
	{ LegacyAlphaType::Specular, MotionAlphaRule::Never },
	{ LegacyAlphaType::LightmapBlendSaturate, MotionAlphaRule::Never },
};

const size_t kMotionAlphaPredicateContractCount =
	sizeof(kMotionAlphaPredicateContract) /
	sizeof(kMotionAlphaPredicateContract[0]);

static_assert(sizeof(kMotionAlphaPredicateContract) /
	sizeof(kMotionAlphaPredicateContract[0]) ==
	sizeof(kAlphaTypeTransitionContract) /
	sizeof(kAlphaTypeTransitionContract[0]),
	"motion alpha predicate inventory must cover every handled alpha type");
static_assert(sizeof(kLegalMrtWriteMasks) / sizeof(kLegalMrtWriteMasks[0]) == 8,
	"the frozen MRT matrix has exactly eight logical write masks");

const MotionAlphaPredicateContract *FindMotionAlphaPredicateContract(uint32_t type)
{
	for (size_t i = 0; i < kMotionAlphaPredicateContractCount; ++i)
		if (static_cast<uint32_t>(kMotionAlphaPredicateContract[i].alpha_type) == type)
			return &kMotionAlphaPredicateContract[i];
	return nullptr;
}

MotionWritePredicateDecision EvaluateMotionWritePredicate(
	const MotionWritePredicateInput &input)
{
	MotionWritePredicateDecision decision = {};
	decision.late_cockpit_draw_buffer_override =
		input.pixel_target_enabled != 0 && input.zbuffer_enabled != 0 &&
		input.capture_locked != 0 && input.cockpit_draw != 0;
	decision.opaque_polyobject_or_cockpit_vertex_alpha =
		(input.ao_class == kLegacyAoClassPolyobject || input.cockpit_draw != 0) &&
		input.alpha_value == 255;
	decision.object_attached = input.force_capture != 0 ||
		(input.ao_class == kLegacyAoClassPolyobject &&
		 input.motion_object_active != 0);

	const MotionAlphaPredicateContract *alpha_contract =
		FindMotionAlphaPredicateContract(input.alpha_type);
	decision.alpha_rule = alpha_contract != nullptr ? alpha_contract->rule :
		MotionAlphaRule::Never;

	if (input.suppression_depth > 0)
		decision.block_bits |= kMotionBlockedBySuppression;
	if (input.pixel_target_enabled == 0)
		decision.block_bits |= kMotionBlockedByTarget;
	if (input.frozen != 0)
		decision.block_bits |= kMotionBlockedByFrozenHistory;
	if (input.post_present_pending != 0)
		decision.block_bits |= kMotionBlockedByPostPresent;
	if (input.zbuffer_enabled == 0)
		decision.block_bits |= kMotionBlockedByDepthTest;
	if (input.motion_object_active == 0)
		decision.block_bits |= kMotionBlockedByInactiveObject;
	if (input.capture_locked != 0 && input.cockpit_draw == 0)
		decision.block_bits |= kMotionBlockedByCaptureLock;

	bool alpha_eligible = false;
	switch (decision.alpha_rule)
	{
	case MotionAlphaRule::Always:
		alpha_eligible = true;
		break;
	case MotionAlphaRule::Alpha255OrObjectAttached:
		alpha_eligible = input.alpha_value == 255 || decision.object_attached != 0;
		break;
	case MotionAlphaRule::OpaquePolyobjectOrCockpitOrObjectAttached:
		alpha_eligible = decision.opaque_polyobject_or_cockpit_vertex_alpha != 0 ||
			decision.object_attached != 0;
		break;
	case MotionAlphaRule::OpaquePolyobjectOrCockpitFlatOrObjectAttached:
		alpha_eligible =
			(decision.opaque_polyobject_or_cockpit_vertex_alpha != 0 &&
			 input.texture_type == kLegacyTextureTypeFlat) ||
			decision.object_attached != 0;
		break;
	case MotionAlphaRule::ObjectAttached:
		alpha_eligible = decision.object_attached != 0;
		break;
	case MotionAlphaRule::Never:
	default:
		break;
	}
	if (!alpha_eligible)
		decision.block_bits |= kMotionBlockedByAlphaRule;

	decision.motion_write = decision.block_bits == 0;
	decision.object_id_write = decision.motion_write != 0 &&
		input.motion_object_id != 0 && input.cockpit_motion_scope_active == 0;
	return decision;
}

MotionObjectScopeState EvaluateBeginMotionObject(
	const MotionObjectBeginInput &input)
{
	MotionObjectScopeState result = {};
	result.active = input.object_handle >= 0 && input.framebuffer_available != 0 &&
		input.post_present_pending == 0 &&
		(input.capture_locked == 0 || input.cockpit_draw != 0) &&
		input.pixel_consumer_active != 0 && input.velocity_texture_available != 0;
	if (result.active == 0)
		return result;
	if (input.cockpit_draw != 0)
	{
		result.cockpit_active = 1;
		return result;
	}
	result.force_capture =
		(input.motion_object_flags & kMotionObjectFlagForceCapture) != 0;
	result.object_id = (static_cast<uint32_t>(input.object_handle) + 1u) &
		~kMotionObjectLegacyBlurMask;
	if ((input.motion_object_flags & kMotionObjectFlagLegacyBlur) != 0)
		result.object_id |= kMotionObjectLegacyBlurMask;
	return result;
}

MotionObjectEndDecision EvaluateEndMotionObject(
	const MotionObjectScopeState &current_scope,
	uint32_t current_view_projection_valid)
{
	MotionObjectEndDecision decision = {};
	decision.capture_cockpit_previous_view_projection =
		current_scope.cockpit_active != 0 && current_view_projection_valid != 0;
	return decision;
}

MotionSuppressionTransitionDecision EvaluateMotionSuppressionTransition(
	uint32_t current_depth, MotionSuppressionOperation operation,
	uint32_t pixel_motion_mode_enabled)
{
	MotionSuppressionTransitionDecision decision = {};
	decision.resulting_depth = current_depth;
	decision.mark_shader_state_dirty = pixel_motion_mode_enabled != 0;
	if (operation == MotionSuppressionOperation::Suspend)
		decision.resulting_depth = current_depth + 1u;
	else if (current_depth > 0)
		decision.resulting_depth = current_depth - 1u;
	else
		decision.unmatched_resume = 1;
	return decision;
}

MotionCaptureLockTransitionDecision EvaluateMotionCaptureLockTransition(
	const MotionCaptureLockTransitionInput &input)
{
	MotionCaptureLockTransitionDecision decision = {};
	decision.resulting_locked = input.currently_locked != 0;
	switch (input.event)
	{
	case MotionCaptureLockEvent::CaptureWorldForLatePost:
		if (input.pixel_target_enabled != 0 && input.frozen == 0)
			decision.resulting_locked = 1;
		break;
	case MotionCaptureLockEvent::PresentNextFramebuffer:
	case MotionCaptureLockEvent::FramebufferRebuild:
		decision.resulting_locked = 0;
		break;
	}
	decision.changed =
		decision.resulting_locked != (input.currently_locked != 0);
	return decision;
}

MotionRegionFillDecision EvaluateMotionRegionFill(
	const MotionRegionFillInput &input)
{
	MotionRegionFillDecision decision = {};
	decision.flush_fonts_before_predicate = 1;
	decision.execute = input.pixel_motion_mode_enabled != 0 && input.frozen == 0 &&
		input.post_present_pending == 0 && input.framebuffer_available != 0 &&
		input.velocity_texture_available != 0 && input.positive_clip_extent != 0;
	if (decision.execute != 0)
	{
		decision.write_mask = kWriteVelocity | kWriteObjectId;
		decision.protective_object_id =
			(static_cast<uint32_t>(input.object_handle) + 1u) &
			~kMotionObjectLegacyBlurMask;
		if (decision.protective_object_id == 0)
			decision.protective_object_id = 1;
	}
	return decision;
}

LiteralMrtState DefaultSceneMrtState()
{
	LiteralMrtState state = {};
	state.selected_draw_buffers = kDefaultSceneSelectedDrawBuffers;
	for (size_t i = 0; i < 5; ++i)
		state.attachment_masks.rgba[i] = kChannelRgba;
	return state;
}

bool IsLiteralMrtStateValid(const LiteralMrtState &state)
{
	constexpr uint32_t all_attachments = (1u << 5) - 1u;
	if ((state.selected_draw_buffers & ~all_attachments) != 0)
		return false;
	for (size_t i = 0; i < 5; ++i)
		if ((state.attachment_masks.rgba[i] & ~kChannelRgba) != 0)
			return false;
	return true;
}

uint32_t DeriveLiteralMrtWriteMask(const LiteralMrtState &state)
{
	uint32_t result = 0;
	for (uint32_t location = 0; location < 5; ++location)
	{
		const uint32_t bit = 1u << location;
		if ((state.selected_draw_buffers & bit) != 0 &&
			(state.attachment_masks.rgba[location] & kChannelRgba) != 0)
			result |= bit;
	}
	return result;
}

static void ConfigureAuxiliaryMrtMasks(LiteralMrtState *state)
{
	state->attachment_masks =
		ApplyConfigurePostMaskBlend(state->attachment_masks);
}

MrtDrawRoutingDecision EvaluateMrtDrawRouting(
	const MrtDrawRoutingInput &input)
{
	MrtDrawRoutingDecision decision = {};
	decision.state_for_draw = input.literal_state;
	decision.state_after_draw = input.literal_state;

	if (input.draw_kind == MrtDrawKind::SmallViewProtectionFill)
	{
		decision.logical_write_mask = kWriteVelocity | kWriteObjectId;
		decision.legal_write_mask = 1;
		if (input.target == RenderTargetClass::Scene &&
			input.drawing_to_scene_framebuffer != 0)
		{
			decision.state_after_draw.selected_draw_buffers =
				kDefaultSceneSelectedDrawBuffers;
			ConfigureAuxiliaryMrtMasks(&decision.state_after_draw);
		}
		return decision;
	}

	if (input.target == RenderTargetClass::PostPresent)
	{
		decision.state_for_draw.selected_draw_buffers = kColorOnlySelectedDrawBuffers;
		decision.state_after_draw = decision.state_for_draw;
	}
	else if (input.target == RenderTargetClass::CockpitScene ||
		input.cockpit_scene_frame_active != 0)
	{
		decision.override_draw_buffers = input.draw_kind == MrtDrawKind::Polygon &&
			input.drawing_to_scene_framebuffer != 0;
		decision.state_for_draw.selected_draw_buffers = kColorOnlySelectedDrawBuffers;
		decision.state_after_draw = decision.state_for_draw;
	}
	else if (input.draw_kind == MrtDrawKind::FontFlush &&
		input.drawing_to_scene_framebuffer != 0 && input.zbuffer_enabled == 0)
	{
		decision.override_draw_buffers = 1;
		decision.state_for_draw.selected_draw_buffers =
			kWriteColor | kWriteProtectionMask;
		ConfigureAuxiliaryMrtMasks(&decision.state_for_draw);
		decision.state_after_draw.selected_draw_buffers =
			kDefaultSceneSelectedDrawBuffers;
		ConfigureAuxiliaryMrtMasks(&decision.state_after_draw);
	}
	else if (input.draw_kind == MrtDrawKind::Polygon &&
		input.drawing_to_scene_framebuffer != 0)
	{
		decision.late_cockpit_draw_buffer_override =
			input.pixel_motion_mode_enabled != 0 && input.zbuffer_enabled != 0 &&
			input.capture_locked != 0 && input.cockpit_draw != 0;
		decision.override_draw_buffers = input.cockpit_scene_frame_active != 0 ||
			decision.late_cockpit_draw_buffer_override != 0 ||
			input.pixel_motion_mode_enabled != 0 || input.zbuffer_enabled == 0;
		if (decision.override_draw_buffers != 0)
		{
			if (input.cockpit_scene_frame_active != 0)
			{
				decision.state_for_draw.selected_draw_buffers =
					kColorOnlySelectedDrawBuffers;
				decision.state_after_draw = decision.state_for_draw;
			}
			else
			{
				decision.state_for_draw.selected_draw_buffers =
					kWriteColor | kWriteProtectionMask;
				if (input.motion_write != 0)
					decision.state_for_draw.selected_draw_buffers |= kWriteVelocity;
				if (input.zbuffer_enabled != 0)
					decision.state_for_draw.selected_draw_buffers |= kWriteAoClass;
				if (input.object_id_write != 0)
					decision.state_for_draw.selected_draw_buffers |= kWriteObjectId;
				ConfigureAuxiliaryMrtMasks(&decision.state_for_draw);
				decision.state_after_draw.selected_draw_buffers =
					kDefaultSceneSelectedDrawBuffers;
				ConfigureAuxiliaryMrtMasks(&decision.state_after_draw);
			}
		}
	}

	decision.logical_write_mask =
		DeriveLiteralMrtWriteMask(decision.state_for_draw);
	decision.legal_write_mask = IsLiteralMrtStateValid(decision.state_for_draw) &&
		IsLegalMrtWriteMask(decision.logical_write_mask);
	return decision;
}

ZBiasTransitionDecision EvaluateZBiasTransition(
	const ZBiasTransitionInput &input)
{
	ZBiasTransitionDecision decision = {};
	decision.resulting = input.current;
	decision.affects_depth_and_perspective_w = 1;
	decision.changed = input.current != input.requested;
	if (decision.changed != 0)
		decision.resulting = input.requested;
	return decision;
}

const SetterTransitionContract kSetterTransitionContract[] = {
	{ LegacySetterFamily::AlphaType, 0, 1, 0, 0, 0 },
	{ LegacySetterFamily::PostMaskOnly, 1, 1, 1, 0, 0 },
	{ LegacySetterFamily::FogState, 0, 1, 0, 1, 0 },
	{ LegacySetterFamily::Lighting, 1, 1, 0, 0, 0 },
	{ LegacySetterFamily::AoClass, 1, 1, 0, 0, 0 },
	{ LegacySetterFamily::AoSuppression, 1, 1, 0, 0, 0 },
	{ LegacySetterFamily::BloomSuppression, 1, 1, 0, 0, 0 },
	{ LegacySetterFamily::SpecularMode, 1, 1, 0, 0, 0 },
	{ LegacySetterFamily::SoftParticle, 1, 1, 0, 0, 0 },
	{ LegacySetterFamily::CockpitBacking, 1, 1, 0, 0, 0 },
	{ LegacySetterFamily::ColorModel, 0, 1, 0, 0, 0 },
	{ LegacySetterFamily::TextureType, 0, 1, 0, 0, 0 },
	{ LegacySetterFamily::Filtering, 0, 1, 0, 0, 0 },
	{ LegacySetterFamily::ZTest, 0, 1, 0, 0, 0 },
	{ LegacySetterFamily::OverlayMap, 0, 1, 0, 0, 0 },
	{ LegacySetterFamily::OverlayType, 0, 1, 0, 0, 0 },
	{ LegacySetterFamily::AlphaValue, 0, 0, 0, 0, 1 },
	{ LegacySetterFamily::AlphaFactor, 1, 0, 0, 0, 1 },
	{ LegacySetterFamily::FlatColor, 0, 0, 0, 0, 1 },
	{ LegacySetterFamily::FogBorders, 0, 0, 0, 0, 1 },
	{ LegacySetterFamily::FogColor, 0, 0, 0, 0, 1 },
	{ LegacySetterFamily::DynamicLightDirection, 0, 0, 0, 0, 1 },
	{ LegacySetterFamily::DynamicLightMap, 0, 0, 0, 0, 1 },
	{ LegacySetterFamily::Wrap, 0, 0, 0, 0, 1 },
	{ LegacySetterFamily::Mip, 0, 0, 0, 0, 1 },
	{ LegacySetterFamily::ZValues, 0, 0, 0, 0, 1 },
	{ LegacySetterFamily::ZBias, 0, 1, 0, 0, 0 },
	{ LegacySetterFamily::ZWrite, 0, 0, 0, 0, 1 },
	{ LegacySetterFamily::CoplanarOffset, 1, 0, 0, 0, 1 },
	{ LegacySetterFamily::Cull, 0, 0, 0, 0, 1 },
	{ LegacySetterFamily::DynamicLighting, 1, 0, 0, 0, 1 },
};

const size_t kSetterTransitionContractCount =
	sizeof(kSetterTransitionContract) / sizeof(kSetterTransitionContract[0]);

static_assert(sizeof(kSetterTransitionContract) /
	sizeof(kSetterTransitionContract[0]) ==
	static_cast<size_t>(LegacySetterFamily::Count),
	"setter transition contract mismatch");

} // namespace render
} // namespace piccu
