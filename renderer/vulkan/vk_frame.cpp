#include "vk_frame.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace piccu
{
namespace render
{
namespace vk
{

namespace
{

const char *BufferName(FrameBufferClass value)
{
	switch (value)
	{
	case FrameBufferClass::Vertex: return "vk.frame.vertex";
	case FrameBufferClass::Index: return "vk.frame.index";
	case FrameBufferClass::Storage: return "vk.frame.storage";
	case FrameBufferClass::Indirect: return "vk.frame.indirect";
	case FrameBufferClass::Readback: return "vk.frame.readback";
	default: return "vk.frame.unknown";
	}
}

VkBufferUsageFlags BufferUsage(FrameBufferClass value)
{
	const VkBufferUsageFlags transfer = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	switch (value)
	{
	case FrameBufferClass::Vertex:
		return transfer | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	case FrameBufferClass::Index:
		return transfer | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	case FrameBufferClass::Storage:
		return transfer | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	case FrameBufferClass::Indirect:
		return transfer | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	case FrameBufferClass::Readback:
		return transfer;
	default:
		return 0;
	}
}

BufferMemoryClass MemoryClass(FrameBufferClass value)
{
	return value == FrameBufferClass::Readback ? BufferMemoryClass::Readback :
		BufferMemoryClass::DeviceLocal;
}

VkDeviceSize ConfiguredCapacity(const FrameSchedulerConfig &config,
	FrameBufferClass value)
{
	switch (value)
	{
	case FrameBufferClass::Vertex: return config.initial_vertex_bytes;
	case FrameBufferClass::Index: return config.initial_index_bytes;
	case FrameBufferClass::Storage: return config.initial_storage_bytes;
	case FrameBufferClass::Indirect: return config.initial_indirect_bytes;
	case FrameBufferClass::Readback: return config.initial_readback_bytes;
	default: return 0;
	}
}

} // namespace

FrameScheduler::FrameScheduler()
	: platform_(nullptr), allocator_(nullptr), current_(nullptr), next_slot_(0),
	  next_timeline_value_(0), completed_timeline_(0), ready_(false)
{
}

FrameScheduler::~FrameScheduler()
{
	Shutdown(platform_ && platform_->DeviceLost());
}

bool FrameScheduler::Initialize(Platform *platform, ResourceAllocator *allocator,
	const FrameSchedulerConfig &config)
{
	if (ready_ || !platform || !allocator || !platform->Ready() ||
		!allocator->Ready())
		return false;
	platform_ = platform;
	allocator_ = allocator;
	config_ = config;
	for (uint32_t i = 0; i < kFrameContextCount; ++i)
	{
		if (!CreateContext(i, config_))
		{
			Shutdown(false);
			return false;
		}
	}
	ready_ = true;
	return true;
}

void FrameScheduler::Shutdown(bool device_lost) noexcept
{
	if (!platform_ && !allocator_)
		return;
	if (!device_lost && platform_ && platform_->Ready() && next_timeline_value_)
		WaitTimeline(next_timeline_value_, UINT64_MAX);
	for (uint32_t i = 0; i < kFrameContextCount; ++i)
		DestroyContext(&contexts_[i], device_lost);
	if (allocator_ && allocator_->Ready() && !device_lost)
		allocator_->Reclaim(std::numeric_limits<uint64_t>::max());
	platform_ = nullptr;
	allocator_ = nullptr;
	current_ = nullptr;
	next_slot_ = 0;
	next_timeline_value_ = 0;
	completed_timeline_ = 0;
	ready_ = false;
}

bool FrameScheduler::Ready() const noexcept
{
	return ready_ && platform_ && platform_->Ready() && allocator_ &&
		allocator_->Ready();
}

VkDeviceSize FrameScheduler::AlignUp(VkDeviceSize value, VkDeviceSize alignment)
{
	if (alignment <= 1)
		return value;
	const VkDeviceSize remainder = value % alignment;
	if (!remainder)
		return value;
	const VkDeviceSize delta = alignment - remainder;
	return value > std::numeric_limits<VkDeviceSize>::max() - delta ?
		std::numeric_limits<VkDeviceSize>::max() : value + delta;
}

VkDeviceSize FrameScheduler::GrowCapacity(VkDeviceSize current,
	VkDeviceSize required)
{
	VkDeviceSize capacity = std::max<VkDeviceSize>(current, 64u * 1024u);
	while (capacity < required)
	{
		const VkDeviceSize increment = std::max(capacity / 2,
			VkDeviceSize(64u * 1024u));
		if (capacity > std::numeric_limits<VkDeviceSize>::max() - increment)
			return required;
		capacity = AlignUp(capacity + increment, 64u * 1024u);
	}
	return capacity;
}

uint32_t FrameScheduler::GrowCount(uint32_t current, uint32_t required)
{
	uint32_t capacity = std::max(current, 16u);
	while (capacity < required)
	{
		const uint32_t increment = std::max(capacity / 2u, 16u);
		if (capacity > std::numeric_limits<uint32_t>::max() - increment)
			return required;
		capacity += increment;
	}
	return capacity;
}

bool FrameScheduler::CreateContext(uint32_t slot,
	const FrameSchedulerConfig &config)
{
	FrameContext &context = contexts_[slot];
	context.frame_slot = slot;
	VkCommandPoolCreateInfo pool_info = {
		VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO
	};
	pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT |
		VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	pool_info.queueFamilyIndex = platform_->GraphicsQueueFamily();
	if (vkCreateCommandPool(platform_->Device(), &pool_info, nullptr,
		&context.command_pool) != VK_SUCCESS)
		return false;

	VkCommandBufferAllocateInfo allocate = {
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO
	};
	allocate.commandPool = context.command_pool;
	allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocate.commandBufferCount = 2;
	VkCommandBuffer buffers[2] = {};
	if (vkAllocateCommandBuffers(platform_->Device(), &allocate, buffers) !=
		VK_SUCCESS)
		return false;
	context.primary = buffers[0];
	context.continuation = buffers[1];

	if (!EnsureUpload(&context, std::max<VkDeviceSize>(1,
		config.initial_upload_bytes), 0))
		return false;
	for (uint32_t i = 0; i < static_cast<uint32_t>(FrameBufferClass::Count); ++i)
	{
		const FrameBufferClass value = static_cast<FrameBufferClass>(i);
		if (!EnsureBuffer(&context, value,
			std::max<VkDeviceSize>(1, ConfiguredCapacity(config, value)), 0))
			return false;
	}
	if (!EnsureDescriptorPool(&context,
		std::max(1u, config.initial_descriptor_sets),
		std::max(1u, config.initial_descriptor_sets * 16u), 0))
		return false;
	if (!EnsureQueryPool(&context,
		std::max(1u, config.initial_timestamp_queries), 0))
		return false;
	return true;
}

void FrameScheduler::DestroyContext(FrameContext *context,
	bool device_lost) noexcept
{
	if (!context)
		return;
	const VkDevice device = platform_ ? platform_->Device() : VK_NULL_HANDLE;
	if (device != VK_NULL_HANDLE && !device_lost)
	{
		if (context->timestamp_pool)
			vkDestroyQueryPool(device, context->timestamp_pool, nullptr);
		if (context->descriptor_pool)
			vkDestroyDescriptorPool(device, context->descriptor_pool, nullptr);
		if (context->command_pool)
			vkDestroyCommandPool(device, context->command_pool, nullptr);
	}
	if (allocator_ && allocator_->Ready() && !device_lost)
	{
		allocator_->RetireBuffer(&context->upload, context->last_submitted_timeline);
		for (uint32_t i = 0; i < static_cast<uint32_t>(FrameBufferClass::Count); ++i)
			allocator_->RetireBuffer(&context->buffers[i],
				context->last_submitted_timeline);
	}
	*context = FrameContext();
}

VkDeviceSize FrameScheduler::BufferCapacity(const FrameContext &context,
	FrameBufferClass buffer_class) const
{
	return context.buffers[static_cast<uint32_t>(buffer_class)].size;
}

bool FrameScheduler::EnsureUpload(FrameContext *context,
	VkDeviceSize required_size, uint64_t retire_after)
{
	if (context->upload.size >= required_size)
		return true;
	BufferCreateRequest request = {};
	request.size = GrowCapacity(context->upload.size, required_size);
	request.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	request.memory_class = BufferMemoryClass::Upload;
	request.minimum_alignment = 16;
	request.debug_name = "vk.frame.upload";
	AllocatedBuffer replacement = {};
	if (!allocator_->CreateBuffer(request, &replacement))
		return false;
	allocator_->RetireBuffer(&context->upload, retire_after);
	context->upload = replacement;
	return true;
}

bool FrameScheduler::EnsureBuffer(FrameContext *context,
	FrameBufferClass buffer_class, VkDeviceSize required_size,
	uint64_t retire_after)
{
	AllocatedBuffer &buffer = context->buffers[
		static_cast<uint32_t>(buffer_class)];
	if (buffer.size >= required_size)
		return true;
	BufferCreateRequest request = {};
	request.size = GrowCapacity(buffer.size, required_size);
	request.usage = BufferUsage(buffer_class);
	request.memory_class = MemoryClass(buffer_class);
	request.minimum_alignment = 16;
	request.debug_name = BufferName(buffer_class);
	AllocatedBuffer replacement = {};
	if (!allocator_->CreateBuffer(request, &replacement))
		return false;
	allocator_->RetireBuffer(&buffer, retire_after);
	buffer = replacement;
	return true;
}

bool FrameScheduler::EnsureDescriptorPool(FrameContext *context,
	uint32_t required_sets, uint32_t required_sampled_images,
	uint64_t retire_after)
{
	if (context->descriptor_pool && context->descriptor_capacity >= required_sets &&
		context->descriptor_sampled_image_capacity >= required_sampled_images)
		return true;
	if (context->descriptor_pool)
	{
		if (retire_after && !WaitTimeline(retire_after, UINT64_MAX))
			return false;
		vkDestroyDescriptorPool(platform_->Device(), context->descriptor_pool,
			nullptr);
		context->descriptor_pool = VK_NULL_HANDLE;
	}
	const uint32_t capacity = GrowCount(context->descriptor_capacity,
		required_sets);
	const uint32_t sampled_image_capacity = GrowCount(
		context->descriptor_sampled_image_capacity,
		std::max(required_sampled_images, capacity * 16u));
	VkDescriptorPoolSize sizes[] = {
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, capacity * 4u },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, capacity * 4u },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, capacity * 8u },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, sampled_image_capacity },
		{ VK_DESCRIPTOR_TYPE_SAMPLER, capacity * 8u },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sampled_image_capacity },
	};
	VkDescriptorPoolCreateInfo info = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
	};
	info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	info.maxSets = capacity;
	info.poolSizeCount = static_cast<uint32_t>(sizeof(sizes) / sizeof(sizes[0]));
	info.pPoolSizes = sizes;
	if (vkCreateDescriptorPool(platform_->Device(), &info, nullptr,
		&context->descriptor_pool) != VK_SUCCESS)
		return false;
	context->descriptor_capacity = capacity;
	context->descriptor_sampled_image_capacity = sampled_image_capacity;
	return true;
}

bool FrameScheduler::EnsureQueryPool(FrameContext *context,
	uint32_t required_queries, uint64_t retire_after)
{
	if (context->timestamp_pool && context->timestamp_capacity >= required_queries)
		return true;
	if (context->timestamp_pool)
	{
		if (retire_after && !WaitTimeline(retire_after, UINT64_MAX))
			return false;
		vkDestroyQueryPool(platform_->Device(), context->timestamp_pool, nullptr);
		context->timestamp_pool = VK_NULL_HANDLE;
	}
	const uint32_t capacity = GrowCount(context->timestamp_capacity,
		required_queries);
	VkQueryPoolCreateInfo info = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
	info.queryType = VK_QUERY_TYPE_TIMESTAMP;
	info.queryCount = capacity;
	if (vkCreateQueryPool(platform_->Device(), &info, nullptr,
		&context->timestamp_pool) != VK_SUCCESS)
		return false;
	context->timestamp_capacity = capacity;
	return true;
}

bool FrameScheduler::Reserve(const FrameRequirements &requirements)
{
	if (!Ready() || (current_ && current_->recording))
		return false;
	for (uint32_t i = 0; i < kFrameContextCount; ++i)
	{
		FrameContext &context = contexts_[i];
		const uint64_t retire = context.last_submitted_timeline;
		if (!EnsureUpload(&context, std::max<VkDeviceSize>(1,
			requirements.upload_bytes), retire) ||
			!EnsureBuffer(&context, FrameBufferClass::Vertex,
				std::max<VkDeviceSize>(1, requirements.vertex_bytes), retire) ||
			!EnsureBuffer(&context, FrameBufferClass::Index,
				std::max<VkDeviceSize>(1, requirements.index_bytes), retire) ||
			!EnsureBuffer(&context, FrameBufferClass::Storage,
				std::max<VkDeviceSize>(1, requirements.storage_bytes), retire) ||
			!EnsureBuffer(&context, FrameBufferClass::Indirect,
				std::max<VkDeviceSize>(1, requirements.indirect_bytes), retire) ||
			!EnsureBuffer(&context, FrameBufferClass::Readback,
				std::max<VkDeviceSize>(1, requirements.readback_bytes), retire) ||
			!EnsureDescriptorPool(&context,
				std::max(1u, requirements.descriptor_sets),
				std::max(1u, requirements.descriptor_sampled_images), retire) ||
			!EnsureQueryPool(&context,
				std::max(1u, requirements.timestamp_queries), retire))
			return false;
	}
	return true;
}

uint64_t FrameScheduler::PollCompletedTimeline()
{
	if (!Ready())
		return completed_timeline_;
	uint64_t value = 0;
	const VkResult result = vkGetSemaphoreCounterValue(platform_->Device(),
		platform_->TimelineSemaphore(), &value);
	platform_->NotifyDeviceResult(result);
	if (result == VK_SUCCESS)
	{
		completed_timeline_ = std::max(completed_timeline_, value);
		allocator_->Reclaim(completed_timeline_);
	}
	return completed_timeline_;
}

bool FrameScheduler::WaitTimeline(uint64_t value, uint64_t timeout_nanoseconds)
{
	if (!Ready() || value == 0 || value <= PollCompletedTimeline())
		return Ready();
	VkSemaphore semaphore = platform_->TimelineSemaphore();
	VkSemaphoreWaitInfo wait = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
	wait.semaphoreCount = 1;
	wait.pSemaphores = &semaphore;
	wait.pValues = &value;
	const VkResult result = vkWaitSemaphores(platform_->Device(), &wait,
		timeout_nanoseconds);
	platform_->NotifyDeviceResult(result);
	if (result == VK_SUCCESS)
	{
		completed_timeline_ = std::max(completed_timeline_, value);
		allocator_->Reclaim(completed_timeline_);
		return true;
	}
	return false;
}

bool FrameScheduler::ConfirmExternalSubmission(uint64_t timeline_value)
{
	if (!Ready() || timeline_value == 0 ||
		timeline_value <= next_timeline_value_)
		return false;
	next_timeline_value_ = timeline_value;
	return true;
}

FrameContext *FrameScheduler::BeginFrame(uint64_t timeout_nanoseconds)
{
	if (!Ready() || (current_ && current_->recording))
		return nullptr;
	FrameContext &context = contexts_[next_slot_];
	if (context.last_submitted_timeline &&
		!WaitTimeline(context.last_submitted_timeline, timeout_nanoseconds))
		return nullptr;
	const VkDevice device = platform_->Device();
	VkResult result = vkResetCommandPool(device, context.command_pool, 0);
	platform_->NotifyDeviceResult(result);
	if (result != VK_SUCCESS)
		return nullptr;
	result = vkResetDescriptorPool(device, context.descriptor_pool, 0);
	platform_->NotifyDeviceResult(result);
	if (result != VK_SUCCESS)
		return nullptr;
	context.upload_head = 0;
	std::memset(context.buffer_heads, 0, sizeof(context.buffer_heads));
	context.abandoned = false;
	VkCommandBufferBeginInfo begin = {
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
	};
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = vkBeginCommandBuffer(context.primary, &begin);
	platform_->NotifyDeviceResult(result);
	if (result != VK_SUCCESS)
		return nullptr;
	vkCmdResetQueryPool(context.primary, context.timestamp_pool, 0,
		context.timestamp_capacity);
	context.recording = true;
	context.recording_command = context.primary;
	context.executable_command = VK_NULL_HANDLE;
	current_ = &context;
	next_slot_ = (next_slot_ + 1) % kFrameContextCount;
	return current_;
}

VkCommandBuffer FrameScheduler::BeginContinuation()
{
	if (!Ready() || !current_ || current_->recording)
		return VK_NULL_HANDLE;
	if (current_->last_submitted_timeline &&
		!WaitTimeline(current_->last_submitted_timeline, UINT64_MAX))
		return VK_NULL_HANDLE;
	const VkResult reset = vkResetCommandBuffer(current_->continuation, 0);
	platform_->NotifyDeviceResult(reset);
	if (reset != VK_SUCCESS)
		return VK_NULL_HANDLE;
	VkCommandBufferBeginInfo begin = {
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
	};
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	const VkResult result = vkBeginCommandBuffer(current_->continuation, &begin);
	platform_->NotifyDeviceResult(result);
	if (result != VK_SUCCESS)
		return VK_NULL_HANDLE;
	current_->recording = true;
	current_->recording_command = current_->continuation;
	return current_->continuation;
}

bool FrameScheduler::EndRecording()
{
	if (!Ready() || !current_ || !current_->recording)
		return false;
	const VkCommandBuffer active = current_->recording_command;
	if (active == VK_NULL_HANDLE)
		return false;
	const VkResult result = vkEndCommandBuffer(active);
	platform_->NotifyDeviceResult(result);
	current_->recording = false;
	current_->recording_command = VK_NULL_HANDLE;
	if (result != VK_SUCCESS)
	{
		current_->abandoned = true;
		return false;
	}
	current_->executable_command = active;
	return true;
}

void FrameScheduler::AbandonCurrentRecording() noexcept
{
	if (!current_)
		return;
	current_->recording = false;
	current_->recording_command = VK_NULL_HANDLE;
	current_->executable_command = VK_NULL_HANDLE;
	current_->abandoned = true;
	current_ = nullptr;
}

bool FrameScheduler::Submit(const FrameSubmitInfo &submit,
	uint64_t *signaled_timeline)
{
	if (!Ready() || !current_ || current_->recording || current_->abandoned)
		return false;
	const uint64_t timeline_value = submit.timeline_value ? submit.timeline_value :
		next_timeline_value_ + 1;
	if (timeline_value <= next_timeline_value_)
		return false;
	const VkCommandBuffer command = current_->executable_command;
	if (command == VK_NULL_HANDLE)
		return false;

	VkSemaphoreSubmitInfo waits[1] = {};
	uint32_t wait_count = 0;
	if (submit.wait_binary)
	{
		waits[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
		waits[0].semaphore = submit.wait_binary;
		waits[0].stageMask = submit.wait_stage;
		waits[0].deviceIndex = 0;
		wait_count = 1;
	}
	VkSemaphoreSubmitInfo signals[2] = {};
	signals[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	signals[0].semaphore = platform_->TimelineSemaphore();
	signals[0].value = timeline_value;
	signals[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	uint32_t signal_count = 1;
	if (submit.signal_binary)
	{
		signals[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
		signals[1].semaphore = submit.signal_binary;
		signals[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		signal_count = 2;
	}
	VkCommandBufferSubmitInfo command_info = {
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO
	};
	command_info.commandBuffer = command;
	VkSubmitInfo2 info = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
	info.waitSemaphoreInfoCount = wait_count;
	info.pWaitSemaphoreInfos = waits;
	info.commandBufferInfoCount = 1;
	info.pCommandBufferInfos = &command_info;
	info.signalSemaphoreInfoCount = signal_count;
	info.pSignalSemaphoreInfos = signals;
	const VkResult result = vkQueueSubmit2(platform_->GraphicsQueue(), 1, &info,
		VK_NULL_HANDLE);
	platform_->NotifyDeviceResult(result);
	if (result != VK_SUCCESS)
	{
		current_->abandoned = true;
		return false;
	}
	next_timeline_value_ = timeline_value;
	current_->last_submitted_timeline = timeline_value;
	current_->executable_command = VK_NULL_HANDLE;
	current_->upload.last_use_timeline = timeline_value;
	for (uint32_t i = 0; i < static_cast<uint32_t>(FrameBufferClass::Count); ++i)
		current_->buffers[i].last_use_timeline = timeline_value;
	if (signaled_timeline)
		*signaled_timeline = timeline_value;
	return true;
}

FrameBufferSlice FrameScheduler::AllocateUpload(VkDeviceSize size,
	VkDeviceSize alignment)
{
	FrameBufferSlice result = {};
	if (!current_ || !current_->recording || size == 0 || alignment == 0)
		return result;
	const VkDeviceSize offset = AlignUp(current_->upload_head, alignment);
	if (offset > current_->upload.size || size > current_->upload.size - offset)
		return result;
	result.buffer = current_->upload.handle;
	result.offset = offset;
	result.size = size;
	result.mapped = static_cast<uint8_t *>(current_->upload.mapped) + offset;
	current_->upload_head = offset + size;
	return result;
}

FrameBufferSlice FrameScheduler::Allocate(FrameBufferClass buffer_class,
	VkDeviceSize size, VkDeviceSize alignment)
{
	FrameBufferSlice result = {};
	if (!current_ || !current_->recording || size == 0 || alignment == 0)
		return result;
	const uint32_t index = static_cast<uint32_t>(buffer_class);
	if (index >= static_cast<uint32_t>(FrameBufferClass::Count))
		return result;
	AllocatedBuffer &buffer = current_->buffers[index];
	const VkDeviceSize offset = AlignUp(current_->buffer_heads[index], alignment);
	if (offset > buffer.size || size > buffer.size - offset)
		return result;
	result.buffer = buffer.handle;
	result.offset = offset;
	result.size = size;
	result.mapped = buffer.mapped ? static_cast<uint8_t *>(buffer.mapped) + offset :
		nullptr;
	current_->buffer_heads[index] = offset + size;
	return result;
}

bool FrameScheduler::StageBuffer(const void *source, VkDeviceSize size,
	FrameBufferClass destination_class, VkDeviceSize destination_alignment,
	FrameBufferSlice *destination)
{
	if (!source || !destination || !current_ || !current_->recording || size == 0)
		return false;
	FrameBufferSlice upload = AllocateUpload(size, 16);
	FrameBufferSlice target = Allocate(destination_class, size,
		destination_alignment);
	if (!upload.Valid() || !upload.mapped || !target.Valid())
		return false;
	std::memcpy(upload.mapped, source, static_cast<size_t>(size));
	if (!allocator_->Flush(current_->upload, upload.offset, upload.size))
		return false;
	VkBufferCopy2 region = { VK_STRUCTURE_TYPE_BUFFER_COPY_2 };
	region.srcOffset = upload.offset;
	region.dstOffset = target.offset;
	region.size = size;
	VkCopyBufferInfo2 copy = { VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2 };
	copy.srcBuffer = upload.buffer;
	copy.dstBuffer = target.buffer;
	copy.regionCount = 1;
	copy.pRegions = &region;
	vkCmdCopyBuffer2(current_->recording_command, &copy);
	*destination = target;
	return true;
}

} // namespace vk
} // namespace render
} // namespace piccu
