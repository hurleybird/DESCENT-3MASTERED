#include "vk_state_tracker.h"

#include <algorithm>
#include <limits>

namespace piccu
{
namespace render
{
namespace vk
{

namespace
{

template <typename Handle>
uint64_t HandleValue(Handle handle)
{
#if VK_USE_64_BIT_PTR_DEFINES
	return reinterpret_cast<uint64_t>(handle);
#else
	return static_cast<uint64_t>(handle);
#endif
}

bool QueueTransfer(uint32_t old_family, uint32_t new_family)
{
	return old_family != VK_QUEUE_FAMILY_IGNORED &&
		new_family != VK_QUEUE_FAMILY_IGNORED && old_family != new_family;
}

} // namespace

ResourceStateTracker::ResourceStateTracker() = default;

void ResourceStateTracker::Reserve(uint32_t buffer_ranges,
	uint32_t image_subresources, uint32_t pending_barriers)
{
	buffers_.reserve(buffer_ranges);
	images_.reserve(image_subresources);
	buffer_barriers_.reserve(pending_barriers);
	image_barriers_.reserve(pending_barriers);
	touched_buffers_.reserve(buffer_ranges);
	touched_images_.reserve(image_subresources);
}

void ResourceStateTracker::Reset()
{
	buffers_.clear();
	images_.clear();
	BeginRecording();
}

void ResourceStateTracker::BeginRecording()
{
	buffer_barriers_.clear();
	image_barriers_.clear();
	touched_buffers_.clear();
	touched_images_.clear();
}

bool ResourceStateTracker::Writes(ResourceIntent intent)
{
	return intent != ResourceIntent::Read;
}

bool ResourceStateTracker::Same(const BufferUse &left,
	const BufferUse &right)
{
	return left.stages == right.stages && left.access == right.access &&
		left.queue_family == right.queue_family && left.intent == right.intent;
}

bool ResourceStateTracker::Same(const ImageUse &left, const ImageUse &right)
{
	return left.stages == right.stages && left.access == right.access &&
		left.layout == right.layout &&
		left.queue_family == right.queue_family && left.intent == right.intent;
}

VkDeviceSize ResourceStateTracker::RangeEnd(VkDeviceSize offset,
	VkDeviceSize size)
{
	if (size == VK_WHOLE_SIZE ||
		offset > std::numeric_limits<VkDeviceSize>::max() - size)
		return std::numeric_limits<VkDeviceSize>::max();
	return offset + size;
}

void ResourceStateTracker::ImportBuffer(VkBuffer buffer, VkDeviceSize offset,
	VkDeviceSize size, const BufferUse &use, uint64_t last_use_timeline)
{
	if (buffer == VK_NULL_HANDLE || size == 0)
		return;
	ReplaceBufferRange(buffer, offset, RangeEnd(offset, size), use);
	for (BufferRecord &record : buffers_)
		if (record.buffer == buffer && record.begin < RangeEnd(offset, size) &&
			record.end > offset)
			record.last_use_timeline = last_use_timeline;
}

void ResourceStateTracker::ImportImage(VkImage image,
	const VkImageSubresourceRange &range, const ImageUse &use,
	uint64_t last_use_timeline)
{
	if (image == VK_NULL_HANDLE || range.aspectMask == 0 ||
		range.levelCount == 0 || range.layerCount == 0)
		return;
	for (uint32_t bit = 1; bit != 0; bit <<= 1)
	{
		if (!(range.aspectMask & bit))
			continue;
		for (uint32_t mip = range.baseMipLevel;
			mip < range.baseMipLevel + range.levelCount; ++mip)
			for (uint32_t layer = range.baseArrayLayer;
				layer < range.baseArrayLayer + range.layerCount; ++layer)
			{
				ImageRecord *existing = FindImage(image,
					static_cast<VkImageAspectFlagBits>(bit), mip, layer);
				if (!existing)
				{
					ImageRecord record = {};
					record.image = image;
					record.aspect = static_cast<VkImageAspectFlagBits>(bit);
					record.mip = mip;
					record.layer = layer;
					record.use = use;
					record.last_use_timeline = last_use_timeline;
					images_.push_back(record);
				}
				else
				{
					existing->use = use;
					existing->last_use_timeline = last_use_timeline;
				}
			}
	}
}

void ResourceStateTracker::ForgetBuffer(VkBuffer buffer)
{
	if (buffer == VK_NULL_HANDLE)
		return;
	buffers_.erase(std::remove_if(buffers_.begin(), buffers_.end(),
		[buffer](const BufferRecord &record) {
			return record.buffer == buffer;
		}), buffers_.end());
	buffer_barriers_.erase(std::remove_if(buffer_barriers_.begin(),
		buffer_barriers_.end(), [buffer](const VkBufferMemoryBarrier2 &barrier) {
			return barrier.buffer == buffer;
		}), buffer_barriers_.end());
	touched_buffers_.erase(std::remove_if(touched_buffers_.begin(),
		touched_buffers_.end(), [buffer](const TouchedBuffer &touched) {
			return touched.buffer == buffer;
		}), touched_buffers_.end());
}

void ResourceStateTracker::ForgetImage(VkImage image)
{
	if (image == VK_NULL_HANDLE)
		return;
	images_.erase(std::remove_if(images_.begin(), images_.end(),
		[image](const ImageRecord &record) {
			return record.image == image;
		}), images_.end());
	image_barriers_.erase(std::remove_if(image_barriers_.begin(),
		image_barriers_.end(), [image](const VkImageMemoryBarrier2 &barrier) {
			return barrier.image == image;
		}), image_barriers_.end());
	touched_images_.erase(std::remove_if(touched_images_.begin(),
		touched_images_.end(), [image](const TouchedImage &touched) {
			return touched.image == image;
		}), touched_images_.end());
}

void ResourceStateTracker::ImportFreshBuffer(VkBuffer buffer,
	VkDeviceSize offset, VkDeviceSize size, const BufferUse &use,
	uint64_t last_use_timeline)
{
	ForgetBuffer(buffer);
	ImportBuffer(buffer, offset, size, use, last_use_timeline);
}

void ResourceStateTracker::ImportFreshImage(VkImage image,
	const VkImageSubresourceRange &range, const ImageUse &use,
	uint64_t last_use_timeline)
{
	ForgetImage(image);
	ImportImage(image, range, use, last_use_timeline);
}

void ResourceStateTracker::ReplaceBufferRange(VkBuffer buffer,
	VkDeviceSize begin, VkDeviceSize end, const BufferUse &use)
{
	std::vector<BufferRecord> replacement;
	replacement.reserve(buffers_.size() + 2);
	uint64_t inherited_last_use = 0;
	for (const BufferRecord &record : buffers_)
	{
		if (record.buffer != buffer || record.end <= begin || record.begin >= end)
		{
			replacement.push_back(record);
			continue;
		}
		inherited_last_use = std::max(inherited_last_use,
			record.last_use_timeline);
		if (record.begin < begin)
		{
			BufferRecord left = record;
			left.end = begin;
			replacement.push_back(left);
		}
		if (record.end > end)
		{
			BufferRecord right = record;
			right.begin = end;
			replacement.push_back(right);
		}
	}
	BufferRecord added = {};
	added.buffer = buffer;
	added.begin = begin;
	added.end = end;
	added.use = use;
	added.last_use_timeline = inherited_last_use;
	replacement.push_back(added);
	std::sort(replacement.begin(), replacement.end(),
		[](const BufferRecord &left, const BufferRecord &right) {
			const uint64_t lh = HandleValue(left.buffer);
			const uint64_t rh = HandleValue(right.buffer);
			return lh != rh ? lh < rh : left.begin < right.begin;
		});
	buffers_.clear();
	for (const BufferRecord &record : replacement)
	{
		if (!buffers_.empty())
		{
			BufferRecord &back = buffers_.back();
			if (back.buffer == record.buffer && back.end == record.begin &&
				back.last_use_timeline == record.last_use_timeline &&
				Same(back.use, record.use))
			{
				back.end = record.end;
				continue;
			}
		}
		buffers_.push_back(record);
	}
}

bool ResourceStateTracker::UseBuffer(VkBuffer buffer, VkDeviceSize offset,
	VkDeviceSize size, const BufferUse &use)
{
	if (buffer == VK_NULL_HANDLE || size == 0 || use.stages == 0)
		return false;
	const VkDeviceSize end = RangeEnd(offset, size);
	for (const BufferRecord &old : buffers_)
	{
		if (old.buffer != buffer || old.end <= offset || old.begin >= end)
			continue;
		const bool transfer = QueueTransfer(old.use.queue_family,
			use.queue_family);
		if (transfer || Writes(old.use.intent) || Writes(use.intent))
		{
			VkBufferMemoryBarrier2 barrier = {
				VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2
			};
			barrier.srcStageMask = old.use.stages;
			barrier.srcAccessMask = old.use.access;
			barrier.dstStageMask = use.stages;
			barrier.dstAccessMask = use.access;
			barrier.srcQueueFamilyIndex = transfer ? old.use.queue_family :
				VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = transfer ? use.queue_family :
				VK_QUEUE_FAMILY_IGNORED;
			barrier.buffer = buffer;
			barrier.offset = std::max(offset, old.begin);
			barrier.size = std::min(end, old.end) - barrier.offset;
			buffer_barriers_.push_back(barrier);
		}
	}
	ReplaceBufferRange(buffer, offset, end, use);
	touched_buffers_.push_back({ buffer, offset, end });
	return true;
}

ResourceStateTracker::ImageRecord *ResourceStateTracker::FindImage(
	VkImage image, VkImageAspectFlagBits aspect, uint32_t mip, uint32_t layer)
{
	for (ImageRecord &record : images_)
		if (record.image == image && record.aspect == aspect &&
			record.mip == mip && record.layer == layer)
			return &record;
	return nullptr;
}

const ResourceStateTracker::ImageRecord *ResourceStateTracker::FindImage(
	VkImage image, VkImageAspectFlagBits aspect, uint32_t mip,
	uint32_t layer) const
{
	for (const ImageRecord &record : images_)
		if (record.image == image && record.aspect == aspect &&
			record.mip == mip && record.layer == layer)
			return &record;
	return nullptr;
}

bool ResourceStateTracker::UseImage(VkImage image,
	const VkImageSubresourceRange &range, const ImageUse &use)
{
	if (image == VK_NULL_HANDLE || range.aspectMask == 0 ||
		range.levelCount == 0 || range.layerCount == 0 || use.stages == 0 ||
		use.layout == VK_IMAGE_LAYOUT_UNDEFINED)
		return false;
	for (uint32_t bit = 1; bit != 0; bit <<= 1)
	{
		if (!(range.aspectMask & bit))
			continue;
		const VkImageAspectFlagBits aspect =
			static_cast<VkImageAspectFlagBits>(bit);
		for (uint32_t mip = range.baseMipLevel;
			mip < range.baseMipLevel + range.levelCount; ++mip)
			for (uint32_t layer = range.baseArrayLayer;
				layer < range.baseArrayLayer + range.layerCount; ++layer)
			{
				ImageRecord *old = FindImage(image, aspect, mip, layer);
				const bool transfer = old && QueueTransfer(old->use.queue_family,
					use.queue_family);
				const bool barrier_needed = !old || transfer ||
					old->use.layout != use.layout || Writes(old->use.intent) ||
					Writes(use.intent);
				if (barrier_needed)
				{
					VkImageMemoryBarrier2 barrier = {
						VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2
					};
					barrier.srcStageMask = old ? old->use.stages :
						VK_PIPELINE_STAGE_2_NONE;
					barrier.srcAccessMask = old ? old->use.access :
						VK_ACCESS_2_NONE;
					barrier.dstStageMask = use.stages;
					barrier.dstAccessMask = use.access;
					barrier.oldLayout = old ? old->use.layout :
						VK_IMAGE_LAYOUT_UNDEFINED;
					barrier.newLayout = use.layout;
					barrier.srcQueueFamilyIndex = transfer ?
						old->use.queue_family : VK_QUEUE_FAMILY_IGNORED;
					barrier.dstQueueFamilyIndex = transfer ? use.queue_family :
						VK_QUEUE_FAMILY_IGNORED;
					barrier.image = image;
					barrier.subresourceRange.aspectMask = aspect;
					barrier.subresourceRange.baseMipLevel = mip;
					barrier.subresourceRange.levelCount = 1;
					barrier.subresourceRange.baseArrayLayer = layer;
					barrier.subresourceRange.layerCount = 1;
					image_barriers_.push_back(barrier);
				}
				if (!old)
				{
					ImageRecord record = {};
					record.image = image;
					record.aspect = aspect;
					record.mip = mip;
					record.layer = layer;
					record.use = use;
					images_.push_back(record);
				}
				else
					old->use = use;
				touched_images_.push_back({ image, aspect, mip, layer });
			}
	}
	return true;
}

void ResourceStateTracker::Flush(VkCommandBuffer command_buffer,
	VkDependencyFlags dependency_flags)
{
	if (command_buffer == VK_NULL_HANDLE ||
		(buffer_barriers_.empty() && image_barriers_.empty()))
		return;
	VkDependencyInfo dependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	dependency.dependencyFlags = dependency_flags;
	dependency.bufferMemoryBarrierCount =
		static_cast<uint32_t>(buffer_barriers_.size());
	dependency.pBufferMemoryBarriers = buffer_barriers_.data();
	dependency.imageMemoryBarrierCount =
		static_cast<uint32_t>(image_barriers_.size());
	dependency.pImageMemoryBarriers = image_barriers_.data();
	vkCmdPipelineBarrier2(command_buffer, &dependency);
	buffer_barriers_.clear();
	image_barriers_.clear();
}

void ResourceStateTracker::StampSubmission(uint64_t timeline_value)
{
	for (const TouchedBuffer &touched : touched_buffers_)
		for (BufferRecord &record : buffers_)
			if (record.buffer == touched.buffer && record.begin < touched.end &&
				record.end > touched.begin)
				record.last_use_timeline = std::max(record.last_use_timeline,
					timeline_value);
	for (const TouchedImage &touched : touched_images_)
	{
		ImageRecord *record = FindImage(touched.image, touched.aspect,
			touched.mip, touched.layer);
		if (record)
			record->last_use_timeline = std::max(record->last_use_timeline,
				timeline_value);
	}
	touched_buffers_.clear();
	touched_images_.clear();
}

ResourceStateSnapshot ResourceStateTracker::Snapshot(uint64_t serial) const
{
	ResourceStateSnapshot result = {};
	result.serial = serial;
	result.buffers = buffers_;
	result.images = images_;
	return result;
}

bool ResourceStateTracker::Restore(const ResourceStateSnapshot &snapshot)
{
	if (snapshot.serial == 0)
		return false;
	buffers_ = snapshot.buffers;
	images_ = snapshot.images;
	BeginRecording();
	return true;
}

uint64_t ResourceStateTracker::LastUse(VkBuffer buffer, VkDeviceSize offset,
	VkDeviceSize size) const
{
	const VkDeviceSize end = RangeEnd(offset, size);
	uint64_t result = 0;
	for (const BufferRecord &record : buffers_)
		if (record.buffer == buffer && record.begin < end && record.end > offset)
			result = std::max(result, record.last_use_timeline);
	return result;
}

uint64_t ResourceStateTracker::LastUse(VkImage image,
	const VkImageSubresourceRange &range) const
{
	uint64_t result = 0;
	for (const ImageRecord &record : images_)
		if (record.image == image && (range.aspectMask & record.aspect) &&
			record.mip >= range.baseMipLevel &&
			record.mip < range.baseMipLevel + range.levelCount &&
			record.layer >= range.baseArrayLayer &&
			record.layer < range.baseArrayLayer + range.layerCount)
			result = std::max(result, record.last_use_timeline);
	return result;
}

} // namespace vk
} // namespace render
} // namespace piccu
