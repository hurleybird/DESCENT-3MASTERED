/* Synchronization2 resource-state tracker for exact buffer/image subranges. */
#pragma once

#include "vk_common.h"

#include <stdint.h>
#include <vector>

namespace piccu
{
namespace render
{
namespace vk
{

enum class ResourceIntent : uint32_t
{
	Read = 0,
	Write,
	ReadWrite,
};

struct BufferUse
{
	VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
	VkAccessFlags2 access = VK_ACCESS_2_NONE;
	uint32_t queue_family = VK_QUEUE_FAMILY_IGNORED;
	ResourceIntent intent = ResourceIntent::Read;
};

struct ImageUse
{
	VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
	VkAccessFlags2 access = VK_ACCESS_2_NONE;
	VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
	uint32_t queue_family = VK_QUEUE_FAMILY_IGNORED;
	ResourceIntent intent = ResourceIntent::Read;
};

struct ResourceStateSnapshot
{
	uint64_t serial = 0;
	struct BufferRecord
	{
		VkBuffer buffer = VK_NULL_HANDLE;
		VkDeviceSize begin = 0;
		VkDeviceSize end = 0;
		BufferUse use;
		uint64_t last_use_timeline = 0;
	};
	struct ImageRecord
	{
		VkImage image = VK_NULL_HANDLE;
		VkImageAspectFlagBits aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		uint32_t mip = 0;
		uint32_t layer = 0;
		ImageUse use;
		uint64_t last_use_timeline = 0;
	};
	std::vector<BufferRecord> buffers;
	std::vector<ImageRecord> images;
};

class ResourceStateTracker final
{
public:
	ResourceStateTracker();

	void Reserve(uint32_t buffer_ranges, uint32_t image_subresources,
		uint32_t pending_barriers);
	void Reset();
	void BeginRecording();

	void ImportBuffer(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size,
		const BufferUse &use, uint64_t last_use_timeline = 0);
	void ImportImage(VkImage image, const VkImageSubresourceRange &range,
		const ImageUse &use, uint64_t last_use_timeline = 0);
	// A Vulkan handle may be reused after its object is destroyed.  Remove every
	// trace of the old object, including unflushed barriers and submission stamps,
	// before destruction or before publishing a logically fresh object.
	void ForgetBuffer(VkBuffer buffer);
	void ForgetImage(VkImage image);
	void ImportFreshBuffer(VkBuffer buffer, VkDeviceSize offset,
		VkDeviceSize size, const BufferUse &use,
		uint64_t last_use_timeline = 0);
	void ImportFreshImage(VkImage image,
		const VkImageSubresourceRange &range, const ImageUse &use,
		uint64_t last_use_timeline = 0);

	bool UseBuffer(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size,
		const BufferUse &use);
	bool UseImage(VkImage image, const VkImageSubresourceRange &range,
		const ImageUse &use);
	void Flush(VkCommandBuffer command_buffer,
		VkDependencyFlags dependency_flags = 0);
	void StampSubmission(uint64_t timeline_value);

	ResourceStateSnapshot Snapshot(uint64_t serial) const;
	bool Restore(const ResourceStateSnapshot &snapshot);
	uint64_t LastUse(VkBuffer buffer, VkDeviceSize offset,
		VkDeviceSize size) const;
	uint64_t LastUse(VkImage image,
		const VkImageSubresourceRange &range) const;

private:
	using BufferRecord = ResourceStateSnapshot::BufferRecord;
	using ImageRecord = ResourceStateSnapshot::ImageRecord;
	struct TouchedBuffer
	{
		VkBuffer buffer;
		VkDeviceSize begin;
		VkDeviceSize end;
	};
	struct TouchedImage
	{
		VkImage image;
		VkImageAspectFlagBits aspect;
		uint32_t mip;
		uint32_t layer;
	};

	static bool Writes(ResourceIntent intent);
	static bool Same(const BufferUse &left, const BufferUse &right);
	static bool Same(const ImageUse &left, const ImageUse &right);
	static VkDeviceSize RangeEnd(VkDeviceSize offset, VkDeviceSize size);
	void ReplaceBufferRange(VkBuffer buffer, VkDeviceSize begin,
		VkDeviceSize end, const BufferUse &use);
	void TouchBuffer(VkBuffer buffer, VkDeviceSize begin, VkDeviceSize end);
	ImageRecord *FindImage(VkImage image, VkImageAspectFlagBits aspect,
		uint32_t mip, uint32_t layer);
	const ImageRecord *FindImage(VkImage image, VkImageAspectFlagBits aspect,
		uint32_t mip, uint32_t layer) const;

	std::vector<BufferRecord> buffers_;
	std::vector<BufferRecord> buffer_replacement_scratch_;
	std::vector<ImageRecord> images_;
	std::vector<VkBufferMemoryBarrier2> buffer_barriers_;
	std::vector<VkImageMemoryBarrier2> image_barriers_;
	std::vector<TouchedBuffer> touched_buffers_;
	std::vector<TouchedImage> touched_images_;
};

} // namespace vk
} // namespace render
} // namespace piccu
