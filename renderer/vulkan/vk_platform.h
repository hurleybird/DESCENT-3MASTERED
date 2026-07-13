/* Vulkan 1.3 loader, instance, surface, physical-device, and device bootstrap. */
#pragma once

#include "vk_common.h"
#include "../core/render_device_contract.h"

#include <string>
#include <vector>

namespace piccu
{
namespace render
{
namespace vk
{

enum class SurfaceBackend : uint32_t
{
	NativeWin32 = 0,
	Sdl3,
	LegacyPlatformUnsupported,
};

// The native pointers are never interpreted as renderer or GL objects.
// NativeWin32: window=HWND, native_instance=HINSTANCE (null uses the module
// handle). SDL3: window=SDL_Window*, native_instance is ignored.
struct SurfaceSource
{
	SurfaceBackend backend = SurfaceBackend::LegacyPlatformUnsupported;
	void *window = nullptr;
	void *native_instance = nullptr;
};

// Values derived from the immutable target/resource signature.  A zero byte/
// element/count requirement means that no stronger constraint than the frozen
// baseline is known yet; dimensions and layer counts must remain nonzero.
struct RequestedDeviceProfile
{
	uint32_t image_width = 1;
	uint32_t image_height = 1;
	uint32_t image_array_layers = 1;
	uint32_t framebuffer_width = 1;
	uint32_t framebuffer_height = 1;
	uint32_t framebuffer_layers = 1;
	uint32_t viewport_width = 1;
	uint32_t viewport_height = 1;
	uint32_t compute_work_group_count_x = 1;
	uint32_t vertex_input_bindings = 1;
	uint32_t vertex_input_attributes = 4;
	uint32_t vertex_input_binding_stride = sizeof(BaseVertex);
	uint32_t draw_indirect_count = 1;
	uint32_t texel_buffer_elements = 0;
	uint32_t requested_msaa_samples = 1;
	uint64_t storage_buffer_range = 0;
	uint64_t buffer_size = 0;
	uint32_t require_gpu_timestamps = 0;
};

enum class DiagnosticSeverity : uint32_t
{
	Info = 0,
	Warning,
	Error,
};

struct Diagnostic
{
	uint64_t sequence = 0;
	DiagnosticSeverity severity = DiagnosticSeverity::Info;
	std::string stage;
	std::string message;
};

using LogCallback = void (*)(void *user, DiagnosticSeverity severity,
	const char *stage, const char *message);

enum class PlatformStatus : uint32_t
{
	Success = 0,
	AlreadyInitialized,
	InvalidArgument,
	UnsupportedPlatform,
	LoaderUnavailable,
	LoaderTooOld,
	MissingInstanceExtension,
	ValidationLayerUnavailable,
	InstanceCreationFailed,
	DebugMessengerCreationFailed,
	SurfaceCreationFailed,
	PhysicalDeviceEnumerationFailed,
	DeviceSelectionFailed,
	LogicalDeviceCreationFailed,
	TimelineSemaphoreCreationFailed,
};

const char *PlatformStatusName(PlatformStatus status) noexcept;

struct PlatformCreateInfo
{
	const char *application_name = "PiccuEngine";
	uint32_t application_version = 0;
	SurfaceSource surface;
	RequestedDeviceProfile requested_profile;
	DeviceSelectionOverride device_override = {};
	uint32_t enable_validation = 0;
	uint32_t require_validation = 0;
	uint32_t verbose_validation = 0;
	LogCallback log_callback = nullptr;
	void *log_user = nullptr;
};

struct SelectedDeviceInfo
{
	PhysicalDeviceCandidate contract_candidate = {};
	VkPhysicalDeviceProperties properties = {};
	VkPhysicalDeviceMemoryProperties memory_properties = {};
	uint32_t graphics_compute_queue_family = kInvalidQueueFamily;
	uint32_t present_queue_family = kInvalidQueueFamily;
	uint32_t timestamp_valid_bits = 0;
	uint32_t memory_budget_enabled = 0;
	uint32_t present_id_enabled = 0;
	uint32_t present_wait_enabled = 0;
	uint32_t swapchain_maintenance1_enabled = 0;
	uint32_t portability_subset_enabled = 0;
};

class Platform final
{
public:
	Platform();
	~Platform();

	Platform(const Platform &) = delete;
	Platform &operator=(const Platform &) = delete;
	Platform(Platform &&) = delete;
	Platform &operator=(Platform &&) = delete;

	// A failed Initialize fully tears down partial Vulkan state and may be
	// retried (for example after changing an override). Resource/WSI owners must
	// be destroyed before Shutdown because Volk uses one global dispatch table.
	PlatformStatus Initialize(const PlatformCreateInfo &create_info);
	// Rebuilds only the native presentation surface. The caller must first
	// destroy all swapchains and wait their submissions; the logical device and
	// every offscreen renderer resource remain alive.
	bool RecreateSurface(const SurfaceSource &source);
	void Shutdown(bool device_lost = false) noexcept;
	// Feed every queue/device result through this method. DEVICE_LOST makes
	// Ready() false immediately and guarantees teardown performs no idle wait.
	void NotifyDeviceResult(VkResult result) noexcept;

	bool Ready() const noexcept;
	bool DeviceLost() const noexcept;
	PlatformStatus Status() const noexcept;
	VkResult LastVulkanResult() const noexcept;

	VkInstance Instance() const noexcept;
	VkSurfaceKHR Surface() const noexcept;
	VkPhysicalDevice PhysicalDevice() const noexcept;
	VkDevice Device() const noexcept;
	uint32_t EnabledApiVersion() const noexcept;
	VkQueue GraphicsQueue() const noexcept;
	VkQueue PresentQueue() const noexcept;
	VkSemaphore TimelineSemaphore() const noexcept;
	uint32_t GraphicsQueueFamily() const noexcept;
	uint32_t PresentQueueFamily() const noexcept;
	bool UsesSeparatePresentQueue() const noexcept;
	bool DebugUtilsEnabled() const noexcept;
	const SelectedDeviceInfo &SelectedDevice() const noexcept;

	std::vector<Diagnostic> CopyDiagnostics() const;
	std::string DiagnosticsText() const;

private:
	struct Impl;
	Impl *impl_;
};

} // namespace vk
} // namespace render
} // namespace piccu
