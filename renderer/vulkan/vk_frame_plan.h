/* Deterministic lowering of a frozen capture segment into ordered GPU work. */
#pragma once

#include "vk_frame.h"
#include "../core/render_capture.h"
#include "../core/render_graph_contract.h"
#include "../core/render_device_contract.h"

#include <stdint.h>
#include <vector>

namespace piccu
{
namespace render
{
namespace vk
{

enum class PlanOperationType : uint32_t
{
	CapturedCommand = 0,
	GraphNode,
	CompilerGraphPhase,
	InsertedGraphNode,
};

struct PlanOperation
{
	PlanOperationType type = PlanOperationType::CapturedCommand;
	uint32_t capture_command_index = kInvalidId;
	GraphNodeId graph_node = GraphNodeId::Count;
	uint32_t graph_invocation = 0;
	uint32_t graph_level = 0;
	uint32_t compiler_phase_index = kInvalidId;
	InsertedGraphNodeId inserted_graph_node = InsertedGraphNodeId::Count;
	PostPassVariant descriptor_variant = PostPassVariant::Only;
	PostUniformSourceSelector source_selector =
		PostUniformSourceSelector::Primary2D;
	GraphResource selected_inputs[4] = {
		GraphResource::Count, GraphResource::Count,
		GraphResource::Count, GraphResource::Count
	};
};

enum FramePlanErrorBits : uint32_t
{
	kFramePlanInvalidCaptureLifecycle = 1u << 0,
	kFramePlanInvalidCapture = 1u << 1,
	kFramePlanMissingPresentationSignature = 1u << 2,
	kFramePlanInvalidGraphContext = 1u << 3,
	kFramePlanInvalidCommandOrder = 1u << 4,
	kFramePlanSizeOverflow = 1u << 5,
};

struct FramePlan
{
	uint32_t errors = 0;
	uint32_t error_command_index = kInvalidId;
	GraphEvaluationContext graph = {};
	GraphSourceSelectionContext sources = {};
	FrameRequirements requirements = {};
	std::vector<PlanOperation> operations;
	uint32_t direct_draw_count = 0;
	uint32_t graph_pass_count = 0;
	uint32_t synchronous_readback_count = 0;
	uint32_t deferred_readback_count = 0;

	bool Valid() const noexcept { return errors == 0; }
};

class FramePlanner final
{
public:
	FramePlanner();
	void Reserve(uint32_t operation_count);
	bool Build(const RenderCaptureSegment &capture, FramePlan *plan) const;

private:
	static bool AddBytes(VkDeviceSize value, VkDeviceSize *total);
	static uint32_t ReadbackPixelBytes(ReadbackFormat format);
	static const CapturedTargetSignature *FindSignature(
		const RenderCaptureSegment &capture, uint32_t *signature_index);
	static GraphEvaluationContext BuildGraphContext(
		const RenderCaptureSegment &capture,
		const CapturedTargetSignature &signature);
	static GraphSourceSelectionContext BuildSourceContext(
		const GraphEvaluationContext &graph,
		const CapturedTargetSignature &signature);
	static void ResolveSelectedInputs(GraphNodeId node,
		const GraphSourceSelectionContext &sources, PlanOperation *operation);
	static void AppendNode(GraphNodeId node, uint32_t command_index,
		const GraphEvaluationContext &graph,
		const GraphSourceSelectionContext &sources, FramePlan *plan);
	static void AppendPhaseNode(GraphNodeId node, uint32_t command_index,
		const GraphEvaluationContext &graph,
		const GraphSourceSelectionContext &sources, FramePlan *plan);
	static bool IsDrawCommand(CaptureCommandType type);
};

} // namespace vk
} // namespace render
} // namespace piccu
