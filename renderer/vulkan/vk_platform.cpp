#define VOLK_IMPLEMENTATION
#include "vk_platform.h"

#if defined(SDL3)
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_vulkan.h>
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <utility>

namespace piccu
{
namespace render
{
namespace vk
{

const char *ResultName(VkResult result) noexcept
{
	switch (result)
	{
	case VK_SUCCESS: return "VK_SUCCESS";
	case VK_NOT_READY: return "VK_NOT_READY";
	case VK_TIMEOUT: return "VK_TIMEOUT";
	case VK_EVENT_SET: return "VK_EVENT_SET";
	case VK_EVENT_RESET: return "VK_EVENT_RESET";
	case VK_INCOMPLETE: return "VK_INCOMPLETE";
	case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
	case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
	case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
	case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
	case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
	case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
	case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
	case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
	case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
	case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
	case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
	case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
	case VK_ERROR_UNKNOWN: return "VK_ERROR_UNKNOWN";
	case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
	case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
	case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
	case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
	case VK_ERROR_VALIDATION_FAILED_EXT: return "VK_ERROR_VALIDATION_FAILED_EXT";
	default: return "VK_RESULT_UNRECOGNIZED";
	}
}

const char *PlatformStatusName(PlatformStatus status) noexcept
{
	switch (status)
	{
	case PlatformStatus::Success: return "success";
	case PlatformStatus::AlreadyInitialized: return "already-initialized";
	case PlatformStatus::InvalidArgument: return "invalid-argument";
	case PlatformStatus::UnsupportedPlatform: return "unsupported-platform";
	case PlatformStatus::LoaderUnavailable: return "loader-unavailable";
	case PlatformStatus::LoaderTooOld: return "loader-too-old";
	case PlatformStatus::MissingInstanceExtension: return "missing-instance-extension";
	case PlatformStatus::ValidationLayerUnavailable: return "validation-layer-unavailable";
	case PlatformStatus::InstanceCreationFailed: return "instance-creation-failed";
	case PlatformStatus::DebugMessengerCreationFailed: return "debug-messenger-creation-failed";
	case PlatformStatus::SurfaceCreationFailed: return "surface-creation-failed";
	case PlatformStatus::PhysicalDeviceEnumerationFailed: return "physical-device-enumeration-failed";
	case PlatformStatus::DeviceSelectionFailed: return "device-selection-failed";
	case PlatformStatus::LogicalDeviceCreationFailed: return "logical-device-creation-failed";
	case PlatformStatus::TimelineSemaphoreCreationFailed: return "timeline-semaphore-creation-failed";
	default: return "unknown-platform-status";
	}
}

namespace
{

constexpr const char *kPortabilitySubsetExtensionName =
	"VK_KHR_portability_subset";
std::atomic<void *> gVolkOwner(nullptr);

template <typename Property>
bool ContainsName(const std::vector<Property> &properties, const char *name)
{
	for (size_t i = 0; i < properties.size(); ++i)
		if (strcmp(properties[i].extensionName, name) == 0)
			return true;
	return false;
}

bool ContainsLayer(const std::vector<VkLayerProperties> &properties, const char *name)
{
	for (size_t i = 0; i < properties.size(); ++i)
		if (strcmp(properties[i].layerName, name) == 0)
			return true;
	return false;
}

bool ContainsString(const std::vector<std::string> &names, const char *name)
{
	return std::binary_search(names.begin(), names.end(), std::string(name));
}

VkResult EnumerateInstanceExtensions(std::vector<VkExtensionProperties> *out)
{
	for (uint32_t attempt = 0; attempt < 4; ++attempt)
	{
		uint32_t count = 0;
		VkResult result = vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
		if (result != VK_SUCCESS)
			return result;
		out->resize(count);
		result = vkEnumerateInstanceExtensionProperties(nullptr, &count,
			out->empty() ? nullptr : out->data());
		out->resize(count);
		if (result != VK_INCOMPLETE)
			return result;
	}
	return VK_INCOMPLETE;
}

VkResult EnumerateInstanceLayers(std::vector<VkLayerProperties> *out)
{
	for (uint32_t attempt = 0; attempt < 4; ++attempt)
	{
		uint32_t count = 0;
		VkResult result = vkEnumerateInstanceLayerProperties(&count, nullptr);
		if (result != VK_SUCCESS)
			return result;
		out->resize(count);
		result = vkEnumerateInstanceLayerProperties(&count,
			out->empty() ? nullptr : out->data());
		out->resize(count);
		if (result != VK_INCOMPLETE)
			return result;
	}
	return VK_INCOMPLETE;
}

VkResult EnumeratePhysicalDevices(VkInstance instance, std::vector<VkPhysicalDevice> *out)
{
	for (uint32_t attempt = 0; attempt < 4; ++attempt)
	{
		uint32_t count = 0;
		VkResult result = vkEnumeratePhysicalDevices(instance, &count, nullptr);
		if (result != VK_SUCCESS)
			return result;
		out->resize(count);
		result = vkEnumeratePhysicalDevices(instance, &count,
			out->empty() ? nullptr : out->data());
		out->resize(count);
		if (result != VK_INCOMPLETE)
			return result;
	}
	return VK_INCOMPLETE;
}

VkResult EnumerateDeviceExtensions(VkPhysicalDevice device,
	std::vector<VkExtensionProperties> *out)
{
	for (uint32_t attempt = 0; attempt < 4; ++attempt)
	{
		uint32_t count = 0;
		VkResult result = vkEnumerateDeviceExtensionProperties(device, nullptr,
			&count, nullptr);
		if (result != VK_SUCCESS)
			return result;
		out->resize(count);
		result = vkEnumerateDeviceExtensionProperties(device, nullptr, &count,
			out->empty() ? nullptr : out->data());
		out->resize(count);
		if (result != VK_INCOMPLETE)
			return result;
	}
	return VK_INCOMPLETE;
}

VkResult EnumerateSurfaceFormats(VkPhysicalDevice device, VkSurfaceKHR surface,
	std::vector<VkSurfaceFormatKHR> *out)
{
	for (uint32_t attempt = 0; attempt < 4; ++attempt)
	{
		uint32_t count = 0;
		VkResult result = vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface,
			&count, nullptr);
		if (result != VK_SUCCESS)
			return result;
		out->resize(count);
		result = vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count,
			out->empty() ? nullptr : out->data());
		out->resize(count);
		if (result != VK_INCOMPLETE)
			return result;
	}
	return VK_INCOMPLETE;
}

VkResult EnumeratePresentModes(VkPhysicalDevice device, VkSurfaceKHR surface,
	std::vector<VkPresentModeKHR> *out)
{
	for (uint32_t attempt = 0; attempt < 4; ++attempt)
	{
		uint32_t count = 0;
		VkResult result = vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface,
			&count, nullptr);
		if (result != VK_SUCCESS)
			return result;
		out->resize(count);
		result = vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &count,
			out->empty() ? nullptr : out->data());
		out->resize(count);
		if (result != VK_INCOMPLETE)
			return result;
	}
	return VK_INCOMPLETE;
}

PhysicalDeviceType MapDeviceType(VkPhysicalDeviceType type)
{
	switch (type)
	{
	case VK_PHYSICAL_DEVICE_TYPE_CPU: return PhysicalDeviceType::Cpu;
	case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return PhysicalDeviceType::Virtual;
	case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return PhysicalDeviceType::Integrated;
	case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return PhysicalDeviceType::Discrete;
	default: return PhysicalDeviceType::Other;
	}
}

VkFormat MapFormat(RenderFormat format)
{
	switch (format)
	{
	case RenderFormat::R8G8B8A8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
	case RenderFormat::R16G16Sfloat: return VK_FORMAT_R16G16_SFLOAT;
	case RenderFormat::R8G8Unorm: return VK_FORMAT_R8G8_UNORM;
	case RenderFormat::R8Unorm: return VK_FORMAT_R8_UNORM;
	case RenderFormat::R32Uint: return VK_FORMAT_R32_UINT;
	case RenderFormat::D32Sfloat: return VK_FORMAT_D32_SFLOAT;
	case RenderFormat::R32G32Sfloat: return VK_FORMAT_R32G32_SFLOAT;
	case RenderFormat::R16G16B16A16Sfloat: return VK_FORMAT_R16G16B16A16_SFLOAT;
	default: return VK_FORMAT_UNDEFINED;
	}
}

std::string SanitizeName(const char *name)
{
	std::string result;
	if (!name)
		return result;
	for (size_t i = 0; name[i] != '\0' && i < VK_MAX_PHYSICAL_DEVICE_NAME_SIZE; ++i)
	{
		const unsigned char c = static_cast<unsigned char>(name[i]);
		result.push_back(std::isprint(c) && c != '"' && c != '\\' ?
			static_cast<char>(c) : '_');
	}
	return result;
}

std::string UuidString(const PhysicalDeviceUuid &uuid)
{
	std::ostringstream stream;
	stream << std::hex << std::setfill('0');
	for (uint32_t i = 0; i < kPhysicalDeviceUuidSize; ++i)
		stream << std::setw(2) << static_cast<uint32_t>(uuid.bytes[i]);
	return stream.str();
}

const char *SelectionStatusName(DeviceSelectionStatus status)
{
	switch (status)
	{
	case DeviceSelectionStatus::Success: return "success";
	case DeviceSelectionStatus::InvalidInput: return "invalid-input";
	case DeviceSelectionStatus::InvalidOverride: return "invalid-override";
	case DeviceSelectionStatus::DuplicateUuid: return "duplicate-uuid";
	case DeviceSelectionStatus::DuplicateEnumerationIndex: return "duplicate-enumeration-index";
	case DeviceSelectionStatus::OverrideNotFound: return "override-not-found";
	case DeviceSelectionStatus::OverrideRejectedByRequiredProfile: return "override-rejected";
	case DeviceSelectionStatus::NoRequiredProfileDevice: return "no-required-profile-device";
	default: return "unknown-selection-status";
	}
}

uint64_t FeatureBit(RequiredDeviceFeature feature)
{
	return uint64_t(1) << static_cast<uint32_t>(feature);
}

uint32_t FormatBit(FormatSemantic semantic)
{
	return uint32_t(1) << static_cast<uint32_t>(semantic);
}

VkImageUsageFlags ImageUsageFor(uint32_t required_usage)
{
	VkImageUsageFlags usage = 0;
	if (required_usage & kFormatSampled) usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
	if (required_usage & kFormatColorAttachment) usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	if (required_usage & kFormatDepthAttachment) usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	if (required_usage & kFormatTransferSource) usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	if (required_usage & kFormatTransferDestination) usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	if (required_usage & kFormatStorage) usage |= VK_IMAGE_USAGE_STORAGE_BIT;
	return usage;
}

VkFormatFeatureFlags2 FormatFeaturesFor(uint32_t required_usage)
{
	VkFormatFeatureFlags2 features = 0;
	if (required_usage & kFormatSampled) features |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT;
	if (required_usage & kFormatColorAttachment) features |= VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT;
	if (required_usage & kFormatDepthAttachment) features |= VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT;
	if (required_usage & kFormatBlend) features |= VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BLEND_BIT;
	if (required_usage & kFormatTransferSource) features |= VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT;
	if (required_usage & kFormatTransferDestination) features |= VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT;
	if (required_usage & kFormatStorage) features |= VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT;
	if (required_usage & kFormatLinearFilter) features |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
	return features;
}

struct DeviceProbe
{
	VkPhysicalDevice handle = VK_NULL_HANDLE;
	PhysicalDeviceCandidate candidate = {};
	VkPhysicalDeviceProperties properties = {};
	VkPhysicalDeviceMemoryProperties memory_properties = {};
	VkDeviceSize max_buffer_size = 0;
	uint32_t timestamp_valid_bits = 0;
	std::vector<std::string> extensions;
	std::vector<std::string> rejection_reasons;
	bool memory_budget = false;
	bool present_id = false;
	bool present_wait = false;
	bool swapchain_maintenance1 = false;
	bool portability_subset = false;
};

std::string LimitsDiagnostic(const DeviceProbe &probe)
{
	const VkPhysicalDeviceLimits &l = probe.properties.limits;
	std::ostringstream text;
	text << "device[" << probe.candidate.enumeration_index << "]"
		<< " colorAttachments=" << l.maxColorAttachments
		<< " fragmentOutputs=" << l.maxFragmentOutputAttachments
		<< " fragmentCombined=" << l.maxFragmentCombinedOutputResources
		<< " boundSets=" << l.maxBoundDescriptorSets
		<< " stageResources=" << l.maxPerStageResources
		<< " uniformBuffers=" << l.maxPerStageDescriptorUniformBuffers << "/"
		<< l.maxDescriptorSetUniformBuffers << "/"
		<< l.maxDescriptorSetUniformBuffersDynamic
		<< " storageBuffers=" << l.maxPerStageDescriptorStorageBuffers << "/"
		<< l.maxDescriptorSetStorageBuffers
		<< " samplers=" << l.maxPerStageDescriptorSamplers << "/"
		<< l.maxDescriptorSetSamplers
		<< " sampledImages=" << l.maxPerStageDescriptorSampledImages << "/"
		<< l.maxDescriptorSetSampledImages
		<< " storageRange=" << l.maxStorageBufferRange
		<< " storageAlign=" << l.minStorageBufferOffsetAlignment
		<< " uniformAlign=" << l.minUniformBufferOffsetAlignment
		<< " maxBuffer=" << probe.max_buffer_size
		<< " texelElements=" << l.maxTexelBufferElements
		<< " indirect=" << l.maxDrawIndirectCount
		<< " push=" << l.maxPushConstantsSize
		<< " compute=" << l.maxComputeWorkGroupInvocations << "/"
		<< l.maxComputeWorkGroupSize[0] << "/" << l.maxComputeSharedMemorySize
		<< "/" << l.maxComputeWorkGroupCount[0]
		<< " image2D=" << l.maxImageDimension2D
		<< " arrayLayers=" << l.maxImageArrayLayers
		<< " framebuffer=" << l.maxFramebufferWidth << "x"
		<< l.maxFramebufferHeight << "x" << l.maxFramebufferLayers
		<< " viewport=" << l.maxViewportDimensions[0] << "x"
		<< l.maxViewportDimensions[1] << "@" << l.viewportBoundsRange[0]
		<< ":" << l.viewportBoundsRange[1]
		<< " vertexInput=" << l.maxVertexInputBindings << "/"
		<< l.maxVertexInputAttributes << "/" << l.maxVertexInputBindingStride
		<< " maxIndex=" << l.maxDrawIndexedIndexValue
		<< " atom=" << l.nonCoherentAtomSize
		<< " copyAlign=" << l.optimalBufferCopyOffsetAlignment << "/"
		<< l.optimalBufferCopyRowPitchAlignment
		<< " timestamp=" << l.timestampComputeAndGraphics << "/"
		<< l.timestampPeriod << "/" << probe.timestamp_valid_bits
		<< " samples=0x" << std::hex
		<< probe.candidate.profile.common_sample_count_mask << std::dec
		<< " heaps=" << probe.memory_properties.memoryHeapCount
		<< " localBudget=" << probe.candidate.device_local_budget_bytes;
	return text.str();
}

} // namespace

struct Platform::Impl
{
	VkInstance instance = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VkPhysicalDevice physical_device = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VkQueue graphics_queue = VK_NULL_HANDLE;
	VkQueue present_queue = VK_NULL_HANDLE;
	VkSemaphore timeline = VK_NULL_HANDLE;
	SurfaceBackend surface_backend = SurfaceBackend::LegacyPlatformUnsupported;
	uint32_t graphics_family = kInvalidQueueFamily;
	uint32_t present_family = kInvalidQueueFamily;
	bool loader_initialized = false;
	bool owns_volk = false;
	bool ready = false;
	bool device_lost = false;
	bool validation_enabled = false;
	bool debug_utils_enabled = false;
	bool verbose_validation = false;
	PlatformStatus status = PlatformStatus::Success;
	VkResult last_result = VK_SUCCESS;
	SelectedDeviceInfo selected = {};
	LogCallback log_callback = nullptr;
	void *log_user = nullptr;
	mutable std::mutex diagnostic_mutex;
	uint64_t diagnostic_sequence = 0;
	std::vector<Diagnostic> diagnostics;

	void Add(DiagnosticSeverity severity, const char *stage, const std::string &message)
	{
		LogCallback callback = nullptr;
		void *user = nullptr;
		uint64_t sequence = 0;
		{
			std::lock_guard<std::mutex> lock(diagnostic_mutex);
			sequence = diagnostic_sequence++;
			Diagnostic diagnostic;
			diagnostic.sequence = sequence;
			diagnostic.severity = severity;
			diagnostic.stage = stage ? stage : "platform";
			diagnostic.message = message;
			diagnostics.push_back(std::move(diagnostic));
			callback = log_callback;
			user = log_user;
		}
		if (callback)
		{
			try
			{
				callback(user, severity, stage ? stage : "platform", message.c_str());
			}
			catch (...)
			{
			}
		}
	}

	void ClearDiagnostics()
	{
		std::lock_guard<std::mutex> lock(diagnostic_mutex);
		diagnostics.clear();
		diagnostic_sequence = 0;
	}

	PlatformStatus Fail(PlatformStatus failure, VkResult result,
		const char *stage, const std::string &message)
	{
		status = failure;
		last_result = result;
		std::ostringstream text;
		text << message;
		if (result != VK_SUCCESS)
			text << " (" << ResultName(result) << ", " << static_cast<int32_t>(result) << ")";
		Add(DiagnosticSeverity::Error, stage, text.str());
		return failure;
	}

	static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
		VkDebugUtilsMessageTypeFlagsEXT,
		const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
		void *user_data)
	{
		Impl *self = static_cast<Impl *>(user_data);
		if (!self)
			return VK_FALSE;
		DiagnosticSeverity severity = DiagnosticSeverity::Info;
		if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
			severity = DiagnosticSeverity::Error;
		else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
			severity = DiagnosticSeverity::Warning;
		const char *message = callback_data && callback_data->pMessage ?
			callback_data->pMessage : "validation callback without a message";
		try
		{
			self->Add(severity, "validation", message);
		}
		catch (...)
		{
		}
		return VK_FALSE;
	}

	VkDebugUtilsMessengerCreateInfoEXT DebugCreateInfo() const
	{
		VkDebugUtilsMessengerCreateInfoEXT info = {};
		info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		if (verbose_validation)
			info.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
		info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		info.pfnUserCallback = &Impl::DebugCallback;
		info.pUserData = const_cast<Impl *>(this);
		return info;
	}

	bool BuildSurfaceExtensions(const SurfaceSource &source,
		std::vector<std::string> *extensions, std::string *error)
	{
		extensions->clear();
		switch (source.backend)
		{
		case SurfaceBackend::NativeWin32:
#if defined(_WIN32)
			extensions->push_back(VK_KHR_SURFACE_EXTENSION_NAME);
			extensions->push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
			break;
#else
			*error = "native Win32 surface requested by a non-Windows build";
			return false;
#endif
		case SurfaceBackend::Sdl3:
#if defined(SDL3)
		{
			uint32_t count = 0;
			const char *const *names = SDL_Vulkan_GetInstanceExtensions(&count);
			if (!names || count == 0)
			{
				*error = std::string("SDL_Vulkan_GetInstanceExtensions failed: ") +
					(SDL_GetError() ? SDL_GetError() : "unknown SDL error");
				return false;
			}
			for (uint32_t i = 0; i < count; ++i)
				if (names[i] && names[i][0])
					extensions->push_back(names[i]);
			break;
		}
#else
			*error = "SDL3 surface requested by a build without SDL3 support";
			return false;
#endif
		default:
			*error = "legacy non-SDL platform surface creation is unsupported";
			return false;
		}
		std::sort(extensions->begin(), extensions->end());
		extensions->erase(std::unique(extensions->begin(), extensions->end()),
			extensions->end());
		return true;
	}

	VkResult CreateSurface(const SurfaceSource &source)
	{
		if (!source.window)
			return VK_ERROR_INITIALIZATION_FAILED;
		switch (source.backend)
		{
		case SurfaceBackend::NativeWin32:
#if defined(_WIN32)
		{
			if (!vkCreateWin32SurfaceKHR)
				return VK_ERROR_EXTENSION_NOT_PRESENT;
			VkWin32SurfaceCreateInfoKHR info = {};
			info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
			info.hwnd = static_cast<HWND>(source.window);
			info.hinstance = source.native_instance ?
				static_cast<HINSTANCE>(source.native_instance) : GetModuleHandleW(nullptr);
			const VkResult result = vkCreateWin32SurfaceKHR(instance, &info, nullptr, &surface);
			if (result == VK_SUCCESS)
				surface_backend = source.backend;
			return result;
		}
#else
			return VK_ERROR_EXTENSION_NOT_PRESENT;
#endif
		case SurfaceBackend::Sdl3:
#if defined(SDL3)
			if (SDL_Vulkan_CreateSurface(static_cast<SDL_Window *>(source.window),
				instance, nullptr, &surface))
			{
				surface_backend = source.backend;
				return VK_SUCCESS;
			}
			return VK_ERROR_INITIALIZATION_FAILED;
#else
			return VK_ERROR_EXTENSION_NOT_PRESENT;
#endif
		default:
			return VK_ERROR_EXTENSION_NOT_PRESENT;
		}
	}

	void DestroySurfaceHandle(VkSurfaceKHR handle,
		SurfaceBackend backend) noexcept
	{
		(void)backend;
		if (handle == VK_NULL_HANDLE || instance == VK_NULL_HANDLE)
			return;
#if defined(SDL3)
		if (backend == SurfaceBackend::Sdl3)
			SDL_Vulkan_DestroySurface(instance, handle, nullptr);
		else
#endif
		if (vkDestroySurfaceKHR)
			vkDestroySurfaceKHR(instance, handle, nullptr);
	}

	void ProbeOptionalFeatures(DeviceProbe *probe)
	{
		probe->present_id = ContainsString(probe->extensions,
			VK_KHR_PRESENT_ID_EXTENSION_NAME);
		if (probe->present_id)
		{
			VkPhysicalDevicePresentIdFeaturesKHR feature = {};
			feature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR;
			VkPhysicalDeviceFeatures2 query = {};
			query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
			query.pNext = &feature;
			vkGetPhysicalDeviceFeatures2(probe->handle, &query);
			probe->present_id = feature.presentId == VK_TRUE;
		}

		probe->present_wait = ContainsString(probe->extensions,
			VK_KHR_PRESENT_WAIT_EXTENSION_NAME);
		if (probe->present_wait)
		{
			VkPhysicalDevicePresentWaitFeaturesKHR feature = {};
			feature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR;
			VkPhysicalDeviceFeatures2 query = {};
			query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
			query.pNext = &feature;
			vkGetPhysicalDeviceFeatures2(probe->handle, &query);
			probe->present_wait = feature.presentWait == VK_TRUE;
		}

		probe->swapchain_maintenance1 = ContainsString(probe->extensions,
			VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
		if (probe->swapchain_maintenance1)
		{
			VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT feature = {};
			feature.sType =
				VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;
			VkPhysicalDeviceFeatures2 query = {};
			query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
			query.pNext = &feature;
			vkGetPhysicalDeviceFeatures2(probe->handle, &query);
			probe->swapchain_maintenance1 = feature.swapchainMaintenance1 == VK_TRUE;
		}
	}

	uint32_t ProbeFormats(VkPhysicalDevice physical, uint32_t *common_samples,
		std::vector<std::string> *reasons)
	{
		uint32_t supported_bits = 0;
		uint32_t samples = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT |
			VK_SAMPLE_COUNT_4_BIT | VK_SAMPLE_COUNT_8_BIT;
		for (size_t i = 0; i < kRequiredFormatCount; ++i)
		{
			const FormatRequirement &requirement = kRequiredFormats[i];
			const VkFormat format = MapFormat(requirement.format);
			bool supported = format != VK_FORMAT_UNDEFINED;
			VkFormatProperties3 properties3 = {};
			properties3.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3;
			VkFormatProperties2 properties2 = {};
			properties2.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
			properties2.pNext = &properties3;
			if (supported)
				vkGetPhysicalDeviceFormatProperties2(physical, format, &properties2);
			const VkFormatFeatureFlags2 required_features =
				FormatFeaturesFor(requirement.required_usage);
			supported = supported &&
				(properties3.optimalTilingFeatures & required_features) == required_features;

			VkImageFormatProperties image_properties = {};
			if (supported)
			{
				VkPhysicalDeviceImageFormatInfo2 image_info = {};
				image_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
				image_info.format = format;
				image_info.type = VK_IMAGE_TYPE_2D;
				image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
				image_info.usage = ImageUsageFor(requirement.required_usage);
				image_info.flags = 0;
				VkImageFormatProperties2 image_properties2 = {};
				image_properties2.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
				const VkResult result = vkGetPhysicalDeviceImageFormatProperties2(
					physical, &image_info, &image_properties2);
				supported = result == VK_SUCCESS;
				if (supported)
					image_properties = image_properties2.imageFormatProperties;
			}

			if (supported && (image_properties.sampleCounts & VK_SAMPLE_COUNT_1_BIT) == 0)
				supported = false;
			if (supported && (requirement.required_usage & kFormatMultisample))
				samples &= static_cast<uint32_t>(image_properties.sampleCounts);
			if (supported)
				supported_bits |= FormatBit(requirement.semantic);
			else
			{
				std::ostringstream reason;
				reason << "format[" << static_cast<uint32_t>(requirement.semantic)
					<< "]=" << static_cast<uint32_t>(format);
				reasons->push_back(reason.str());
			}
		}
		*common_samples = samples & (VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT |
			VK_SAMPLE_COUNT_4_BIT | VK_SAMPLE_COUNT_8_BIT);
		return supported_bits;
	}

	bool ProbeSurfaceConfiguration(VkPhysicalDevice physical,
		std::vector<std::string> *reasons)
	{
		VkSurfaceCapabilitiesKHR capabilities = {};
		VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical,
			surface, &capabilities);
		if (result != VK_SUCCESS)
		{
			reasons->push_back(std::string("surface-capabilities:") + ResultName(result));
			return false;
		}
		if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0)
		{
			reasons->push_back("surface-missing-color-attachment-usage");
			return false;
		}

		std::vector<VkSurfaceFormatKHR> formats;
		result = EnumerateSurfaceFormats(physical, surface, &formats);
		if (result != VK_SUCCESS || formats.empty())
		{
			reasons->push_back(std::string("surface-formats:") + ResultName(result));
			return false;
		}
		bool safe_format = formats.size() == 1 &&
			formats[0].format == VK_FORMAT_UNDEFINED &&
			formats[0].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		for (size_t i = 0; i < formats.size(); ++i)
		{
			const bool preferred_unorm = formats[i].format == VK_FORMAT_B8G8R8A8_UNORM ||
				formats[i].format == VK_FORMAT_R8G8B8A8_UNORM;
			if (preferred_unorm && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
				safe_format = true;
		}
		if (!safe_format)
		{
			reasons->push_back("surface-missing-safe-unorm-srgb-nonlinear-pair");
			return false;
		}

		std::vector<VkPresentModeKHR> modes;
		result = EnumeratePresentModes(physical, surface, &modes);
		if (result != VK_SUCCESS || modes.empty())
		{
			reasons->push_back(std::string("present-modes:") + ResultName(result));
			return false;
		}
		bool parity_mode = false;
		for (size_t i = 0; i < modes.size(); ++i)
			if (modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR ||
				modes[i] == VK_PRESENT_MODE_MAILBOX_KHR ||
				modes[i] == VK_PRESENT_MODE_FIFO_KHR)
				parity_mode = true;
		if (!parity_mode)
		{
			reasons->push_back("surface-missing-immediate-mailbox-fifo-present-mode");
			return false;
		}
		return true;
	}

	uint32_t DescriptorTier(const VkPhysicalDeviceLimits &limits) const
	{
		uint32_t capacity = std::min(limits.maxPerStageDescriptorSampledImages,
			limits.maxDescriptorSetSampledImages);
		capacity = capacity > kWorldArrayImageCount ?
			capacity - kWorldArrayImageCount : 0;
		// One UBO, 32 samplers, eight array images, and eight storage buffers
		// accompany the P float-2D image page in every world stage.
		const uint32_t fixed_stage_resources = 1 + kWorldSamplerCount +
			kWorldArrayImageCount + 8;
		const uint32_t resource_capacity = limits.maxPerStageResources >
			fixed_stage_resources ? limits.maxPerStageResources - fixed_stage_resources : 0;
		capacity = std::min(capacity, resource_capacity);
		return SelectDescriptorPageTier(capacity);
	}

	bool ProbeLimits(const DeviceProbe &probe, const RequestedDeviceProfile &requested,
		uint32_t common_samples, uint32_t descriptor_tier,
		std::vector<std::string> *reasons) const
	{
		const VkPhysicalDeviceLimits &limit = probe.properties.limits;
		bool valid = true;
		auto require = [&](bool condition, const char *name) {
			if (!condition)
			{
				valid = false;
				reasons->push_back(std::string("limit:") + name);
			}
		};
		require(limit.maxColorAttachments >= 5, "maxColorAttachments");
		require(limit.maxFragmentOutputAttachments >= 5, "maxFragmentOutputAttachments");
		require(limit.maxFragmentCombinedOutputResources >= 13,
			"maxFragmentCombinedOutputResources");
		require(limit.maxBoundDescriptorSets >= 3, "maxBoundDescriptorSets");
		require(descriptor_tier >= kDescriptorPageTiers[0], "descriptorPageTier32");
		require(limit.maxPerStageDescriptorUniformBuffers >= 1,
			"maxPerStageDescriptorUniformBuffers");
		require(limit.maxDescriptorSetUniformBuffers >= 1,
			"maxDescriptorSetUniformBuffers");
		require(limit.maxDescriptorSetUniformBuffersDynamic >= 1,
			"maxDescriptorSetUniformBuffersDynamic");
		require(limit.maxPerStageDescriptorStorageBuffers >= 8,
			"maxPerStageDescriptorStorageBuffers");
		require(limit.maxDescriptorSetStorageBuffers >= 8,
			"maxDescriptorSetStorageBuffers");
		require(limit.maxPerStageDescriptorSamplers >= kWorldSamplerCount,
			"maxPerStageDescriptorSamplers");
		require(limit.maxDescriptorSetSamplers >= kWorldSamplerCount,
			"maxDescriptorSetSamplers");
		require(limit.maxPerStageDescriptorSampledImages >=
			kDescriptorPageTiers[0] + kWorldArrayImageCount,
			"maxPerStageDescriptorSampledImages");
		require(limit.maxDescriptorSetSampledImages >=
			kDescriptorPageTiers[0] + kWorldArrayImageCount,
			"maxDescriptorSetSampledImages");
		require(requested.storage_buffer_range == 0 ||
			limit.maxStorageBufferRange >= requested.storage_buffer_range,
			"maxStorageBufferRange");
		require(requested.buffer_size == 0 ||
			probe.max_buffer_size >= requested.buffer_size, "maxBufferSize");
		require(requested.texel_buffer_elements == 0 ||
			limit.maxTexelBufferElements >= requested.texel_buffer_elements,
			"maxTexelBufferElements");
		require(limit.maxDrawIndirectCount >= std::max(1u, requested.draw_indirect_count),
			"maxDrawIndirectCount");
		require(limit.maxPushConstantsSize >= 32, "maxPushConstantsSize");
		require(limit.maxComputeWorkGroupInvocations >= 256,
			"maxComputeWorkGroupInvocations");
		require(limit.maxComputeWorkGroupSize[0] >= 256,
			"maxComputeWorkGroupSizeX");
		require(limit.maxComputeSharedMemorySize >= 16 * 1024,
			"maxComputeSharedMemorySize");
		require(limit.maxComputeWorkGroupCount[0] >= requested.compute_work_group_count_x,
			"maxComputeWorkGroupCountX");
		require(limit.maxImageDimension2D >= requested.image_width &&
			limit.maxImageDimension2D >= requested.image_height, "maxImageDimension2D");
		require(limit.maxImageArrayLayers >= requested.image_array_layers,
			"maxImageArrayLayers");
		require(limit.maxFramebufferWidth >= requested.framebuffer_width,
			"maxFramebufferWidth");
		require(limit.maxFramebufferHeight >= requested.framebuffer_height,
			"maxFramebufferHeight");
		require(limit.maxFramebufferLayers >= requested.framebuffer_layers,
			"maxFramebufferLayers");
		require(limit.maxViewportDimensions[0] >= requested.viewport_width &&
			limit.maxViewportDimensions[1] >= requested.viewport_height,
			"maxViewportDimensions");
		require(limit.viewportBoundsRange[0] <= 0.0f &&
			limit.viewportBoundsRange[1] >= static_cast<float>(requested.viewport_height),
			"viewportBoundsRange");
		require(limit.maxVertexInputBindings >= requested.vertex_input_bindings,
			"maxVertexInputBindings");
		require(limit.maxVertexInputAttributes >= requested.vertex_input_attributes,
			"maxVertexInputAttributes");
		require(limit.maxVertexInputBindingStride >= requested.vertex_input_binding_stride,
			"maxVertexInputBindingStride");
		require((common_samples & VK_SAMPLE_COUNT_1_BIT) != 0,
			"framebufferSampleCounts");
		if (requested.require_gpu_timestamps)
		{
			require(limit.timestampComputeAndGraphics == VK_TRUE,
				"timestampComputeAndGraphics");
			require(probe.timestamp_valid_bits != 0, "timestampValidBits");
			require(limit.timestampPeriod > 0.0f, "timestampPeriod");
		}
		return valid;
	}

	bool RequestedSignatureFits(const DeviceProbe &probe,
		const RequestedDeviceProfile &requested, uint32_t common_samples) const
	{
		const VkPhysicalDeviceLimits &limit = probe.properties.limits;
		const bool sample_request_valid = requested.requested_msaa_samples == 0 ||
			requested.requested_msaa_samples == 1 || requested.requested_msaa_samples == 2 ||
			requested.requested_msaa_samples == 4 || requested.requested_msaa_samples == 8;
		return sample_request_valid &&
			(common_samples & VK_SAMPLE_COUNT_1_BIT) != 0 &&
			limit.maxImageDimension2D >= requested.image_width &&
			limit.maxImageDimension2D >= requested.image_height &&
			limit.maxImageArrayLayers >= requested.image_array_layers &&
			limit.maxFramebufferWidth >= requested.framebuffer_width &&
			limit.maxFramebufferHeight >= requested.framebuffer_height &&
			limit.maxFramebufferLayers >= requested.framebuffer_layers &&
			limit.maxViewportDimensions[0] >= requested.viewport_width &&
			limit.maxViewportDimensions[1] >= requested.viewport_height &&
			limit.maxComputeWorkGroupCount[0] >= requested.compute_work_group_count_x &&
			limit.maxDrawIndirectCount >= requested.draw_indirect_count &&
			(requested.storage_buffer_range == 0 ||
			 limit.maxStorageBufferRange >= requested.storage_buffer_range) &&
			(requested.buffer_size == 0 || probe.max_buffer_size >= requested.buffer_size) &&
			(requested.texel_buffer_elements == 0 ||
			 limit.maxTexelBufferElements >= requested.texel_buffer_elements);
	}

	DeviceProbe ProbeDevice(VkPhysicalDevice physical, uint32_t enumeration_index,
		const RequestedDeviceProfile &requested)
	{
		DeviceProbe probe;
		probe.handle = physical;
		probe.candidate.enumeration_index = enumeration_index;

		VkPhysicalDeviceIDProperties id = {};
		id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
		VkPhysicalDeviceProperties2 properties2 = {};
		properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		properties2.pNext = &id;
		vkGetPhysicalDeviceProperties2(physical, &properties2);
		probe.properties = properties2.properties;
		memcpy(probe.candidate.uuid.bytes, id.deviceUUID,
			kPhysicalDeviceUuidSize);
		probe.candidate.type = MapDeviceType(probe.properties.deviceType);
		probe.candidate.profile.api_major = VK_API_VERSION_MAJOR(probe.properties.apiVersion);
		probe.candidate.profile.api_minor = VK_API_VERSION_MINOR(probe.properties.apiVersion);
		probe.candidate.profile.api_patch = VK_API_VERSION_PATCH(probe.properties.apiVersion);

		std::vector<VkExtensionProperties> extension_properties;
		VkResult extension_result = EnumerateDeviceExtensions(physical,
			&extension_properties);
		if (extension_result != VK_SUCCESS)
			probe.rejection_reasons.push_back(std::string("device-extensions:") +
				ResultName(extension_result));
		for (size_t i = 0; i < extension_properties.size(); ++i)
			probe.extensions.push_back(extension_properties[i].extensionName);
		std::sort(probe.extensions.begin(), probe.extensions.end());
		probe.extensions.erase(std::unique(probe.extensions.begin(),
			probe.extensions.end()), probe.extensions.end());

		probe.memory_budget = ContainsString(probe.extensions,
			VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
		probe.portability_subset = ContainsString(probe.extensions,
			kPortabilitySubsetExtensionName);
		ProbeOptionalFeatures(&probe);

		if (probe.properties.apiVersion >= VK_API_VERSION_1_3 ||
			ContainsString(probe.extensions, VK_KHR_MAINTENANCE_4_EXTENSION_NAME))
		{
			VkPhysicalDeviceMaintenance4Properties maintenance4 = {};
			maintenance4.sType =
				VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_PROPERTIES;
			VkPhysicalDeviceProperties2 query = {};
			query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
			query.pNext = &maintenance4;
			vkGetPhysicalDeviceProperties2(physical, &query);
			probe.max_buffer_size = maintenance4.maxBufferSize;
		}

		VkPhysicalDeviceMemoryBudgetPropertiesEXT budget = {};
		budget.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
		VkPhysicalDeviceMemoryProperties2 memory2 = {};
		memory2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
		memory2.pNext = probe.memory_budget ? &budget : nullptr;
		vkGetPhysicalDeviceMemoryProperties2(physical, &memory2);
		probe.memory_properties = memory2.memoryProperties;
		uint64_t local_budget = 0;
		for (uint32_t heap = 0; heap < memory2.memoryProperties.memoryHeapCount; ++heap)
		{
			if ((memory2.memoryProperties.memoryHeaps[heap].flags &
				VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == 0)
				continue;
			VkDeviceSize bytes = memory2.memoryProperties.memoryHeaps[heap].size;
			if (probe.memory_budget && budget.heapBudget[heap] != 0)
				bytes = std::min(bytes, budget.heapBudget[heap]);
			if (bytes > std::numeric_limits<uint64_t>::max() - local_budget)
				local_budget = std::numeric_limits<uint64_t>::max();
			else
				local_budget += bytes;
		}
		probe.candidate.device_local_budget_bytes = local_budget;

		VkPhysicalDeviceVulkan11Features features11 = {};
		features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
		VkPhysicalDeviceVulkan12Features features12 = {};
		features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
		VkPhysicalDeviceVulkan13Features features13 = {};
		features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
		VkPhysicalDeviceFeatures2 features2 = {};
		features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		features2.pNext = &features11;
		features11.pNext = &features12;
		features12.pNext = &features13;
		vkGetPhysicalDeviceFeatures2(physical, &features2);

		uint64_t feature_bits = 0;
		if (features13.dynamicRendering) feature_bits |=
			FeatureBit(RequiredDeviceFeature::DynamicRendering);
		if (features13.synchronization2) feature_bits |=
			FeatureBit(RequiredDeviceFeature::Synchronization2);
		if (features12.timelineSemaphore) feature_bits |=
			FeatureBit(RequiredDeviceFeature::TimelineSemaphore);
		if (features2.features.independentBlend) feature_bits |=
			FeatureBit(RequiredDeviceFeature::IndependentBlend);
		if (features2.features.multiDrawIndirect) feature_bits |=
			FeatureBit(RequiredDeviceFeature::MultiDrawIndirect);
		if (features11.shaderDrawParameters) feature_bits |=
			FeatureBit(RequiredDeviceFeature::ShaderDrawParameters);
		if (features12.shaderSampledImageArrayNonUniformIndexing) feature_bits |=
			FeatureBit(RequiredDeviceFeature::NonUniformSampledImageIndexing);
		if (probe.properties.apiVersion >= VK_API_VERSION_1_3) feature_bits |=
			FeatureBit(RequiredDeviceFeature::ExtendedDynamicRasterDepthState);
		if (ContainsString(probe.extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
			feature_bits |= FeatureBit(RequiredDeviceFeature::Swapchain);
		if (surface != VK_NULL_HANDLE)
			feature_bits |= FeatureBit(RequiredDeviceFeature::PlatformSurface);

		uint32_t queue_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(physical, &queue_count, nullptr);
		std::vector<VkQueueFamilyProperties> queues(queue_count);
		vkGetPhysicalDeviceQueueFamilyProperties(physical, &queue_count,
			queues.empty() ? nullptr : queues.data());
		queues.resize(queue_count);
		uint32_t first_graphics_compute = kInvalidQueueFamily;
		uint32_t first_present = kInvalidQueueFamily;
		uint32_t unified = kInvalidQueueFamily;
		for (uint32_t family = 0; family < queue_count; ++family)
		{
			if (queues[family].queueCount == 0)
				continue;
			const bool graphics_compute =
				(queues[family].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
				(VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);
			VkBool32 present = VK_FALSE;
			const VkResult present_result = vkGetPhysicalDeviceSurfaceSupportKHR(
				physical, family, surface, &present);
			if (present_result != VK_SUCCESS)
				present = VK_FALSE;
			if (graphics_compute && first_graphics_compute == kInvalidQueueFamily)
				first_graphics_compute = family;
			if (present && first_present == kInvalidQueueFamily)
				first_present = family;
			if (graphics_compute && present && unified == kInvalidQueueFamily)
				unified = family;
		}
		if (unified != kInvalidQueueFamily)
		{
			probe.candidate.profile.graphics_compute_queue_family = unified;
			probe.candidate.profile.present_queue_family = unified;
		}
		else
		{
			probe.candidate.profile.graphics_compute_queue_family = first_graphics_compute;
			probe.candidate.profile.present_queue_family = first_present;
		}
		if (first_graphics_compute != kInvalidQueueFamily)
		{
			feature_bits |= FeatureBit(RequiredDeviceFeature::GraphicsQueue) |
				FeatureBit(RequiredDeviceFeature::ComputeOnGraphicsQueue);
			probe.timestamp_valid_bits =
				queues[probe.candidate.profile.graphics_compute_queue_family].timestampValidBits;
		}
		if (first_present != kInvalidQueueFamily)
			feature_bits |= FeatureBit(RequiredDeviceFeature::Presentation);
		probe.candidate.profile.required_feature_bits = feature_bits;

		uint32_t common_samples = 0;
		probe.candidate.profile.required_format_bits = ProbeFormats(physical,
			&common_samples, &probe.rejection_reasons);
		common_samples &= static_cast<uint32_t>(probe.properties.limits.framebufferColorSampleCounts);
		common_samples &= static_cast<uint32_t>(probe.properties.limits.framebufferDepthSampleCounts);
		probe.candidate.profile.common_sample_count_mask = common_samples;

		const uint32_t descriptor_tier = DescriptorTier(probe.properties.limits);
		probe.candidate.profile.descriptor_page_tier = descriptor_tier;
		probe.candidate.profile.all_required_limits_satisfied = ProbeLimits(probe,
			requested, common_samples, descriptor_tier, &probe.rejection_reasons) ? 1u : 0u;
		probe.candidate.profile.requested_signature_supported = RequestedSignatureFits(
			probe, requested, common_samples) ? 1u : 0u;
		probe.candidate.profile.surface_configuration_satisfied =
			ProbeSurfaceConfiguration(physical, &probe.rejection_reasons) ? 1u : 0u;

		if (feature_bits != kAllRequiredDeviceFeatureBits)
		{
			for (size_t i = 0; i < kRequiredDeviceFeatureCount; ++i)
				if ((feature_bits & FeatureBit(kRequiredDeviceFeatures[i].feature)) == 0)
					probe.rejection_reasons.push_back(std::string("feature:") +
						kRequiredDeviceFeatures[i].diagnostic_name);
		}
		if (probe.candidate.profile.required_format_bits != kAllRequiredFormatBits)
			probe.rejection_reasons.push_back("required-format-matrix");
		if (!probe.candidate.profile.requested_signature_supported)
			probe.rejection_reasons.push_back("requested-signature");
		if (PhysicalDeviceUuidIsZero(probe.candidate.uuid))
			probe.rejection_reasons.push_back("zero-device-uuid");
		return probe;
	}

	VkResult CreateLogicalDevice(const DeviceProbe &probe)
	{
		const float priority = 1.0f;
		std::vector<VkDeviceQueueCreateInfo> queue_infos;
		VkDeviceQueueCreateInfo graphics_info = {};
		graphics_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		graphics_info.queueFamilyIndex =
			probe.candidate.profile.graphics_compute_queue_family;
		graphics_info.queueCount = 1;
		graphics_info.pQueuePriorities = &priority;
		queue_infos.push_back(graphics_info);
		if (probe.candidate.profile.present_queue_family !=
			probe.candidate.profile.graphics_compute_queue_family)
		{
			VkDeviceQueueCreateInfo present_info = graphics_info;
			present_info.queueFamilyIndex = probe.candidate.profile.present_queue_family;
			queue_infos.push_back(present_info);
		}

		std::vector<const char *> extension_names;
		extension_names.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
		if (probe.memory_budget)
			extension_names.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
		if (probe.portability_subset)
			extension_names.push_back(kPortabilitySubsetExtensionName);
		const bool enable_present_wait = probe.present_id && probe.present_wait;
		if (enable_present_wait)
		{
			extension_names.push_back(VK_KHR_PRESENT_ID_EXTENSION_NAME);
			extension_names.push_back(VK_KHR_PRESENT_WAIT_EXTENSION_NAME);
		}
		if (probe.swapchain_maintenance1)
			extension_names.push_back(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
		std::sort(extension_names.begin(), extension_names.end(),
			[](const char *left, const char *right) { return strcmp(left, right) < 0; });

		VkPhysicalDeviceFeatures2 enabled = {};
		enabled.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		enabled.features.independentBlend = VK_TRUE;
		enabled.features.multiDrawIndirect = VK_TRUE;
		VkPhysicalDeviceVulkan11Features enabled11 = {};
		enabled11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
		enabled11.shaderDrawParameters = VK_TRUE;
		VkPhysicalDeviceVulkan12Features enabled12 = {};
		enabled12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
		enabled12.timelineSemaphore = VK_TRUE;
		enabled12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
		VkPhysicalDeviceVulkan13Features enabled13 = {};
		enabled13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
		enabled13.dynamicRendering = VK_TRUE;
		enabled13.synchronization2 = VK_TRUE;
		enabled.pNext = &enabled11;
		enabled11.pNext = &enabled12;
		enabled12.pNext = &enabled13;

		VkPhysicalDevicePresentIdFeaturesKHR present_id = {};
		present_id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR;
		present_id.presentId = enable_present_wait ? VK_TRUE : VK_FALSE;
		VkPhysicalDevicePresentWaitFeaturesKHR present_wait = {};
		present_wait.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR;
		present_wait.presentWait = enable_present_wait ? VK_TRUE : VK_FALSE;
		VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT maintenance1 = {};
		maintenance1.sType =
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;
		maintenance1.swapchainMaintenance1 = probe.swapchain_maintenance1 ? VK_TRUE : VK_FALSE;
		void **tail = &enabled13.pNext;
		if (enable_present_wait)
		{
			*tail = &present_id;
			tail = &present_id.pNext;
			*tail = &present_wait;
			tail = &present_wait.pNext;
		}
		if (probe.swapchain_maintenance1)
			*tail = &maintenance1;

		VkDeviceCreateInfo create_info = {};
		create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		create_info.pNext = &enabled;
		create_info.queueCreateInfoCount = static_cast<uint32_t>(queue_infos.size());
		create_info.pQueueCreateInfos = queue_infos.data();
		create_info.enabledExtensionCount = static_cast<uint32_t>(extension_names.size());
		create_info.ppEnabledExtensionNames = extension_names.data();
		const VkResult result = vkCreateDevice(probe.handle, &create_info, nullptr, &device);
		if (result != VK_SUCCESS)
			return result;

		volkLoadDevice(device);
		graphics_family = probe.candidate.profile.graphics_compute_queue_family;
		present_family = probe.candidate.profile.present_queue_family;
		vkGetDeviceQueue(device, graphics_family, 0, &graphics_queue);
		vkGetDeviceQueue(device, present_family, 0, &present_queue);
		if (!graphics_queue || !present_queue)
			return VK_ERROR_INITIALIZATION_FAILED;

		physical_device = probe.handle;
		selected.contract_candidate = probe.candidate;
		selected.properties = probe.properties;
		selected.memory_properties = probe.memory_properties;
		selected.graphics_compute_queue_family = graphics_family;
		selected.present_queue_family = present_family;
		selected.timestamp_valid_bits = probe.timestamp_valid_bits;
		selected.memory_budget_enabled = probe.memory_budget ? 1u : 0u;
		selected.present_id_enabled = enable_present_wait ? 1u : 0u;
		selected.present_wait_enabled = enable_present_wait ? 1u : 0u;
		selected.swapchain_maintenance1_enabled =
			probe.swapchain_maintenance1 ? 1u : 0u;
		selected.portability_subset_enabled = probe.portability_subset ? 1u : 0u;
		return VK_SUCCESS;
	}

	VkResult CreateTimelineSemaphore()
	{
		VkSemaphoreTypeCreateInfo type = {};
		type.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
		type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
		type.initialValue = 0;
		VkSemaphoreCreateInfo create_info = {};
		create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		create_info.pNext = &type;
		return vkCreateSemaphore(device, &create_info, nullptr, &timeline);
	}

	void DestroyObjects(bool known_device_lost) noexcept
	{
		device_lost = device_lost || known_device_lost;
		if (device != VK_NULL_HANDLE)
		{
			if (!device_lost && vkDeviceWaitIdle)
			{
				const VkResult wait_result = vkDeviceWaitIdle(device);
				if (wait_result == VK_ERROR_DEVICE_LOST)
					device_lost = true;
			}
			if (timeline != VK_NULL_HANDLE && vkDestroySemaphore)
				vkDestroySemaphore(device, timeline, nullptr);
			timeline = VK_NULL_HANDLE;
			if (vkDestroyDevice)
				vkDestroyDevice(device, nullptr);
		}
		device = VK_NULL_HANDLE;
		graphics_queue = VK_NULL_HANDLE;
		present_queue = VK_NULL_HANDLE;
		graphics_family = kInvalidQueueFamily;
		present_family = kInvalidQueueFamily;
		physical_device = VK_NULL_HANDLE;
		DestroySurfaceHandle(surface, surface_backend);
		surface = VK_NULL_HANDLE;
		surface_backend = SurfaceBackend::LegacyPlatformUnsupported;
		if (debug_messenger != VK_NULL_HANDLE && instance != VK_NULL_HANDLE &&
			vkDestroyDebugUtilsMessengerEXT)
			vkDestroyDebugUtilsMessengerEXT(instance, debug_messenger, nullptr);
		debug_messenger = VK_NULL_HANDLE;
		if (instance != VK_NULL_HANDLE && vkDestroyInstance)
			vkDestroyInstance(instance, nullptr);
		instance = VK_NULL_HANDLE;
		if (loader_initialized)
			volkFinalize();
		loader_initialized = false;
		if (owns_volk)
		{
			void *expected = this;
			gVolkOwner.compare_exchange_strong(expected, nullptr);
			owns_volk = false;
		}
		ready = false;
		validation_enabled = false;
		debug_utils_enabled = false;
	}
};

Platform::Platform() : impl_(new Impl)
{
}

Platform::~Platform()
{
	if (impl_)
	{
		impl_->DestroyObjects(false);
		delete impl_;
		impl_ = nullptr;
	}
}

PlatformStatus Platform::Initialize(const PlatformCreateInfo &create_info)
{
	if (impl_->ready || impl_->instance != VK_NULL_HANDLE || impl_->device != VK_NULL_HANDLE)
		return PlatformStatus::AlreadyInitialized;
	impl_->ClearDiagnostics();
	impl_->selected = {};
	impl_->device_lost = false;
	impl_->last_result = VK_SUCCESS;
	impl_->status = PlatformStatus::Success;
	impl_->log_callback = create_info.log_callback;
	impl_->log_user = create_info.log_user;
	impl_->verbose_validation = create_info.verbose_validation != 0;

	const RequestedDeviceProfile &requested = create_info.requested_profile;
	const bool invalid_boolean = create_info.enable_validation > 1 ||
		create_info.require_validation > 1 || create_info.verbose_validation > 1 ||
		requested.require_gpu_timestamps > 1;
	const bool invalid_dimensions = requested.image_width == 0 ||
		requested.image_height == 0 || requested.image_array_layers == 0 ||
		requested.framebuffer_width == 0 || requested.framebuffer_height == 0 ||
		requested.framebuffer_layers == 0 || requested.viewport_width == 0 ||
		requested.viewport_height == 0 || requested.compute_work_group_count_x == 0 ||
		requested.vertex_input_bindings == 0 || requested.vertex_input_attributes == 0 ||
		requested.vertex_input_binding_stride == 0 || requested.draw_indirect_count == 0;
	const bool invalid_samples = requested.requested_msaa_samples != 0 &&
		requested.requested_msaa_samples != 1 && requested.requested_msaa_samples != 2 &&
		requested.requested_msaa_samples != 4 && requested.requested_msaa_samples != 8;
	if (!create_info.application_name || !create_info.application_name[0] ||
		!create_info.surface.window || invalid_boolean || invalid_dimensions || invalid_samples)
		return impl_->Fail(PlatformStatus::InvalidArgument, VK_SUCCESS, "arguments",
			"invalid Vulkan platform creation arguments");
	if (create_info.surface.backend == SurfaceBackend::LegacyPlatformUnsupported)
		return impl_->Fail(PlatformStatus::UnsupportedPlatform,
			VK_ERROR_EXTENSION_NOT_PRESENT, "surface",
			"legacy non-SDL platform surface creation is unsupported");

	void *expected_volk_owner = nullptr;
	if (!gVolkOwner.compare_exchange_strong(expected_volk_owner, impl_))
		return impl_->Fail(PlatformStatus::AlreadyInitialized,
			VK_ERROR_INITIALIZATION_FAILED, "loader",
			"another Vulkan Platform owns Volk's global dispatch table");
	impl_->owns_volk = true;
	VkResult result = volkInitialize();
	if (result != VK_SUCCESS)
	{
		const PlatformStatus failure = impl_->Fail(PlatformStatus::LoaderUnavailable,
			result, "loader", "unable to load the Vulkan loader through Volk");
		impl_->DestroyObjects(false);
		return failure;
	}
	impl_->loader_initialized = true;
	const uint32_t loader_version = volkGetInstanceVersion();
	if (loader_version < kApiVersion)
	{
		std::ostringstream message;
		message << "Vulkan 1.3 is required; loader exposes "
			<< VK_API_VERSION_MAJOR(loader_version) << "."
			<< VK_API_VERSION_MINOR(loader_version) << "."
			<< VK_API_VERSION_PATCH(loader_version);
		const PlatformStatus failure = impl_->Fail(PlatformStatus::LoaderTooOld,
			VK_ERROR_INCOMPATIBLE_DRIVER, "loader", message.str());
		impl_->DestroyObjects(false);
		return failure;
	}

	std::vector<std::string> requested_extensions;
	std::string surface_extension_error;
	if (!impl_->BuildSurfaceExtensions(create_info.surface, &requested_extensions,
		&surface_extension_error))
	{
		const PlatformStatus failure = impl_->Fail(PlatformStatus::UnsupportedPlatform,
			VK_ERROR_EXTENSION_NOT_PRESENT, "surface", surface_extension_error);
		impl_->DestroyObjects(false);
		return failure;
	}

	std::vector<VkExtensionProperties> available_extensions;
	result = EnumerateInstanceExtensions(&available_extensions);
	if (result != VK_SUCCESS)
	{
		const PlatformStatus failure = impl_->Fail(
			PlatformStatus::MissingInstanceExtension, result, "instance",
			"unable to enumerate Vulkan instance extensions");
		impl_->DestroyObjects(false);
		return failure;
	}
	for (size_t i = 0; i < requested_extensions.size(); ++i)
	{
		if (!ContainsName(available_extensions, requested_extensions[i].c_str()))
		{
			const PlatformStatus failure = impl_->Fail(
				PlatformStatus::MissingInstanceExtension, VK_ERROR_EXTENSION_NOT_PRESENT,
				"instance", std::string("missing required instance extension ") +
					requested_extensions[i]);
			impl_->DestroyObjects(false);
			return failure;
		}
	}

	bool portability_enumeration = false;
	if (ContainsName(available_extensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
	{
		requested_extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
		portability_enumeration = true;
	}
	const bool validation_requested = create_info.enable_validation != 0 ||
		create_info.require_validation != 0;
	const bool debug_utils_available = ContainsName(available_extensions,
		VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	if (debug_utils_available)
		requested_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	std::sort(requested_extensions.begin(), requested_extensions.end());
	requested_extensions.erase(std::unique(requested_extensions.begin(),
		requested_extensions.end()), requested_extensions.end());

	std::vector<VkLayerProperties> layers;
	result = EnumerateInstanceLayers(&layers);
	if (result != VK_SUCCESS)
	{
		const PlatformStatus failure = impl_->Fail(
			PlatformStatus::ValidationLayerUnavailable, result, "instance",
			"unable to enumerate Vulkan instance layers");
		impl_->DestroyObjects(false);
		return failure;
	}
	const bool validation_layer_available = ContainsLayer(layers,
		"VK_LAYER_KHRONOS_validation");
	if (validation_requested && !validation_layer_available)
	{
		if (create_info.require_validation)
		{
			const PlatformStatus failure = impl_->Fail(
				PlatformStatus::ValidationLayerUnavailable, VK_ERROR_LAYER_NOT_PRESENT,
				"instance", "required VK_LAYER_KHRONOS_validation is unavailable");
			impl_->DestroyObjects(false);
			return failure;
		}
		impl_->Add(DiagnosticSeverity::Warning, "instance",
			"VK_LAYER_KHRONOS_validation is unavailable; continuing without validation");
	}
	impl_->validation_enabled = validation_requested && validation_layer_available;
	if (impl_->validation_enabled && !debug_utils_available)
		impl_->Add(DiagnosticSeverity::Warning, "instance",
			"VK_EXT_debug_utils is unavailable; validation output cannot be captured");

	std::vector<const char *> instance_extension_names;
	for (size_t i = 0; i < requested_extensions.size(); ++i)
		instance_extension_names.push_back(requested_extensions[i].c_str());
	const char *validation_layer = "VK_LAYER_KHRONOS_validation";
	VkApplicationInfo application = {};
	application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application.pApplicationName = create_info.application_name;
	application.applicationVersion = create_info.application_version;
	application.pEngineName = "PiccuEngine Vulkan";
	application.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
	application.apiVersion = kApiVersion;
	VkDebugUtilsMessengerCreateInfoEXT debug_create = impl_->DebugCreateInfo();
	VkInstanceCreateInfo instance_create = {};
	instance_create.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance_create.pNext = impl_->validation_enabled && debug_utils_available ?
		&debug_create : nullptr;
	instance_create.flags = portability_enumeration ?
		VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR : 0;
	instance_create.pApplicationInfo = &application;
	instance_create.enabledLayerCount = impl_->validation_enabled ? 1u : 0u;
	instance_create.ppEnabledLayerNames = impl_->validation_enabled ?
		&validation_layer : nullptr;
	instance_create.enabledExtensionCount =
		static_cast<uint32_t>(instance_extension_names.size());
	instance_create.ppEnabledExtensionNames = instance_extension_names.data();
	result = vkCreateInstance(&instance_create, nullptr, &impl_->instance);
	if (result != VK_SUCCESS)
	{
		const PlatformStatus failure = impl_->Fail(PlatformStatus::InstanceCreationFailed,
			result, "instance", "vkCreateInstance failed");
		impl_->DestroyObjects(false);
		return failure;
	}
	volkLoadInstanceOnly(impl_->instance);
	impl_->debug_utils_enabled = debug_utils_available;

	if (impl_->validation_enabled && debug_utils_available)
	{
		if (!vkCreateDebugUtilsMessengerEXT)
			result = VK_ERROR_EXTENSION_NOT_PRESENT;
		else
			result = vkCreateDebugUtilsMessengerEXT(impl_->instance, &debug_create,
				nullptr, &impl_->debug_messenger);
		if (result != VK_SUCCESS)
		{
			if (create_info.require_validation)
			{
				const PlatformStatus failure = impl_->Fail(
					PlatformStatus::DebugMessengerCreationFailed, result, "validation",
					"unable to create the Vulkan debug messenger");
				impl_->DestroyObjects(false);
				return failure;
			}
			impl_->Add(DiagnosticSeverity::Warning, "validation",
				"unable to create the Vulkan debug messenger; continuing");
		}
	}

	result = impl_->CreateSurface(create_info.surface);
	if (result != VK_SUCCESS)
	{
		std::string message = "unable to create the Vulkan presentation surface";
#if defined(SDL3)
		if (create_info.surface.backend == SurfaceBackend::Sdl3 && SDL_GetError())
			message += std::string(": ") + SDL_GetError();
#endif
		const PlatformStatus failure = impl_->Fail(PlatformStatus::SurfaceCreationFailed,
			result, "surface", message);
		impl_->DestroyObjects(false);
		return failure;
	}

	std::vector<VkPhysicalDevice> physical_devices;
	result = EnumeratePhysicalDevices(impl_->instance, &physical_devices);
	if (result != VK_SUCCESS || physical_devices.empty())
	{
		const PlatformStatus failure = impl_->Fail(
			PlatformStatus::PhysicalDeviceEnumerationFailed,
			result == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : result,
			"device-probe", physical_devices.empty() ?
				"Vulkan reports no physical devices" :
				"unable to enumerate Vulkan physical devices");
		impl_->DestroyObjects(false);
		return failure;
	}

	std::vector<DeviceProbe> probes;
	std::vector<PhysicalDeviceCandidate> candidates;
	probes.reserve(physical_devices.size());
	candidates.reserve(physical_devices.size());
	for (uint32_t i = 0; i < physical_devices.size(); ++i)
	{
		probes.push_back(impl_->ProbeDevice(physical_devices[i], i, requested));
		candidates.push_back(probes.back().candidate);
		std::ostringstream line;
		line << "device[" << i << "] uuid=" << UuidString(probes.back().candidate.uuid)
			<< " name=\"" << SanitizeName(probes.back().properties.deviceName) << "\""
			<< " api=" << probes.back().candidate.profile.api_major << "."
			<< probes.back().candidate.profile.api_minor << "."
			<< probes.back().candidate.profile.api_patch
			<< " type=" << static_cast<uint32_t>(probes.back().candidate.type)
			<< " budget=" << probes.back().candidate.device_local_budget_bytes
			<< " queues=" << probes.back().candidate.profile.graphics_compute_queue_family
			<< "/" << probes.back().candidate.profile.present_queue_family
			<< " tier=" << probes.back().candidate.profile.descriptor_page_tier
			<< " samples=0x" << std::hex
			<< probes.back().candidate.profile.common_sample_count_mask << std::dec
			<< " profile=" << (SupportsRequiredDeviceProfile(probes.back().candidate.profile) ?
				"accept" : "reject");
		if (!probes.back().rejection_reasons.empty())
		{
			line << " reasons=";
			for (size_t reason = 0; reason < probes.back().rejection_reasons.size(); ++reason)
			{
				if (reason) line << ',';
				line << probes.back().rejection_reasons[reason];
			}
		}
		impl_->Add(DiagnosticSeverity::Info, "device-probe", line.str());
		impl_->Add(DiagnosticSeverity::Info, "device-limits",
			LimitsDiagnostic(probes.back()));
	}

	const PhysicalDeviceSelection selection = SelectPhysicalDevice(candidates.data(),
		candidates.size(), create_info.device_override);
	if (selection.status != DeviceSelectionStatus::Success ||
		selection.candidate_position >= probes.size())
	{
		const PlatformStatus failure = impl_->Fail(PlatformStatus::DeviceSelectionFailed,
			VK_ERROR_FEATURE_NOT_PRESENT, "device-selection",
			std::string("device selection failed: ") + SelectionStatusName(selection.status));
		impl_->DestroyObjects(false);
		return failure;
	}
	const DeviceProbe &selected_probe = probes[selection.candidate_position];
	result = impl_->CreateLogicalDevice(selected_probe);
	if (result != VK_SUCCESS)
	{
		const PlatformStatus failure = impl_->Fail(
			PlatformStatus::LogicalDeviceCreationFailed, result, "device",
			"vkCreateDevice or queue retrieval failed");
		impl_->DestroyObjects(result == VK_ERROR_DEVICE_LOST);
		return failure;
	}
	result = impl_->CreateTimelineSemaphore();
	if (result != VK_SUCCESS)
	{
		const PlatformStatus failure = impl_->Fail(
			PlatformStatus::TimelineSemaphoreCreationFailed, result, "timeline",
			"unable to create the renderer timeline semaphore");
		impl_->DestroyObjects(result == VK_ERROR_DEVICE_LOST);
		return failure;
	}

	impl_->ready = true;
	impl_->status = PlatformStatus::Success;
	impl_->last_result = VK_SUCCESS;
	std::ostringstream selected_message;
	selected_message << "selected device[" << selection.enumeration_index << "] uuid="
		<< UuidString(selection.uuid) << " graphics=" << impl_->graphics_family
		<< " present=" << impl_->present_family << " api=1.3";
	impl_->Add(DiagnosticSeverity::Info, "device-selection", selected_message.str());
	return PlatformStatus::Success;
}

bool Platform::RecreateSurface(const SurfaceSource &source)
{
	if (!impl_ || !impl_->ready || impl_->device_lost || !source.window ||
		impl_->instance == VK_NULL_HANDLE || impl_->physical_device == VK_NULL_HANDLE)
		return false;
	const VkSurfaceKHR old_surface = impl_->surface;
	const SurfaceBackend old_backend = impl_->surface_backend;
	impl_->surface = VK_NULL_HANDLE;
	impl_->surface_backend = SurfaceBackend::LegacyPlatformUnsupported;
	VkResult result = impl_->CreateSurface(source);
	if (result != VK_SUCCESS)
	{
		impl_->surface = old_surface;
		impl_->surface_backend = old_backend;
		impl_->Fail(PlatformStatus::SurfaceCreationFailed, result,
			"surface-recreate", "native surface recreation failed");
		return false;
	}
	VkBool32 present_supported = VK_FALSE;
	result = vkGetPhysicalDeviceSurfaceSupportKHR(impl_->physical_device,
		impl_->present_family, impl_->surface, &present_supported);
	std::vector<std::string> reasons;
	const bool configuration_ok = result == VK_SUCCESS && present_supported &&
		impl_->ProbeSurfaceConfiguration(impl_->physical_device, &reasons);
	if (!configuration_ok)
	{
		impl_->DestroySurfaceHandle(impl_->surface, impl_->surface_backend);
		impl_->surface = old_surface;
		impl_->surface_backend = old_backend;
		std::ostringstream message;
		message << "replacement surface is incompatible with selected device";
		for (const std::string &reason : reasons)
			message << " " << reason;
		impl_->Fail(PlatformStatus::SurfaceCreationFailed,
			result == VK_SUCCESS ? VK_ERROR_FORMAT_NOT_SUPPORTED : result,
			"surface-recreate", message.str());
		return false;
	}
	impl_->DestroySurfaceHandle(old_surface, old_backend);
	impl_->status = PlatformStatus::Success;
	impl_->last_result = VK_SUCCESS;
	impl_->Add(DiagnosticSeverity::Info, "surface-recreate",
		"native presentation surface recreated without rebuilding the device");
	return true;
}

void Platform::Shutdown(bool device_lost) noexcept
{
	if (impl_)
		impl_->DestroyObjects(device_lost);
}

void Platform::NotifyDeviceResult(VkResult result) noexcept
{
	if (!impl_)
		return;
	impl_->last_result = result;
	if (result == VK_ERROR_DEVICE_LOST && !impl_->device_lost)
	{
		impl_->device_lost = true;
		impl_->ready = false;
		try
		{
			impl_->Add(DiagnosticSeverity::Error, "device",
				"VK_ERROR_DEVICE_LOST observed; teardown will skip device-idle waits");
		}
		catch (...)
		{
		}
	}
}

bool Platform::Ready() const noexcept { return impl_ && impl_->ready; }
bool Platform::DeviceLost() const noexcept { return impl_ && impl_->device_lost; }
PlatformStatus Platform::Status() const noexcept
{
	return impl_ ? impl_->status : PlatformStatus::InvalidArgument;
}
VkResult Platform::LastVulkanResult() const noexcept
{
	return impl_ ? impl_->last_result : VK_ERROR_INITIALIZATION_FAILED;
}
VkInstance Platform::Instance() const noexcept
{
	return impl_ ? impl_->instance : VK_NULL_HANDLE;
}
VkSurfaceKHR Platform::Surface() const noexcept
{
	return impl_ ? impl_->surface : VK_NULL_HANDLE;
}
VkPhysicalDevice Platform::PhysicalDevice() const noexcept
{
	return impl_ ? impl_->physical_device : VK_NULL_HANDLE;
}
VkDevice Platform::Device() const noexcept
{
	return impl_ ? impl_->device : VK_NULL_HANDLE;
}
uint32_t Platform::EnabledApiVersion() const noexcept { return kApiVersion; }
VkQueue Platform::GraphicsQueue() const noexcept
{
	return impl_ ? impl_->graphics_queue : VK_NULL_HANDLE;
}
VkQueue Platform::PresentQueue() const noexcept
{
	return impl_ ? impl_->present_queue : VK_NULL_HANDLE;
}
VkSemaphore Platform::TimelineSemaphore() const noexcept
{
	return impl_ ? impl_->timeline : VK_NULL_HANDLE;
}
uint32_t Platform::GraphicsQueueFamily() const noexcept
{
	return impl_ ? impl_->graphics_family : kInvalidQueueFamily;
}
uint32_t Platform::PresentQueueFamily() const noexcept
{
	return impl_ ? impl_->present_family : kInvalidQueueFamily;
}
bool Platform::UsesSeparatePresentQueue() const noexcept
{
	return impl_ && impl_->graphics_family != kInvalidQueueFamily &&
		impl_->present_family != kInvalidQueueFamily &&
		impl_->graphics_family != impl_->present_family;
}
bool Platform::DebugUtilsEnabled() const noexcept
{
	return impl_ && impl_->debug_utils_enabled;
}
const SelectedDeviceInfo &Platform::SelectedDevice() const noexcept
{
	return impl_->selected;
}

std::vector<Diagnostic> Platform::CopyDiagnostics() const
{
	if (!impl_)
		return {};
	std::lock_guard<std::mutex> lock(impl_->diagnostic_mutex);
	return impl_->diagnostics;
}

std::string Platform::DiagnosticsText() const
{
	const std::vector<Diagnostic> snapshot = CopyDiagnostics();
	std::ostringstream stream;
	for (size_t i = 0; i < snapshot.size(); ++i)
	{
		const char *severity = snapshot[i].severity == DiagnosticSeverity::Error ?
			"error" : (snapshot[i].severity == DiagnosticSeverity::Warning ? "warning" : "info");
		stream << snapshot[i].sequence << " [" << severity << "] "
			<< snapshot[i].stage << ": " << snapshot[i].message;
		if (i + 1 != snapshot.size()) stream << '\n';
	}
	return stream.str();
}

} // namespace vk
} // namespace render
} // namespace piccu
