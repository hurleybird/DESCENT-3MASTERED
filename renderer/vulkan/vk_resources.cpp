#define VMA_IMPLEMENTATION
#include "vk_resources.h"
#include "vk_state_tracker.h"

#include <algorithm>
#include <string.h>

namespace piccu
{
namespace render
{
namespace vk
{

template <typename Handle>
static uint64_t ObjectHandleValue(Handle handle) noexcept
{
#if VK_USE_64_BIT_PTR_DEFINES
	return reinterpret_cast<uint64_t>(handle);
#else
	return static_cast<uint64_t>(handle);
#endif
}

ResourceAllocator::ResourceAllocator() = default;

ResourceAllocator::~ResourceAllocator()
{
	Shutdown(false);
}

void ResourceAllocator::SetStateTracker(
	ResourceStateTracker *state_tracker) noexcept
{
	state_tracker_ = state_tracker;
}

bool ResourceAllocator::Initialize(VkInstance instance,
	VkPhysicalDevice physical_device, VkDevice device, uint32_t api_version,
	bool memory_budget_enabled)
{
	if (allocator_ != VK_NULL_HANDLE || instance == VK_NULL_HANDLE ||
		physical_device == VK_NULL_HANDLE || device == VK_NULL_HANDLE)
		return false;

	VmaVulkanFunctions functions = {};
	functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
	functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
	VmaAllocatorCreateInfo create_info = {};
	create_info.flags = memory_budget_enabled ?
		VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT : 0;
	create_info.physicalDevice = physical_device;
	create_info.device = device;
	create_info.instance = instance;
	create_info.vulkanApiVersion = api_version;
	create_info.pVulkanFunctions = &functions;

	const VkResult result = vmaCreateAllocator(&create_info, &allocator_);
	if (result != VK_SUCCESS)
	{
		allocator_ = VK_NULL_HANDLE;
		return false;
	}
	instance_ = instance;
	physical_device_ = physical_device;
	device_ = device;
	return true;
}

void ResourceAllocator::Shutdown(bool) noexcept
{
	if (allocator_ == VK_NULL_HANDLE)
		return;

	for (size_t i = 0; i < retired_images_.size(); ++i)
		DestroyImage(&retired_images_[i].resource);
	for (size_t i = 0; i < retired_buffers_.size(); ++i)
		DestroyBuffer(&retired_buffers_[i].resource);
	retired_images_.clear();
	retired_buffers_.clear();

	vmaDestroyAllocator(allocator_);
	allocator_ = VK_NULL_HANDLE;
	instance_ = VK_NULL_HANDLE;
	physical_device_ = VK_NULL_HANDLE;
	device_ = VK_NULL_HANDLE;
	live_buffer_count_ = 0;
	live_image_count_ = 0;
}

bool ResourceAllocator::Ready() const noexcept
{
	return allocator_ != VK_NULL_HANDLE;
}

bool ResourceAllocator::CreateBuffer(const BufferCreateRequest &request,
	AllocatedBuffer *output)
{
	if (!output || allocator_ == VK_NULL_HANDLE || request.size == 0 ||
		request.usage == 0 || request.minimum_alignment == 0)
		return false;

	VkBufferCreateInfo buffer_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	buffer_info.size = request.size;
	buffer_info.usage = request.usage;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo allocation_info = {};
	allocation_info.usage = VMA_MEMORY_USAGE_AUTO;
	switch (request.memory_class)
	{
	case BufferMemoryClass::DeviceLocal:
		allocation_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
		break;
	case BufferMemoryClass::Upload:
		allocation_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
			VMA_ALLOCATION_CREATE_MAPPED_BIT;
		allocation_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
		break;
	case BufferMemoryClass::Readback:
		allocation_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
			VMA_ALLOCATION_CREATE_MAPPED_BIT;
		allocation_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
		break;
	}

	AllocatedBuffer created = {};
	VkResult result = vmaCreateBufferWithAlignment(allocator_, &buffer_info,
		&allocation_info, request.minimum_alignment, &created.handle,
		&created.allocation, &created.allocation_info);
	if (result != VK_SUCCESS)
		return false;
	created.size = request.size;
	created.usage = request.usage;
	created.memory_class = request.memory_class;
	created.mapped = created.allocation_info.pMappedData;
	*output = created;
	if (state_tracker_)
		state_tracker_->ForgetBuffer(created.handle);
	++live_buffer_count_;
	NameObject(VK_OBJECT_TYPE_BUFFER,
		ObjectHandleValue(created.handle), request.debug_name);
	return true;
}

bool ResourceAllocator::CreateImage(const ImageCreateRequest &request,
	AllocatedImage *output)
{
	if (!output || allocator_ == VK_NULL_HANDLE ||
		request.format == VK_FORMAT_UNDEFINED || request.extent.width == 0 ||
		request.extent.height == 0 || request.extent.depth == 0 ||
		request.mip_levels == 0 || request.array_layers == 0 ||
		request.usage == 0 || request.aspect == 0)
		return false;

	VkImageCreateInfo image_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	image_info.flags = request.flags;
	image_info.imageType = request.image_type;
	image_info.format = request.format;
	image_info.extent = request.extent;
	image_info.mipLevels = request.mip_levels;
	image_info.arrayLayers = request.array_layers;
	image_info.samples = request.samples;
	image_info.tiling = request.tiling;
	image_info.usage = request.usage;
	image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VmaAllocationCreateInfo allocation_info = {};
	allocation_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
	AllocatedImage created = {};
	VkResult result = vmaCreateImage(allocator_, &image_info, &allocation_info,
		&created.handle, &created.allocation, nullptr);
	if (result != VK_SUCCESS)
		return false;

	VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	view_info.image = created.handle;
	view_info.viewType = request.view_type;
	view_info.format = request.format;
	view_info.subresourceRange.aspectMask = request.aspect;
	view_info.subresourceRange.baseMipLevel = 0;
	view_info.subresourceRange.levelCount = request.mip_levels;
	view_info.subresourceRange.baseArrayLayer = 0;
	view_info.subresourceRange.layerCount = request.array_layers;
	result = vkCreateImageView(device_, &view_info, nullptr, &created.view);
	if (result != VK_SUCCESS)
	{
		vmaDestroyImage(allocator_, created.handle, created.allocation);
		return false;
	}

	created.format = request.format;
	created.extent = request.extent;
	created.mip_levels = request.mip_levels;
	created.array_layers = request.array_layers;
	created.samples = request.samples;
	created.aspect = request.aspect;
	created.usage = request.usage;
	*output = created;
	if (state_tracker_)
		state_tracker_->ForgetImage(created.handle);
	++live_image_count_;
	NameObject(VK_OBJECT_TYPE_IMAGE,
		ObjectHandleValue(created.handle), request.debug_name);
	return true;
}

void ResourceAllocator::RetireBuffer(AllocatedBuffer *resource,
	uint64_t last_use_timeline)
{
	if (!resource || !resource->Valid())
		return;
	resource->last_use_timeline = std::max(resource->last_use_timeline,
		last_use_timeline);
	RetiredBuffer retired = {};
	retired.resource = *resource;
	retired.retire_after = retired.resource.last_use_timeline;
	retired_buffers_.push_back(retired);
	*resource = AllocatedBuffer();
}

void ResourceAllocator::RetireImage(AllocatedImage *resource,
	uint64_t last_use_timeline)
{
	if (!resource || !resource->Valid())
		return;
	resource->last_use_timeline = std::max(resource->last_use_timeline,
		last_use_timeline);
	RetiredImage retired = {};
	retired.resource = *resource;
	retired.retire_after = retired.resource.last_use_timeline;
	retired_images_.push_back(retired);
	*resource = AllocatedImage();
}

void ResourceAllocator::Reclaim(uint64_t completed_timeline)
{
	for (size_t i = 0; i < retired_images_.size();)
	{
		if (retired_images_[i].retire_after > completed_timeline)
		{
			++i;
			continue;
		}
		DestroyImage(&retired_images_[i].resource);
		retired_images_[i] = retired_images_.back();
		retired_images_.pop_back();
	}
	for (size_t i = 0; i < retired_buffers_.size();)
	{
		if (retired_buffers_[i].retire_after > completed_timeline)
		{
			++i;
			continue;
		}
		DestroyBuffer(&retired_buffers_[i].resource);
		retired_buffers_[i] = retired_buffers_.back();
		retired_buffers_.pop_back();
	}
}

bool ResourceAllocator::Flush(const AllocatedBuffer &buffer,
	VkDeviceSize offset, VkDeviceSize size)
{
	if (allocator_ == VK_NULL_HANDLE || !buffer.Valid() || !buffer.mapped ||
		offset > buffer.size || size > buffer.size - offset)
		return false;
	return vmaFlushAllocation(allocator_, buffer.allocation, offset, size) == VK_SUCCESS;
}

bool ResourceAllocator::Invalidate(const AllocatedBuffer &buffer,
	VkDeviceSize offset, VkDeviceSize size)
{
	if (allocator_ == VK_NULL_HANDLE || !buffer.Valid() || !buffer.mapped ||
		offset > buffer.size || size > buffer.size - offset)
		return false;
	return vmaInvalidateAllocation(allocator_, buffer.allocation, offset, size) == VK_SUCCESS;
}

HeapBudgetSnapshot ResourceAllocator::QueryBudgets() const
{
	HeapBudgetSnapshot result = {};
	if (allocator_ == VK_NULL_HANDLE)
		return result;
	VkPhysicalDeviceMemoryProperties properties = {};
	vkGetPhysicalDeviceMemoryProperties(physical_device_, &properties);
	VmaBudget budgets[VK_MAX_MEMORY_HEAPS] = {};
	vmaGetHeapBudgets(allocator_, budgets);
	result.heap_count = properties.memoryHeapCount;
	for (uint32_t i = 0; i < properties.memoryHeapCount; ++i)
	{
		result.block_bytes[i] = budgets[i].statistics.blockBytes;
		result.allocation_bytes[i] = budgets[i].statistics.allocationBytes;
		result.usage_bytes[i] = budgets[i].usage;
		result.budget_bytes[i] = budgets[i].budget;
	}
	return result;
}

void ResourceAllocator::DestroyBuffer(AllocatedBuffer *resource) noexcept
{
	if (!resource || !resource->Valid() || allocator_ == VK_NULL_HANDLE)
		return;
	if (state_tracker_)
		state_tracker_->ForgetBuffer(resource->handle);
	vmaDestroyBuffer(allocator_, resource->handle, resource->allocation);
	*resource = AllocatedBuffer();
	if (live_buffer_count_ != 0)
		--live_buffer_count_;
}

void ResourceAllocator::DestroyImage(AllocatedImage *resource) noexcept
{
	if (!resource || !resource->Valid() || allocator_ == VK_NULL_HANDLE)
		return;
	if (state_tracker_)
		state_tracker_->ForgetImage(resource->handle);
	if (resource->view != VK_NULL_HANDLE)
		vkDestroyImageView(device_, resource->view, nullptr);
	vmaDestroyImage(allocator_, resource->handle, resource->allocation);
	*resource = AllocatedImage();
	if (live_image_count_ != 0)
		--live_image_count_;
}

void ResourceAllocator::NameObject(VkObjectType type, uint64_t handle,
	const char *name) const
{
	if (!name || !name[0] || !vkSetDebugUtilsObjectNameEXT ||
		device_ == VK_NULL_HANDLE)
		return;
	VkDebugUtilsObjectNameInfoEXT info = {
		VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT
	};
	info.objectType = type;
	info.objectHandle = handle;
	info.pObjectName = name;
	vkSetDebugUtilsObjectNameEXT(device_, &info);
}

} // namespace vk
} // namespace render
} // namespace piccu
