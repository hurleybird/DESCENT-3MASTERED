/* VMA-backed buffers, images, mapped ranges, budgets, and timeline retirement. */
#pragma once

#include "vk_common.h"

#ifndef VMA_STATIC_VULKAN_FUNCTIONS
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#endif
#ifndef VMA_DYNAMIC_VULKAN_FUNCTIONS
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#endif
#include <vk_mem_alloc.h>

#include <stdint.h>
#include <vector>

namespace piccu
{
namespace render
{
namespace vk
{

class ResourceStateTracker;

enum class BufferMemoryClass : uint32_t
{
	DeviceLocal = 0,
	Upload,
	Readback,
};

struct AllocatedBuffer
{
	VkBuffer handle = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	VmaAllocationInfo allocation_info = {};
	VkDeviceSize size = 0;
	VkBufferUsageFlags usage = 0;
	BufferMemoryClass memory_class = BufferMemoryClass::DeviceLocal;
	uint64_t last_use_timeline = 0;
	void *mapped = nullptr;

	bool Valid() const noexcept { return handle != VK_NULL_HANDLE; }
};

struct AllocatedImage
{
	VkImage handle = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	VkFormat format = VK_FORMAT_UNDEFINED;
	VkExtent3D extent = {};
	uint32_t mip_levels = 0;
	uint32_t array_layers = 0;
	VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
	VkImageAspectFlags aspect = 0;
	VkImageUsageFlags usage = 0;
	uint64_t last_use_timeline = 0;

	bool Valid() const noexcept { return handle != VK_NULL_HANDLE; }
};

struct BufferCreateRequest
{
	VkDeviceSize size = 0;
	VkBufferUsageFlags usage = 0;
	BufferMemoryClass memory_class = BufferMemoryClass::DeviceLocal;
	VkDeviceSize minimum_alignment = 1;
	const char *debug_name = nullptr;
};

struct ImageCreateRequest
{
	VkImageType image_type = VK_IMAGE_TYPE_2D;
	VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_2D;
	VkFormat format = VK_FORMAT_UNDEFINED;
	VkExtent3D extent = { 1, 1, 1 };
	uint32_t mip_levels = 1;
	uint32_t array_layers = 1;
	VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
	VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
	VkImageUsageFlags usage = 0;
	VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	VkImageCreateFlags flags = 0;
	const char *debug_name = nullptr;
};

struct HeapBudgetSnapshot
{
	uint32_t heap_count = 0;
	VkDeviceSize block_bytes[VK_MAX_MEMORY_HEAPS] = {};
	VkDeviceSize allocation_bytes[VK_MAX_MEMORY_HEAPS] = {};
	VkDeviceSize usage_bytes[VK_MAX_MEMORY_HEAPS] = {};
	VkDeviceSize budget_bytes[VK_MAX_MEMORY_HEAPS] = {};
};

class ResourceAllocator final
{
public:
	ResourceAllocator();
	~ResourceAllocator();

	ResourceAllocator(const ResourceAllocator &) = delete;
	ResourceAllocator &operator=(const ResourceAllocator &) = delete;

	bool Initialize(VkInstance instance, VkPhysicalDevice physical_device,
		VkDevice device, uint32_t api_version, bool memory_budget_enabled);
	void SetStateTracker(ResourceStateTracker *state_tracker) noexcept;
	void Shutdown(bool device_lost = false) noexcept;
	bool Ready() const noexcept;

	bool CreateBuffer(const BufferCreateRequest &request, AllocatedBuffer *output);
	bool CreateImage(const ImageCreateRequest &request, AllocatedImage *output);

	// Ownership transfers into the allocator's death row and clears resource.
	void RetireBuffer(AllocatedBuffer *resource, uint64_t last_use_timeline);
	void RetireImage(AllocatedImage *resource, uint64_t last_use_timeline);
	void Reclaim(uint64_t completed_timeline);

	bool Flush(const AllocatedBuffer &buffer, VkDeviceSize offset,
		VkDeviceSize size);
	bool Invalidate(const AllocatedBuffer &buffer, VkDeviceSize offset,
		VkDeviceSize size);
	HeapBudgetSnapshot QueryBudgets() const;

	VmaAllocator Handle() const noexcept { return allocator_; }
	VkPhysicalDevice PhysicalDevice() const noexcept { return physical_device_; }
	VkDevice Device() const noexcept { return device_; }
	uint64_t LiveBufferCount() const noexcept { return live_buffer_count_; }
	uint64_t LiveImageCount() const noexcept { return live_image_count_; }
	uint64_t RetiredObjectCount() const noexcept
	{
		return static_cast<uint64_t>(retired_buffers_.size() + retired_images_.size());
	}

private:
	struct RetiredBuffer
	{
		AllocatedBuffer resource;
		uint64_t retire_after = 0;
	};
	struct RetiredImage
	{
		AllocatedImage resource;
		uint64_t retire_after = 0;
	};

	void DestroyBuffer(AllocatedBuffer *resource) noexcept;
	void DestroyImage(AllocatedImage *resource) noexcept;
	void NameObject(VkObjectType type, uint64_t handle, const char *name) const;

	VmaAllocator allocator_ = VK_NULL_HANDLE;
	VkInstance instance_ = VK_NULL_HANDLE;
	VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
	VkDevice device_ = VK_NULL_HANDLE;
	ResourceStateTracker *state_tracker_ = nullptr;
	uint64_t live_buffer_count_ = 0;
	uint64_t live_image_count_ = 0;
	std::vector<RetiredBuffer> retired_buffers_;
	std::vector<RetiredImage> retired_images_;
};

} // namespace vk
} // namespace render
} // namespace piccu
