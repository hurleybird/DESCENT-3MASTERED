/* Capture-segment compiler and Vulkan submission encoder. */
#pragma once

#include "vk_frame_plan.h"
#include "vk_pipelines.h"
#include "vk_retained_world.h"
#include "vk_state_tracker.h"
#include "vk_targets.h"
#include "vk_textures.h"
#include "vk_wsi.h"

#include <stdint.h>
#include <string>
#include <vector>

namespace piccu
{
namespace render
{
namespace vk
{

struct CompiledReadback
{
	ReadbackRequestId request = kInvalidId;
	ReadbackFormat format = ReadbackFormat::RawRgba8;
	ReadbackRowOrder row_order = ReadbackRowOrder::TopDown;
	uint32_t width = 0;
	uint32_t height = 0;
	FrameBufferSlice slice;
};

struct CompilerSubmission
{
	uint64_t timeline_value = 0;
	uint64_t resource_state_snapshot_serial = 0;
	uint64_t wsi_token = 0;
	PresentResult presentation;
	std::vector<CompiledReadback> readbacks;
	uint32_t direct_draws = 0;
	uint32_t indirect_commands = 0;
	uint32_t indirect_batches = 0;
	uint32_t graph_passes = 0;
	uint32_t gtao_active = 0;
	uint32_t dynamic_rendering_instances = 0;
	uint32_t descriptor_page_binds = 0;
};

struct FrameCompilerCreateInfo
{
	Platform *platform = nullptr;
	ResourceAllocator *allocator = nullptr;
	FrameScheduler *frames = nullptr;
	ResourceStateTracker *state_tracker = nullptr;
	TargetManager *targets = nullptr;
	TextureManager *textures = nullptr;
	PipelineLibrary *pipelines = nullptr;
	RetainedWorld *retained_world = nullptr;
	Wsi *wsi = nullptr;
};

class FrameCompiler final
{
public:
	FrameCompiler();
	~FrameCompiler();

	FrameCompiler(const FrameCompiler &) = delete;
	FrameCompiler &operator=(const FrameCompiler &) = delete;

	bool Initialize(const FrameCompilerCreateInfo &create_info);
	void Shutdown(bool device_lost = false) noexcept;
	bool Ready() const noexcept;
	// capture must be Frozen. When present=false no swapchain image is acquired;
	// this is the synchronous-readback prefix path.
	bool CompileAndSubmit(RenderCaptureSegment *capture, bool present,
		CompilerSubmission *submission);
	const std::string &LastError() const noexcept;
	const FramePlan &LastPlan() const noexcept;
	const ResourceStateSnapshot *FindSnapshot(uint64_t serial) const;

private:
	struct Impl;
	Impl *impl_;
};

} // namespace vk
} // namespace render
} // namespace piccu
