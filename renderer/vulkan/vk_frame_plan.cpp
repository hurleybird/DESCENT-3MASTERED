#include "vk_frame_plan.h"

#include <algorithm>
#include <limits>

namespace piccu
{
namespace render
{
namespace vk
{

FramePlanner::FramePlanner() = default;

void FramePlanner::Reserve(uint32_t)
{
	// FramePlan owns its output so callers may retain plans independently. Its
	// vector reserves from the exact capture/graph count during Build.
}

bool FramePlanner::AddBytes(VkDeviceSize value, VkDeviceSize *total)
{
	if (!total || *total > std::numeric_limits<VkDeviceSize>::max() - value)
		return false;
	*total += value;
	return true;
}

uint32_t FramePlanner::ReadbackPixelBytes(ReadbackFormat format)
{
	switch (format)
	{
	case ReadbackFormat::RawRgba8: return 4;
	case ReadbackFormat::Rgb8TopDown: return 3;
	case ReadbackFormat::Rgb565: return 2;
	case ReadbackFormat::R32Float: return 4;
	case ReadbackFormat::Rg16Float: return 4;
	case ReadbackFormat::R32Uint: return 4;
	default: return 0;
	}
}

const CapturedTargetSignature *FramePlanner::FindSignature(
	const RenderCaptureSegment &capture, uint32_t *signature_index)
{
	const std::vector<CaptureCommand> &commands = capture.Commands();
	for (uint32_t i = 0; i < commands.size(); ++i)
	{
		if (commands[i].type != CaptureCommandType::BeginPostPresent)
			continue;
		const uint32_t index = commands[i].payload.begin_post_present.signature;
		if (index >= capture.TargetSignatures().size())
			return nullptr;
		if (signature_index)
			*signature_index = index;
		return &capture.TargetSignatures()[index];
	}
	return nullptr;
}

bool FramePlanner::IsDrawCommand(CaptureCommandType type)
{
	return type == CaptureCommandType::DrawStream ||
		type == CaptureCommandType::DrawRetained ||
		type == CaptureCommandType::EnqueueFontGlyph ||
		type == CaptureCommandType::FlushFontBatches;
}

GraphEvaluationContext FramePlanner::BuildGraphContext(
	const RenderCaptureSegment &capture,
	const CapturedTargetSignature &signature)
{
	GraphEvaluationContext result = {};
	const CapturedTargetLayout &scene =
		capture.TargetLayouts()[signature.target_layout];
	result.msaa_samples = scene.msaa_samples;
	result.ssaa_factor = scene.ssaa_factor;
	result.resolve_consumer_mask = scene.attachment_mask;
	result.late_post_active =
		(scene.feature_flags & kTargetFeatureLatePost) != 0;
	result.gtao_enabled = signature.preferred.gtao_enabled != 0;
	result.gtao_blur_radius = signature.preferred.gtao_blur_radius;
	result.gtao_temporal_active = result.gtao_enabled &&
		(signature.preferred.gtao_temporal_blend > 0.0f ||
		 signature.preferred.gtao_temporal_debug_preview != 0);
	result.gtao_debug_active = result.gtao_enabled &&
		(signature.preferred.gtao_debug_preview != 0 ||
		 signature.preferred.gtao_temporal_debug_preview != 0);
	result.bloom_enabled = signature.preferred.bloom_enabled != 0;
	result.bloom_source_width = signature.preferred.width;
	result.bloom_source_height = signature.preferred.height;
	result.cockpit_deferral_active =
		signature.dynamic.cockpit_deferral_active != 0 ||
		signature.dynamic.defer_bloom != 0;
	result.gtao_deferred_active = result.gtao_enabled &&
		result.cockpit_deferral_active;
	result.motion_consumer_active = WantsMotionResources(signature.preferred);
	result.motion_debug_active = result.motion_consumer_active &&
		signature.preferred.motion_vector_debug_preview != 0;

	bool post_span = false;
	bool cockpit_seen = false;
	bool cockpit_closed = false;
	for (const CaptureCommand &command : capture.Commands())
	{
		switch (command.type)
		{
		case CaptureCommandType::CaptureBloomSource:
			++result.capture_call_count;
			++result.world_color_capture_call_count;
			++result.depth_capture_call_count;
			break;
		case CaptureCommandType::BeginPostPresent:
			result.post_frame_active = 1;
			post_span = false;
			break;
		case CaptureCommandType::BeginCockpitScene:
			++result.cockpit_frame_count;
			cockpit_seen = true;
			if (post_span)
				++result.cockpit_ui_pre_span_count;
			post_span = false;
			break;
		case CaptureCommandType::EndCockpitScene:
			cockpit_closed = true;
			break;
		case CaptureCommandType::Present:
			++result.present_count;
			if (post_span)
			{
				if (!cockpit_seen)
					++result.normal_ui_span_count;
				else if (cockpit_closed)
					++result.cockpit_ui_post_span_count;
			}
			post_span = false;
			break;
		case CaptureCommandType::ReadImage:
			if (command.payload.read_image.source == ImageSemantic::Velocity)
				result.motion_debug_readback_active = 1;
			break;
		default:
			if (IsDrawCommand(command.type) && result.post_frame_active &&
				!cockpit_seen)
				post_span = true;
			else if (IsDrawCommand(command.type) && cockpit_closed)
				post_span = true;
			break;
		}
	}
	return result;
}

GraphSourceSelectionContext FramePlanner::BuildSourceContext(
	const GraphEvaluationContext &graph,
	const CapturedTargetSignature &signature)
{
	GraphSourceSelectionContext result = {};
	result.msaa_samples = graph.msaa_samples;
	result.ssaa_factor = graph.ssaa_factor;
	result.captured_logical_depth_valid =
		signature.dynamic.captured_depth_valid;
	result.gtao_blur_output_valid = graph.gtao_enabled &&
		graph.gtao_blur_radius != 0;
	result.gtao_temporal_output_valid = graph.gtao_temporal_active;
	result.gtao_history_required = graph.gtao_temporal_active;
	result.gtao_applied_output_valid = graph.gtao_enabled;
	result.gtao_deferred_output_valid = graph.gtao_deferred_active;
	return result;
}

void FramePlanner::ResolveSelectedInputs(GraphNodeId node,
	const GraphSourceSelectionContext &sources, PlanOperation *operation)
{
	if (!operation)
		return;
	for (size_t i = 0; i < kGraphInputRuleContractCount; ++i)
	{
		const GraphInputRuleContract &rule = kGraphInputRuleContract[i];
		if (rule.id != node)
			continue;
		for (uint32_t selected = 0; selected < 4; ++selected)
		{
			if (rule.selected_inputs[selected] == GraphInputSemantic::None)
				break;
			operation->selected_inputs[selected] = SelectGraphInputSource(
				rule.selected_inputs[selected], sources);
		}
		break;
	}
}

void FramePlanner::AppendNode(GraphNodeId node, uint32_t command_index,
	const GraphEvaluationContext &graph,
	const GraphSourceSelectionContext &sources, FramePlan *plan)
{
	const uint32_t count = EvaluateGraphNodeInvocationCount(node, graph);
	for (uint32_t invocation = 0; invocation < count; ++invocation)
	{
		PlanOperation operation = {};
		operation.type = PlanOperationType::GraphNode;
		operation.capture_command_index = command_index;
		operation.graph_node = node;
		operation.graph_invocation = invocation;
		operation.graph_level = invocation;
		ResolveSelectedInputs(node, sources, &operation);
		plan->operations.push_back(operation);
		++plan->graph_pass_count;
	}
}

void FramePlanner::AppendPhaseNode(GraphNodeId node, uint32_t command_index,
	const GraphEvaluationContext &graph,
	const GraphSourceSelectionContext &sources, FramePlan *plan)
{
	for (uint32_t phase_index = 0;
		phase_index < kCompilerGraphPhaseContractCount; ++phase_index)
	{
		const CompilerGraphPhaseContract &phase =
			kCompilerGraphPhaseContract[phase_index];
		if (phase.node != node)
			continue;
		const uint32_t count = EvaluateCompilerGraphPhaseInvocationCount(phase,
			graph);
		for (uint32_t invocation = 0; invocation < count; ++invocation)
		{
			PlanOperation operation = {};
			operation.type = PlanOperationType::CompilerGraphPhase;
			operation.capture_command_index = command_index;
			operation.graph_node = node;
			operation.graph_invocation = invocation;
			operation.graph_level = invocation;
			operation.compiler_phase_index = phase_index;
			operation.descriptor_variant = phase.descriptor_variant;
			operation.source_selector = SelectCompilerGraphPhaseSource(phase,
				graph);
			ResolveSelectedInputs(node, sources, &operation);
			plan->operations.push_back(operation);
			++plan->graph_pass_count;
		}
	}
}

bool FramePlanner::Build(const RenderCaptureSegment &capture,
	FramePlan *plan) const
{
	if (!plan)
		return false;
	*plan = FramePlan();
	if (capture.GetLifecycle() != RenderCaptureSegment::Lifecycle::Frozen)
	{
		plan->errors |= kFramePlanInvalidCaptureLifecycle;
		return false;
	}
	CaptureValidationResult validation = {};
	if (!capture.Validate(&validation))
	{
		plan->errors |= kFramePlanInvalidCapture;
		plan->error_command_index = validation.command_index;
		return false;
	}
	CapturedTargetSignature synthetic_signature = {};
	const CapturedTargetSignature *signature = FindSignature(capture, nullptr);
	if (!signature)
	{
		TargetLayoutId scene_id = kInvalidId;
		for (uint32_t i = 0; i < capture.TargetLayouts().size(); ++i)
			if (capture.TargetLayouts()[i].target == RenderTargetClass::Scene)
			{
				scene_id = i;
				break;
			}
		if (scene_id == kInvalidId)
		{
			plan->errors |= kFramePlanMissingPresentationSignature;
			return false;
		}
		const CapturedTargetLayout &scene = capture.TargetLayouts()[scene_id];
		synthetic_signature.target_layout = scene_id;
		synthetic_signature.post_present_layout = scene_id;
		synthetic_signature.cockpit_scene_layout = scene_id;
		synthetic_signature.preferred.width = scene.logical_width;
		synthetic_signature.preferred.height = scene.logical_height;
		synthetic_signature.preferred.window_width = scene.drawable_width;
		synthetic_signature.preferred.window_height = scene.drawable_height;
		synthetic_signature.preferred.supersampling_factor = scene.ssaa_factor;
		synthetic_signature.preferred.msaa_samples = scene.msaa_samples;
		synthetic_signature.preferred.bloom_enabled =
			(scene.feature_flags & kTargetFeatureBloom) != 0;
		synthetic_signature.preferred.gtao_enabled =
			(scene.feature_flags & kTargetFeatureGtao) != 0;
		synthetic_signature.preferred.gtao_sample_count = 1;
		synthetic_signature.preferred.gamma = 1.0f;
		synthetic_signature.dynamic.captured_world_valid = 1;
		synthetic_signature.dynamic.captured_depth_valid = 1;
		signature = &synthetic_signature;
	}
	plan->graph = BuildGraphContext(capture, *signature);
	if (ValidateGraphEvaluationContext(plan->graph) != 0)
	{
		plan->errors |= kFramePlanInvalidGraphContext;
		return false;
	}
	plan->sources = BuildSourceContext(plan->graph, *signature);
	plan->operations.reserve(capture.Commands().size() +
		kFrozenGraphNodeContractCount + 32);

	bool post_begun = false;
	bool cockpit_open = false;
	bool cockpit_closed = false;
	for (uint32_t index = 0; index < capture.Commands().size(); ++index)
	{
		const CaptureCommand &command = capture.Commands()[index];
		if (command.type == CaptureCommandType::CaptureBloomSource)
		{
			AppendPhaseNode(GraphNodeId::CapWorld, index, plan->graph,
				plan->sources, plan);
			AppendNode(GraphNodeId::CapDepthLogical, index, plan->graph,
				plan->sources, plan);
		}
		else if (command.type == CaptureCommandType::BeginPostPresent)
		{
			post_begun = true;
			cockpit_open = cockpit_closed = false;
			const GraphNodeId normal_nodes[] = {
				GraphNodeId::ResolveColor, GraphNodeId::ResolveDepth,
				GraphNodeId::ResolveVelocity, GraphNodeId::ResolveObjectId,
				GraphNodeId::ResolveProtectionMask, GraphNodeId::ResolveAoClass,
				GraphNodeId::Ssaa4To2, GraphNodeId::Ssaa2To1,
				GraphNodeId::PrepareDepthLogical, GraphNodeId::AoDepth,
				GraphNodeId::AoRaw, GraphNodeId::AoBlurX, GraphNodeId::AoBlurY,
				GraphNodeId::AoTemporal, GraphNodeId::AoSuppress,
				GraphNodeId::AoApply, GraphNodeId::AoDeferredComposite,
				GraphNodeId::BloomThreshold, GraphNodeId::BloomDown,
				GraphNodeId::BloomUp, GraphNodeId::NormalComposite,
				GraphNodeId::NormalBlit, GraphNodeId::MotionNormal,
				GraphNodeId::MotionDebugNormal, GraphNodeId::NormalUi,
				GraphNodeId::CockpitLinearCopy, GraphNodeId::MotionCockpitPre,
				GraphNodeId::MotionDebugCockpitPre, GraphNodeId::CockpitUiPre,
			};
			for (GraphNodeId node : normal_nodes)
				AppendNode(node, index, plan->graph, plan->sources, plan);
		}
		else if (command.type == CaptureCommandType::BeginCockpitScene)
		{
			cockpit_open = true;
			AppendNode(GraphNodeId::CockpitScene, index, plan->graph,
				plan->sources, plan);
		}
		else if (command.type == CaptureCommandType::EndCockpitScene)
		{
			cockpit_open = false;
			cockpit_closed = true;
			AppendNode(GraphNodeId::PostAlphaClear, index, plan->graph,
				plan->sources, plan);
			AppendPhaseNode(GraphNodeId::CockpitResolve, index, plan->graph,
				plan->sources, plan);
			AppendPhaseNode(GraphNodeId::BloomDeferred, index, plan->graph,
				plan->sources, plan);
			const GraphNodeId finish_nodes[] = {
				GraphNodeId::CockpitOver, GraphNodeId::CockpitBloomGamma,
				GraphNodeId::CockpitGammaOnly, GraphNodeId::CockpitUiPost,
			};
			for (GraphNodeId node : finish_nodes)
				AppendNode(node, index, plan->graph, plan->sources, plan);
		}
		else if (command.type == CaptureCommandType::AcquireSoftDepth)
		{
			// Soft depth is an exact mid-scene graph insertion.  It deliberately
			// reuses the typed depth-map post pipeline while writing the dedicated
			// snapshot resource, rather than becoming part of the frozen late-post
			// graph.
			PlanOperation inserted = {};
			inserted.type = PlanOperationType::InsertedGraphNode;
			inserted.capture_command_index = index;
			inserted.graph_node = GraphNodeId::CapDepthLogical;
			inserted.inserted_graph_node = InsertedGraphNodeId::AcquireSoftDepth;
			plan->operations.push_back(inserted);
			++plan->graph_pass_count;
		}

		PlanOperation direct = {};
		direct.type = PlanOperationType::CapturedCommand;
		direct.capture_command_index = index;
		plan->operations.push_back(direct);
		if (command.type == CaptureCommandType::DrawStream ||
			command.type == CaptureCommandType::DrawRetained ||
			command.type == CaptureCommandType::FlushFontBatches)
			++plan->direct_draw_count;
		if (command.type == CaptureCommandType::ReadPixel)
			++plan->synchronous_readback_count;
		if (command.type == CaptureCommandType::ReadImage)
			++plan->deferred_readback_count;
		if (command.type == CaptureCommandType::Present)
			AppendNode(GraphNodeId::Present, index, plan->graph,
				plan->sources, plan);

		if ((!post_begun && (command.type == CaptureCommandType::BeginCockpitScene ||
			 command.type == CaptureCommandType::Present)) ||
			(cockpit_open && command.type == CaptureCommandType::BeginPostPresent) ||
			(cockpit_closed && command.type == CaptureCommandType::BeginCockpitScene))
		{
			plan->errors |= kFramePlanInvalidCommandOrder;
			plan->error_command_index = index;
			return false;
		}
	}

	FrameRequirements &requirements = plan->requirements;
	requirements.vertex_bytes = capture.StreamVertices().size() *
		sizeof(BaseVertex);
	requirements.index_bytes = capture.StreamIndices().size() * sizeof(uint32_t);
	requirements.storage_bytes =
		capture.States().size() * sizeof(GpuShaderState) +
		capture.Materials().size() * sizeof(GpuMaterial) +
		capture.Transforms().size() * sizeof(GpuTransform) +
		capture.Commands().size() * sizeof(GpuDrawHeader) +
		capture.StreamPayloadWords().size() * sizeof(uint32_t);
	requirements.indirect_bytes = plan->direct_draw_count *
		sizeof(VkDrawIndexedIndirectCommand);
	requirements.upload_bytes = requirements.vertex_bytes +
		requirements.index_bytes + requirements.storage_bytes +
		requirements.indirect_bytes + plan->graph_pass_count *
		sizeof(PostPassUniforms);
	requirements.descriptor_sets = std::max(32u,
		plan->graph_pass_count + plan->direct_draw_count / 64u + 8u);
	requirements.timestamp_queries = std::max(16u,
		plan->graph_pass_count * 2u + 8u);

	for (const CaptureCommand &command : capture.Commands())
	{
		VkDeviceSize bytes = 0;
		if (command.type == CaptureCommandType::ReadPixel)
			bytes = ReadbackPixelBytes(command.payload.read_pixel.format);
		else if (command.type == CaptureCommandType::ReadImage)
		{
			const LogicalRect &rect = command.payload.read_image.rect;
			bytes = VkDeviceSize(rect.width) * VkDeviceSize(rect.height) *
				ReadbackPixelBytes(command.payload.read_image.format);
		}
		if (bytes && !AddBytes(bytes, &requirements.readback_bytes))
		{
			plan->errors |= kFramePlanSizeOverflow;
			return false;
		}
	}
	return true;
}

} // namespace vk
} // namespace render
} // namespace piccu
