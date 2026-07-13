/* Concrete Vulkan platform/resource/compiler runtime behind VulkanRenderer. */
#pragma once

#include "vk_renderer_runtime.h"

namespace piccu
{
namespace render
{
namespace vk
{

class VulkanRuntime final : public IVulkanRuntime
{
public:
	VulkanRuntime();
	~VulkanRuntime() override;

	VulkanRuntime(const VulkanRuntime &) = delete;
	VulkanRuntime &operator=(const VulkanRuntime &) = delete;

	bool Initialize(oeApplication *app,
		const CapturedPreferredState &preferred) override;
	void Shutdown() override;
	bool ApplyPreferredState(const CapturedPreferredState &preferred) override;
	RenderCaptureSegment *ActiveCapture() override;
	IRetainedWorld *RetainedWorldBridge() override;

	bool DescribeTarget(const TargetRequest &request,
		TargetDescription *description) override;
	bool DescribePresentation(const CapturedPreferredState &preferred,
		PresentationDescription *description) override;

	bool ResolveTexture(const TextureRequest &request,
		ResolvedTexture *resolved) override;
	bool ResolveTerrainTextureArrays(const TerrainArrayRequest &request,
		ResolvedTexture *base, ResolvedTexture *lightmap) override;
	bool NotifyTextureEvent(TextureEvent event, int32_t logical_handle,
		uint32_t map_type) override;
	bool CompleteReadback(const ReadbackCompletion &completion) override;
	bool SubmitPresentedFrame(uint32_t presented_frame_serial) override;
	bool DiscardFailedPresentedFrame(uint32_t presented_frame_serial) override;

	bool ResolvePipeline(const char *name, uint32_t *pipeline) override;
	bool SelectPipeline(uint32_t pipeline) override;
	bool QueryMotionVectorSample(const float current_world[3],
		const float previous_world[3], float result_uv_velocity[4]) const override;

	int VideoMemoryPressure() const override;
	double DisplayRefreshRate() const override;
	void ReportFailure(RuntimeFailure failure, const char *operation) override;

	const char *LastDiagnostic() const noexcept;
	bool DeviceLost() const noexcept;

private:
	struct Impl;
	Impl *impl_;
};

// Renderer switchboard uses one process-global runtime because Volk exposes a
// single process-global dispatch owner. It is quiescent between renderer
// instances and never permits two live Vulkan devices.
IVulkanRuntime *VulkanRuntimeSingleton();
const char *VulkanRuntimeLastDiagnostic();

} // namespace vk
} // namespace render
} // namespace piccu
