/* Atomic scene/post target generations for the frozen Vulkan render graph. */
#pragma once

#include "vk_resources.h"
#include "vk_state_tracker.h"
#include "../core/render_graph_contract.h"

#include <stdint.h>
#include <vector>

namespace piccu
{
namespace render
{
namespace vk
{

struct TargetImageRef
{
	VkImage image = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	VkFormat format = VK_FORMAT_UNDEFINED;
	VkExtent2D extent = {};
	VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
	VkImageAspectFlags aspect = 0;
	uint64_t generation = 0;

	bool Valid() const noexcept { return image != VK_NULL_HANDLE; }
};

class TargetManager final
{
public:
	TargetManager();
	~TargetManager();

	TargetManager(const TargetManager &) = delete;
	TargetManager &operator=(const TargetManager &) = delete;

	bool Initialize(ResourceAllocator *allocator,
		ResourceStateTracker *state_tracker, uint32_t supported_sample_mask);
	void Shutdown(bool device_lost = false) noexcept;
	bool Configure(const CapturedPreferredState &preferred,
		VkExtent2D drawable_extent, uint64_t retire_after_timeline);
	bool Ready() const noexcept;

	const CapturedTargetLayout &SceneLayout() const;
	const CapturedTargetLayout &PostLayout() const;
	const CapturedTargetLayout &CockpitLayout() const;
	CapturedTargetVersion DescribeVersion(RenderTargetClass target,
		TargetLayoutId layout_id, uint32_t color_epoch,
		uint32_t depth_epoch) const;

	TargetImageRef Attachment(RenderTargetClass target,
		uint32_t attachment_index) const;
	TargetImageRef GraphImage(GraphResource resource, uint32_t level = 0) const;
	uint32_t BloomLevelCount() const noexcept;
	uint32_t GtaoScale() const noexcept;
	uint64_t Generation() const noexcept;
	uint64_t EstimatedImageBytes() const noexcept;
	void StampUse(uint64_t timeline_value);
	void InvalidateHistories();
	bool GtaoHistoryValid() const noexcept;
	bool MotionHistoryValid() const noexcept;
	bool CockpitHistoryValid() const noexcept;
	void SetHistoryValidity(bool gtao, bool motion, bool cockpit);

	static VkFormat VulkanFormat(RenderFormat format);
	static RenderFormat ContractFormat(uint32_t attachment_index);

private:
	struct GraphImageEntry
	{
		GraphResource resource = GraphResource::Count;
		uint32_t level = 0;
		AllocatedImage allocation;
	};
	struct GenerationState
	{
		uint64_t generation = 0;
		CapturedPreferredState preferred = {};
		CapturedTargetLayout scene = {};
		CapturedTargetLayout post = {};
		CapturedTargetLayout cockpit = {};
		AllocatedImage scene_attachments[6];
		AllocatedImage post_attachments[6];
		AllocatedImage cockpit_attachments[6];
		std::vector<GraphImageEntry> graph_images;
		uint32_t bloom_level_count = 0;
		uint32_t gtao_scale = 1;
		uint64_t estimated_bytes = 0;
		bool gtao_history_valid = false;
		bool motion_history_valid = false;
		bool cockpit_history_valid = false;
		bool state_registered = false;
	};

	bool BuildGeneration(const CapturedPreferredState &preferred,
		VkExtent2D drawable_extent, GenerationState *generation);
	bool AddGraphImage(GenerationState *generation, GraphResource resource,
		uint32_t level, VkFormat format, VkExtent2D extent,
		VkImageUsageFlags usage, VkImageAspectFlags aspect,
		const char *debug_name);
	bool AddAttachment(AllocatedImage *output, VkFormat format,
		VkExtent2D extent, VkSampleCountFlagBits samples,
		VkImageUsageFlags usage, VkImageAspectFlags aspect,
		const char *debug_name, uint64_t *estimated_bytes);
	void RetireGeneration(GenerationState *generation,
		uint64_t retire_after_timeline) noexcept;
	void RegisterGenerationState(GenerationState *generation);
	void UnregisterGenerationState(GenerationState *generation) noexcept;
	TargetImageRef Reference(const AllocatedImage &image) const;
	const AllocatedImage *FindGraph(GraphResource resource, uint32_t level) const;
	static uint32_t BytesPerPixel(VkFormat format);
	static uint32_t ResolveGtaoScale(const CapturedPreferredState &preferred,
		uint32_t width, uint32_t height, uint32_t applied_msaa);
	static uint32_t FeatureFlags(const CapturedPreferredState &preferred);

	ResourceAllocator *allocator_;
	ResourceStateTracker *state_tracker_;
	uint32_t supported_sample_mask_;
	GenerationState active_;
	uint64_t next_generation_;
	bool initialized_;
};

} // namespace vk
} // namespace render
} // namespace piccu
