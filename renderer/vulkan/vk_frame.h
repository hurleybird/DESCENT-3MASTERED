/* Two-slot Vulkan frame scheduler and pre-sized transient stream arenas. */
#pragma once

#include "vk_platform.h"
#include "vk_resources.h"

#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace piccu
{
namespace render
{
namespace vk
{

constexpr uint32_t kFrameContextCount = 2;

enum class FrameBufferClass : uint32_t
{
	Vertex = 0,
	Index,
	Storage,
	Indirect,
	Readback,
	Count,
};

struct FrameRequirements
{
	VkDeviceSize upload_bytes = 0;
	VkDeviceSize vertex_bytes = 0;
	VkDeviceSize index_bytes = 0;
	VkDeviceSize storage_bytes = 0;
	VkDeviceSize indirect_bytes = 0;
	VkDeviceSize readback_bytes = 0;
	uint32_t descriptor_sets = 0;
	// World descriptor pages contain the device-selected bindless tier, which
	// can be much larger than the historical per-set pool heuristic.
	uint32_t descriptor_sampled_images = 0;
	uint32_t timestamp_queries = 0;
};

struct FrameSchedulerConfig
{
	VkDeviceSize initial_upload_bytes = 8u * 1024u * 1024u;
	VkDeviceSize initial_vertex_bytes = 4u * 1024u * 1024u;
	VkDeviceSize initial_index_bytes = 2u * 1024u * 1024u;
	VkDeviceSize initial_storage_bytes = 8u * 1024u * 1024u;
	VkDeviceSize initial_indirect_bytes = 1u * 1024u * 1024u;
	VkDeviceSize initial_readback_bytes = 4u * 1024u * 1024u;
	uint32_t initial_descriptor_sets = 256;
	uint32_t initial_timestamp_queries = 256;
};

struct FrameBufferSlice
{
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceSize offset = 0;
	VkDeviceSize size = 0;
	void *mapped = nullptr;

	bool Valid() const noexcept { return buffer != VK_NULL_HANDLE && size != 0; }
};

struct FrameSubmitInfo
{
	VkSemaphore wait_binary = VK_NULL_HANDLE;
	VkPipelineStageFlags2 wait_stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	VkSemaphore signal_binary = VK_NULL_HANDLE;
	uint64_t timeline_value = 0; // zero asks the scheduler to allocate one.
};

struct FrameContext
{
	VkCommandPool command_pool = VK_NULL_HANDLE;
	VkCommandBuffer primary = VK_NULL_HANDLE;
	VkCommandBuffer continuation = VK_NULL_HANDLE;
	VkCommandBuffer recording_command = VK_NULL_HANDLE;
	VkCommandBuffer executable_command = VK_NULL_HANDLE;
	VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
	VkQueryPool timestamp_pool = VK_NULL_HANDLE;
	AllocatedBuffer upload;
	AllocatedBuffer buffers[static_cast<uint32_t>(FrameBufferClass::Count)];
	VkDeviceSize upload_head = 0;
	VkDeviceSize buffer_heads[static_cast<uint32_t>(FrameBufferClass::Count)] = {};
	uint32_t descriptor_capacity = 0;
	uint32_t descriptor_sampled_image_capacity = 0;
	uint32_t timestamp_capacity = 0;
	uint32_t recorded_timestamp_count = 0;
	double completed_gpu_frame_ms = 0.0;
	bool completed_gpu_frame_valid = false;
	uint64_t last_submitted_timeline = 0;
	uint32_t frame_slot = 0;
	bool recording = false;
	bool abandoned = false;
};

class FrameScheduler final
{
public:
	FrameScheduler();
	~FrameScheduler();

	FrameScheduler(const FrameScheduler &) = delete;
	FrameScheduler &operator=(const FrameScheduler &) = delete;

	bool Initialize(Platform *platform, ResourceAllocator *allocator,
		const FrameSchedulerConfig &config = FrameSchedulerConfig());
	void Shutdown(bool device_lost = false) noexcept;
	bool Ready() const noexcept;

	// Reserve may grow resources only before command recording. Old versions are
	// timeline-retired through ResourceAllocator.
	bool Reserve(const FrameRequirements &requirements);
	FrameContext *BeginFrame(uint64_t timeout_nanoseconds = UINT64_MAX);
	VkCommandBuffer BeginContinuation();
	bool EndRecording();
	bool Submit(const FrameSubmitInfo &submit, uint64_t *signaled_timeline);
	void AbandonCurrentRecording() noexcept;

	FrameBufferSlice AllocateUpload(VkDeviceSize size, VkDeviceSize alignment);
	FrameBufferSlice Allocate(FrameBufferClass buffer_class, VkDeviceSize size,
		VkDeviceSize alignment);
	bool StageBuffer(const void *source, VkDeviceSize size,
		FrameBufferClass destination_class, VkDeviceSize destination_alignment,
		FrameBufferSlice *destination);

	uint64_t PollCompletedTimeline();
	bool WaitTimeline(uint64_t value, uint64_t timeout_nanoseconds = UINT64_MAX);
	uint64_t NextTimelineValue() const noexcept
	{
		return next_timeline_value_ == UINT64_MAX ? 0 : next_timeline_value_ + 1;
	}
	// Publishes a successful renderer-owned submission recorded outside Submit
	// (currently only WSI's abandoned-acquire drain). Never call on failure.
	bool ConfirmExternalSubmission(uint64_t timeline_value);
	uint64_t LastAllocatedTimeline() const noexcept { return next_timeline_value_; }
	uint64_t CompletedTimeline() const noexcept { return completed_timeline_; }
	FrameContext *Current() noexcept { return current_; }
	const FrameContext *Current() const noexcept { return current_; }

private:
	bool CreateContext(uint32_t slot, const FrameSchedulerConfig &config);
	void DestroyContext(FrameContext *context, bool device_lost) noexcept;
	bool EnsureBuffer(FrameContext *context, FrameBufferClass buffer_class,
		VkDeviceSize required_size, uint64_t retire_after);
	bool EnsureUpload(FrameContext *context, VkDeviceSize required_size,
		uint64_t retire_after);
	bool EnsureDescriptorPool(FrameContext *context, uint32_t required_sets,
		uint32_t required_sampled_images, uint64_t retire_after);
	bool EnsureQueryPool(FrameContext *context, uint32_t required_queries,
		uint64_t retire_after);
	VkDeviceSize BufferCapacity(const FrameContext &context,
		FrameBufferClass buffer_class) const;
	static VkDeviceSize GrowCapacity(VkDeviceSize current, VkDeviceSize required);
	static uint32_t GrowCount(uint32_t current, uint32_t required);
	static VkDeviceSize AlignUp(VkDeviceSize value, VkDeviceSize alignment);

	Platform *platform_;
	ResourceAllocator *allocator_;
	FrameContext contexts_[kFrameContextCount];
	FrameContext *current_;
	FrameSchedulerConfig config_;
	uint32_t next_slot_;
	uint64_t next_timeline_value_;
	uint64_t completed_timeline_;
	bool ready_;
};

} // namespace vk
} // namespace render
} // namespace piccu
