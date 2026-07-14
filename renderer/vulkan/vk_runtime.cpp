/* Concrete Vulkan runtime orchestration behind the API-neutral facade. */
#include "vk_runtime.h"

#include "vk_compiler.h"
#include "vk_frame.h"
#include "vk_pipelines.h"
#include "vk_platform.h"
#include "vk_resources.h"
#include "vk_retained_world.h"
#include "vk_state_tracker.h"
#include "vk_targets.h"
#include "vk_textures.h"
#include "vk_wsi.h"
#include "../core/render_coordinate_contract.h"

#include "application.h"
#include "bitmap.h"
#include "pserror.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#if defined(SDL3)
#include <SDL3/SDL_video.h>
#elif defined(WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace piccu
{
namespace render
{
namespace vk
{

namespace
{

constexpr uint32_t kInitialPresentedFrameSerial = 1;
constexpr VkDeviceSize kRequestedStorageRange = 8u * 1024u * 1024u;
constexpr VkDeviceSize kRequestedMaximumBuffer = 8u * 1024u * 1024u;

void AppendAutomationDiagnostic(const std::string &message) noexcept
{
	const char *path = std::getenv("PICCU_VULKAN_LOG");
	if (!path || !path[0])
		return;
	FILE *file = std::fopen(path, "ab");
	if (!file)
		return;
	std::fwrite(message.data(), 1, message.size(), file);
	std::fwrite("\n", 1, 1, file);
	std::fclose(file);
}

const char *FailureName(RuntimeFailure failure) noexcept
{
	switch (failure)
	{
	case RuntimeFailure::None: return "none";
	case RuntimeFailure::NotInitialized: return "not-initialized";
	case RuntimeFailure::MissingRuntime: return "missing-runtime";
	case RuntimeFailure::InvalidArgument: return "invalid-argument";
	case RuntimeFailure::InvalidLifecycle: return "invalid-lifecycle";
	case RuntimeFailure::CaptureUnavailable: return "capture-unavailable";
	case RuntimeFailure::CaptureRejected: return "capture-rejected";
	case RuntimeFailure::TargetDescriptionFailed: return "target-description-failed";
	case RuntimeFailure::PresentationDescriptionFailed: return "presentation-description-failed";
	case RuntimeFailure::TextureResolutionFailed: return "texture-resolution-failed";
	case RuntimeFailure::TextureEventFailed: return "texture-event-failed";
	case RuntimeFailure::ReadbackFailed: return "readback-failed";
	case RuntimeFailure::PresentationFailed: return "presentation-failed";
	case RuntimeFailure::PipelineUnavailable: return "pipeline-unavailable";
	case RuntimeFailure::UnsupportedFeature: return "unsupported-feature";
	case RuntimeFailure::ResourceExhausted: return "resource-exhausted";
	default: return "unknown-runtime-failure";
	}
}

bool EnvironmentFlag(const char *name) noexcept
{
	if (!name)
		return false;
#if defined(_WIN32)
	char *owned_value = nullptr;
	size_t value_size = 0;
	if (_dupenv_s(&owned_value, &value_size, name) != 0)
		return false;
	const char *value = owned_value;
#else
	const char *value = std::getenv(name);
#endif
	if (!value || !value[0])
	{
#if defined(_WIN32)
		std::free(owned_value);
#endif
		return false;
	}
	const bool enabled = std::strcmp(value, "0") != 0 &&
		std::strcmp(value, "false") != 0 &&
		std::strcmp(value, "FALSE") != 0 &&
		std::strcmp(value, "off") != 0 &&
		std::strcmp(value, "OFF") != 0;
#if defined(_WIN32)
	std::free(owned_value);
#endif
	return enabled;
}

uint32_t AppliedMsaa(const CapturedPreferredState &preferred,
	uint32_t supported_mask) noexcept
{
	const uint32_t requested = NormalizeRequestedMsaa(preferred.msaa_samples,
		preferred.antialised != 0);
	const uint32_t selected = SelectSupportedMsaa(requested, supported_mask);
	return selected ? selected : 1;
}

VkSampleCountFlagBits SampleBits(uint32_t samples) noexcept
{
	switch (samples)
	{
	case 8: return VK_SAMPLE_COUNT_8_BIT;
	case 4: return VK_SAMPLE_COUNT_4_BIT;
	case 2: return VK_SAMPLE_COUNT_2_BIT;
	default: return VK_SAMPLE_COUNT_1_BIT;
	}
}

bool FullTargetSignatureChanged(const CapturedPreferredState &left,
	const CapturedPreferredState &right) noexcept
{
	return left.width != right.width || left.height != right.height ||
		left.antialised != right.antialised ||
		left.supersampling_factor != right.supersampling_factor ||
		left.msaa_samples != right.msaa_samples ||
		left.gtao_enabled != right.gtao_enabled ||
		NormalizeOverscanPercent(left) != NormalizeOverscanPercent(right) ||
		WantsMotionResources(left) != WantsMotionResources(right);
}

RequestedDeviceProfile BuildRequestedProfile(
	const CapturedPreferredState &preferred) noexcept
{
	RequestedDeviceProfile profile;
	const uint32_t overscan = NormalizeOverscanPercent(preferred);
	const uint64_t scale = preferred.supersampling_factor == 2 ||
		preferred.supersampling_factor == 4 ? preferred.supersampling_factor : 1;
	const uint64_t internal_width =
		(uint64_t(preferred.width) * overscan + 99u) / 100u * scale;
	const uint64_t internal_height =
		(uint64_t(preferred.height) * overscan + 99u) / 100u * scale;
	profile.image_width = static_cast<uint32_t>(std::min<uint64_t>(
		internal_width, std::numeric_limits<uint32_t>::max()));
	profile.image_height = static_cast<uint32_t>(std::min<uint64_t>(
		internal_height, std::numeric_limits<uint32_t>::max()));
	profile.image_array_layers = 256;
	profile.framebuffer_width = profile.image_width;
	profile.framebuffer_height = profile.image_height;
	profile.framebuffer_layers = 1;
	profile.viewport_width = profile.image_width;
	profile.viewport_height = profile.image_height;
	profile.compute_work_group_count_x = 1;
	profile.vertex_input_bindings = 1;
	profile.vertex_input_attributes = 4;
	profile.vertex_input_binding_stride = sizeof(BaseVertex);
	profile.draw_indirect_count = 1;
	profile.texel_buffer_elements = 0;
	profile.requested_msaa_samples = NormalizeRequestedMsaa(
		preferred.msaa_samples, preferred.antialised != 0);
	profile.storage_buffer_range = kRequestedStorageRange;
	profile.buffer_size = kRequestedMaximumBuffer;
	profile.require_gpu_timestamps = 0;
	return profile;
}

CaptureReserve DefaultCaptureReserve() noexcept
{
	CaptureReserve reserve = {};
	reserve.commands = 4096;
	reserve.states = 1024;
	reserve.materials = 1024;
	reserve.texture_versions = 512;
	reserve.transforms = 1024;
	reserve.views = 64;
	reserve.viewports = 64;
	reserve.target_layouts = 32;
	reserve.target_signatures = 16;
	reserve.target_versions = 256;
	reserve.present_rects = 16;
	reserve.wsi_signatures = 16;
	reserve.payload_bindings = 1024;
	reserve.payload_records = 2048;
	reserve.payload_bytes = 4u * 1024u * 1024u;
	reserve.stream_vertices = 64u * 1024u;
	reserve.stream_indices = 128u * 1024u;
	reserve.stream_payload_words = 256u * 1024u;
	return reserve;
}

uint32_t TargetIndex(RenderTargetClass target) noexcept
{
	switch (target)
	{
	case RenderTargetClass::Scene: return 0;
	case RenderTargetClass::PostPresent: return 1;
	case RenderTargetClass::CockpitScene: return 2;
	default: return 0;
	}
}

const CapturedTargetLayout *LayoutFor(const TargetManager &targets,
	RenderTargetClass target) noexcept
{
	switch (target)
	{
	case RenderTargetClass::Scene: return &targets.SceneLayout();
	case RenderTargetClass::PostPresent: return &targets.PostLayout();
	case RenderTargetClass::CockpitScene: return &targets.CockpitLayout();
	default: return nullptr;
	}
}

const CapturedTargetVersion *VersionAt(const RenderCaptureSegment &capture,
	TargetVersionId id) noexcept
{
	return id != kInvalidId && id < capture.TargetVersions().size() ?
		&capture.TargetVersions()[id] : nullptr;
}

const CapturedTargetLayout *LayoutAt(const RenderCaptureSegment &capture,
	TargetLayoutId id) noexcept
{
	return id != kInvalidId && id < capture.TargetLayouts().size() ?
		&capture.TargetLayouts()[id] : nullptr;
}

TargetVersionId LatestVersionFor(const RenderCaptureSegment &capture,
	RenderTargetClass target) noexcept
{
	TargetVersionId latest = kInvalidId;
	uint32_t latest_version = 0;
	uint32_t latest_color = 0;
	uint32_t latest_depth = 0;
	for (size_t i = 0; i < capture.TargetVersions().size(); ++i)
	{
		const CapturedTargetVersion &candidate = capture.TargetVersions()[i];
		if (candidate.target != target)
			continue;
		if (latest == kInvalidId || candidate.version > latest_version ||
			(candidate.version == latest_version &&
				(candidate.color_epoch > latest_color ||
				 candidate.depth_epoch > latest_depth)))
		{
			latest = static_cast<TargetVersionId>(i);
			latest_version = candidate.version;
			latest_color = candidate.color_epoch;
			latest_depth = candidate.depth_epoch;
		}
	}
	return latest;
}

uint64_t NextCommandSerial(const RenderCaptureSegment &capture) noexcept
{
	return capture.Commands().empty() ? 0 :
		capture.Commands().back().serial == UINT64_MAX ? 0 :
		capture.Commands().back().serial + 1;
}

bool BuildContinuationState(const RenderCaptureSegment &capture,
	uint64_t submitted_timeline, uint64_t snapshot_serial,
	CaptureContinuationState *output) noexcept
{
	if (!output || submitted_timeline == 0 || snapshot_serial == 0)
		return false;
	CaptureContinuationState state = capture.StartKind() ==
		CaptureSegmentStartKind::ContinuationAfterReadback ?
		capture.ContinuationState() : CaptureContinuationState();
	state.schema_version = kCaptureContinuationSchemaVersion;
	for (const CaptureCommand &command : capture.Commands())
	{
		switch (command.type)
		{
		case CaptureCommandType::BeginFrameTarget:
			state.active_target = command.payload.begin_frame_target.target;
			state.logical_clip = command.payload.begin_frame_target.logical_clip;
			state.active_target_version =
				command.payload.begin_frame_target.active_target_version;
			break;
		case CaptureCommandType::BeginPostPresent:
			state.post_present_begun = 1;
			break;
		case CaptureCommandType::BeginCockpitScene:
			state.cockpit_open = 1;
			state.cockpit_capture_serial =
				command.payload.begin_cockpit_scene.capture_serial;
			break;
		case CaptureCommandType::EndCockpitScene:
			state.cockpit_open = 0;
			state.cockpit_capture_serial =
				command.payload.end_cockpit_scene.capture_serial;
			break;
		case CaptureCommandType::EndFrame:
			state.have_last_view_interval_serial = 1;
			state.last_view_interval_serial =
				command.payload.end_frame.view_interval_serial;
			break;
		case CaptureCommandType::EnqueueFontGlyph:
			state.have_last_font_enqueue_serial = 1;
			state.last_font_enqueue_serial =
				command.payload.enqueue_font_glyph.enqueue_serial;
			break;
		default:
			break;
		}
	}

	const TargetVersionId latest = LatestVersionFor(capture,
		state.active_target);
	if (latest != kInvalidId)
		state.active_target_version = latest;
	const CapturedTargetVersion *version = VersionAt(capture,
		state.active_target_version);
	if (!version)
		return false;
	const CapturedTargetLayout *layout = LayoutAt(capture,
		version->target_layout);
	if (!layout || layout->target != state.active_target)
		return false;
	state.active_attachment_mask = layout->attachment_mask;
	state.load_attachment_mask = layout->attachment_mask;
	state.color_epoch = version->color_epoch;
	state.depth_epoch = version->depth_epoch;
	state.prior_submitted_timeline = submitted_timeline;
	state.resource_state_snapshot_serial = snapshot_serial;
	*output = state;
	return true;
}

bool MatrixProject(const float matrix[16], const float world[3],
	float *x, float *y, float *w) noexcept
{
	if (!matrix || !world || !x || !y || !w)
		return false;
	*x = matrix[0] * world[0] + matrix[4] * world[1] +
		matrix[8] * world[2] + matrix[12];
	*y = matrix[1] * world[0] + matrix[5] * world[1] +
		matrix[9] * world[2] + matrix[13];
	*w = matrix[3] * world[0] + matrix[7] * world[1] +
		matrix[11] * world[2] + matrix[15];
	return std::isfinite(*x) && std::isfinite(*y) && std::isfinite(*w) &&
		*w > 0.00001f;
}

const char *const kNamedPipelines[] = {
	"lightmapped_specular",
	"lightmap_room_fog",
	"lightmap_room",
	"lightmap_room_specular_fog",
	"unlit_room",
	"unlit_room_fog",
	"fog_portal",
	"terrain_legacy_compute",
	"terrain_legacy_compute_fog",
	"shader-test",
};

} // namespace

struct VulkanRuntime::Impl
{
	struct PendingReadback
	{
		ReadbackCompletion completion = {};
		std::string png_path;
	};

	Platform platform;
	ResourceStateTracker state_tracker;
	ResourceAllocator allocator;
	Wsi wsi;
	TargetManager targets;
	FrameScheduler frames;
	TextureManager textures;
	RetainedWorld retained_world;
	std::unique_ptr<PipelineLibrary> pipelines;
	FrameCompiler compiler;
	RenderCaptureSegment captures[2];
	uint32_t active_capture = 0;
	CapturedPreferredState preferred = {};
	oeApplication *application = nullptr;
	void *native_window = nullptr;
	uint32_t drawable_width = 0;
	uint32_t drawable_height = 0;
	uint32_t selected_pipeline = 0;
	uint32_t target_color_epoch[3] = {};
	uint32_t target_depth_epoch[3] = {};
	uint64_t synchronous_readback_waits = 0;
	uint64_t presented_submissions = 0;
	std::vector<PendingReadback> pending_readbacks;
	std::string diagnostic;
	mutable std::mutex diagnostic_mutex;
	bool initialized = false;
	bool fatal = false;

	Impl() : wsi(platform), pipelines(new PipelineLibrary)
	{
		allocator.SetStateTracker(&state_tracker);
		wsi.SetStateTracker(&state_tracker);
	}

	RenderCaptureSegment &Capture() noexcept
	{
		return captures[active_capture];
	}

	const RenderCaptureSegment &Capture() const noexcept
	{
		return captures[active_capture];
	}

	void SetDiagnostic(const std::string &message)
	{
		std::lock_guard<std::mutex> lock(diagnostic_mutex);
		diagnostic = message;
	}

	std::string DiagnosticCopy() const
	{
		std::lock_guard<std::mutex> lock(diagnostic_mutex);
		return diagnostic;
	}

	static void Log(void *user, DiagnosticSeverity severity,
		const char *stage, const char *message)
	{
		Impl *self = static_cast<Impl *>(user);
		if (!self)
			return;
		std::ostringstream stream;
		stream << "Vulkan " << (severity == DiagnosticSeverity::Error ?
			"error" : severity == DiagnosticSeverity::Warning ? "warning" :
			"info") << " [" << (stage ? stage : "runtime") << "]: "
			<< (message ? message : "no diagnostic text");
		const std::string diagnostic = stream.str();
		self->SetDiagnostic(diagnostic);
		AppendAutomationDiagnostic(diagnostic);
	}

	bool Fail(const char *stage, const std::string &message, bool terminal = false)
	{
		std::ostringstream stream;
		stream << (stage ? stage : "runtime") << ": " << message;
		const std::string diagnostic = stream.str();
		SetDiagnostic(diagnostic);
		AppendAutomationDiagnostic(std::string("Vulkan failure: ") + diagnostic);
		mprintf((0, "Vulkan failure [%s]: %s\n",
			stage ? stage : "runtime", message.c_str()));
		fatal = fatal || terminal;
		return false;
	}

	bool QueryWindow(SurfaceSource *surface, uint32_t *width,
		uint32_t *height)
	{
		if (!application || !surface || !width || !height)
			return false;
		*surface = SurfaceSource();
		*width = *height = 0;
#if defined(SDL3)
		SDLApplication *sdl_application =
			static_cast<SDLApplication *>(application);
		SDL_Window *window = sdl_application->GetWindow();
		if (!window || !sdl_application->UsesVulkanWindow())
			return Fail("surface", "SDL window is not Vulkan-capable");
		int drawable_w = 0;
		int drawable_h = 0;
		if (!SDL_GetWindowSizeInPixels(window, &drawable_w, &drawable_h))
			return Fail("surface", std::string(
				"SDL_GetWindowSizeInPixels failed: ") + SDL_GetError());
		surface->backend = SurfaceBackend::Sdl3;
		surface->window = window;
		surface->native_instance = nullptr;
		native_window = window;
		*width = drawable_w > 0 ? static_cast<uint32_t>(drawable_w) : 0;
		*height = drawable_h > 0 ? static_cast<uint32_t>(drawable_h) : 0;
#elif defined(WIN32)
		tWin32AppInfo information = {};
		application->get_info(&information);
		HWND window = reinterpret_cast<HWND>(
			static_cast<uintptr_t>(information.hwnd));
		HINSTANCE instance = reinterpret_cast<HINSTANCE>(
			static_cast<uintptr_t>(information.hinst));
		if (!window)
			return Fail("surface", "Win32 application returned a null HWND");
		RECT client = {};
		if (!GetClientRect(window, &client))
			return Fail("surface", "GetClientRect failed for Vulkan window");
		surface->backend = SurfaceBackend::NativeWin32;
		surface->window = window;
		surface->native_instance = instance;
		native_window = window;
		*width = client.right > client.left ?
			static_cast<uint32_t>(client.right - client.left) : 0;
		*height = client.bottom > client.top ?
			static_cast<uint32_t>(client.bottom - client.top) : 0;
#else
		return Fail("surface", "no supported Vulkan WSI adapter was compiled");
#endif
		return true;
	}

	bool QueryDrawable(uint32_t *width, uint32_t *height)
	{
		SurfaceSource ignored;
		return QueryWindow(&ignored, width, height);
	}

	bool ConfigureApplicationWindow(const CapturedPreferredState &state)
	{
		if (!application || state.window_width == 0 || state.window_height == 0)
			return false;
		int flags = application->flags();
		flags &= ~(OEAPP_WINDOWED | OEAPP_FULLSCREEN);
		flags |= state.fullscreen ? OEAPP_FULLSCREEN : OEAPP_WINDOWED;
		application->set_flags(flags);
		if (!state.fullscreen)
		{
			int outer_width = static_cast<int>(state.window_width);
			int outer_height = static_cast<int>(state.window_height);
#if defined(WIN32)
			// oeApplication sizes the native outer window. Vulkan's preferred
			// dimensions describe the drawable client area, just like GL4's.
			tWin32AppInfo information = {};
			application->get_info(&information);
			HWND window = reinterpret_cast<HWND>(
				static_cast<uintptr_t>(information.hwnd));
			if (!window)
				return false;
			RECT client = { 0, 0, outer_width, outer_height };
			const DWORD style = static_cast<DWORD>(
				GetWindowLongPtr(window, GWL_STYLE));
			const DWORD extended_style = static_cast<DWORD>(
				GetWindowLongPtr(window, GWL_EXSTYLE));
			if (!AdjustWindowRectEx(&client, style, FALSE, extended_style))
				return false;
			outer_width = client.right - client.left;
			outer_height = client.bottom - client.top;
#endif
			application->set_sizepos(OEAPP_COORD_CENTERED, OEAPP_COORD_CENTERED,
				outer_width, outer_height);
		}
		return true;
	}

	PipelineLibraryCreateInfo PipelineInfo(
		const CapturedPreferredState &state) const
	{
		PipelineLibraryCreateInfo info;
		info.platform = const_cast<Platform *>(&platform);
		info.targets = const_cast<TargetManager *>(&targets);
		info.frames = const_cast<FrameScheduler *>(&frames);
		info.target_samples = SampleBits(AppliedMsaa(state,
			platform.SelectedDevice().contract_candidate.profile.
				common_sample_count_mask));
		for (uint32_t i = 0; i < 5; ++i)
			info.world_formats.color[i] = TargetManager::VulkanFormat(
				TargetManager::ContractFormat(i));
		info.world_formats.depth = TargetManager::VulkanFormat(
			RenderFormat::D32Sfloat);
		return info;
	}

	bool InitializeCompiler()
	{
		FrameCompilerCreateInfo info;
		info.platform = &platform;
		info.allocator = &allocator;
		info.frames = &frames;
		info.state_tracker = &state_tracker;
		info.targets = &targets;
		info.textures = &textures;
		info.retained_world = &retained_world;
		info.pipelines = pipelines.get();
		info.wsi = &wsi;
		if (!compiler.Initialize(info))
			return Fail("compiler", "capture compiler initialization failed");
		return true;
	}

	void ShutdownModules(bool device_lost) noexcept
	{
		compiler.Shutdown(device_lost);
		frames.Shutdown(device_lost);
		if (pipelines)
			pipelines->Shutdown(device_lost);
		retained_world.Shutdown(device_lost);
		textures.Shutdown(device_lost);
		targets.Shutdown(device_lost);
		wsi.Shutdown(device_lost);
		allocator.Shutdown(device_lost);
		state_tracker.Reset();
		platform.Shutdown(device_lost);
		pending_readbacks.clear();
		initialized = false;
	}

	bool ResetFreshCapture(uint32_t presented_frame_serial)
	{
		const uint32_t replacement = active_capture ^ 1u;
		RenderCaptureSegment &next = captures[replacement];
		if (next.GetLifecycle() == RenderCaptureSegment::Lifecycle::Frozen)
			next.MarkCompiled();
		if (!next.Reset(presented_frame_serial, 0, 0))
			return Fail("capture", "unable to reset the next presented-frame capture",
				true);
		active_capture = replacement;
		return true;
	}

	bool EnsureCurrentDrawable()
	{
		uint32_t width = 0;
		uint32_t height = 0;
		if (!QueryDrawable(&width, &height))
			return false;
		if (width == drawable_width && height == drawable_height && wsi.Ready() &&
			!wsi.GenerationStopped())
			return true;
		WsiCreateInfo wsi_info;
		wsi_info.drawable_width = width;
		wsi_info.drawable_height = height;
		const WsiStatus status = wsi.Recreate(wsi_info);
		if (status == WsiStatus::Paused)
		{
			drawable_width = width;
			drawable_height = height;
			return true;
		}
		if (status != WsiStatus::Success && status != WsiStatus::Suboptimal)
			return Fail("wsi", std::string("drawable recreation failed: ") +
				WsiStatusName(status), status == WsiStatus::DeviceLost ||
				status == WsiStatus::SurfaceLost);
		const VkExtent2D extent = wsi.Extent();
		const uint64_t prior_use = frames.LastAllocatedTimeline();
		if (prior_use != 0 && !frames.WaitTimeline(prior_use, UINT64_MAX))
			return Fail("targets",
				"waiting to replace drawable-sized targets failed", true);
		if (!targets.Configure(preferred, extent, prior_use))
			return Fail("targets", "drawable-sized target replacement failed", true);
		targets.InvalidateHistories();
		drawable_width = extent.width;
		drawable_height = extent.height;
		return true;
	}

	bool RecoverSurface()
	{
		if (platform.DeviceLost())
			return false;
		wsi.Shutdown(false);
		SurfaceSource surface;
		uint32_t width = 0, height = 0;
		if (!QueryWindow(&surface, &width, &height) ||
			!platform.RecreateSurface(surface))
			return Fail("surface-recovery",
				"one-shot native surface recreation failed", true);
		WsiCreateInfo info;
		info.drawable_width = width;
		info.drawable_height = height;
		const WsiStatus status = wsi.Initialize(info);
		if (status != WsiStatus::Success && status != WsiStatus::Suboptimal &&
			status != WsiStatus::Paused)
			return Fail("surface-recovery", std::string(
				"swapchain recreation failed: ") + WsiStatusName(status), true);
		VkExtent2D extent = wsi.Extent();
		if (extent.width == 0 || extent.height == 0)
			extent = { width, height };
		if (extent.width != drawable_width || extent.height != drawable_height)
		{
			const uint64_t prior_use = frames.LastAllocatedTimeline();
			if (prior_use != 0 && !frames.WaitTimeline(prior_use, UINT64_MAX))
				return Fail("surface-recovery",
					"waiting to replace drawable targets failed", true);
			if (!targets.Configure(preferred, extent, prior_use))
				return Fail("surface-recovery",
					"drawable target replacement failed", true);
		}
		drawable_width = extent.width;
		drawable_height = extent.height;
		targets.InvalidateHistories();
		SetDiagnostic("surface-recovery: native surface and swapchain recreated");
		return true;
	}

	void CaptureLatestEpochs(const RenderCaptureSegment &capture) noexcept
	{
		for (const CapturedTargetVersion &version : capture.TargetVersions())
		{
			const uint32_t index = TargetIndex(version.target);
			target_color_epoch[index] = std::max(target_color_epoch[index],
				version.color_epoch);
			target_depth_epoch[index] = std::max(target_depth_epoch[index],
				version.depth_epoch);
		}
	}

	bool CompleteBytes(const CompiledReadback &readback,
		const ReadbackCompletion &completion, const std::string &png_path)
	{
		if (!readback.slice.Valid() || !readback.slice.mapped ||
			readback.width == 0 || readback.height == 0)
			return Fail("readback", "compiler returned an invalid mapped readback");
		// The compiler deliberately copies a canonical RGBA8 transfer image for
		// every color destination. Conversion into legacy RGB565/RGB8 is a CPU
		// completion concern and never changes the GPU copy shape.
		const uint64_t byte_count64 = uint64_t(readback.width) *
			readback.height * 4u;
		if (byte_count64 > readback.slice.size ||
			byte_count64 > std::numeric_limits<uint32_t>::max())
			return Fail("readback", "mapped readback byte range is truncated");
		const uint32_t byte_count = static_cast<uint32_t>(byte_count64);
		FrameContext *context = frames.Current();
		if (context)
		{
			const AllocatedBuffer &buffer = context->buffers[
				static_cast<uint32_t>(FrameBufferClass::Readback)];
			if (buffer.handle == readback.slice.buffer &&
				!allocator.Invalidate(buffer, readback.slice.offset, byte_count))
				return Fail("readback", "mapped readback invalidation failed");
		}

		const uint8_t *source = static_cast<const uint8_t *>(
			readback.slice.mapped);
		switch (completion.destination)
		{
		case ReadbackDestination::CpuColor:
			if (readback.format != ReadbackFormat::RawRgba8 ||
				!completion.cpu_bytes || completion.cpu_byte_size < byte_count)
				return Fail("readback", "CPU destination is too small");
			std::memcpy(completion.cpu_bytes, source, byte_count);
			return true;
		case ReadbackDestination::BitmapHandle:
			if (completion.bitmap_handle < 0 ||
				bm_w(completion.bitmap_handle, 0) !=
					static_cast<int>(readback.width) ||
				bm_h(completion.bitmap_handle, 0) !=
					static_cast<int>(readback.height) ||
				readback.format != ReadbackFormat::Rgb565)
				return Fail("readback", "bitmap destination shape or format mismatch");
			if (!bm_data(completion.bitmap_handle, 0))
				return Fail("readback", "bitmap destination has no storage");
			{
				uint16_t *destination = bm_data(completion.bitmap_handle, 0);
				const uint64_t pixel_count = uint64_t(readback.width) *
					readback.height;
				for (uint64_t pixel = 0; pixel < pixel_count; ++pixel)
				{
					const uint8_t *rgba = source + pixel * 4u;
					destination[pixel] = static_cast<uint16_t>(
						((rgba[0] >> 3) << 10) |
						((rgba[1] >> 3) << 5) | (rgba[2] >> 3));
				}
			}
			return true;
		case ReadbackDestination::PngPath:
			if (png_path.empty() || readback.format != ReadbackFormat::Rgb8TopDown)
				return Fail("readback", "PNG destination shape or format mismatch");
			{
				std::vector<uint8_t> rgba(size_t(readback.width) *
					readback.height * 4u);
				for (uint64_t pixel = 0;
					pixel < uint64_t(readback.width) * readback.height; ++pixel)
				{
					rgba[size_t(pixel) * 4u + 0u] = source[pixel * 4u + 0u];
					rgba[size_t(pixel) * 4u + 1u] = source[pixel * 4u + 1u];
					rgba[size_t(pixel) * 4u + 2u] = source[pixel * 4u + 2u];
					rgba[size_t(pixel) * 4u + 3u] = 255;
				}
				return bm_SaveRawRGBA32PNG(png_path.c_str(),
					static_cast<int>(readback.width),
					static_cast<int>(readback.height), rgba.data()) != 0;
			}
		default:
			return Fail("readback", "unknown readback destination");
		}
	}
};

VulkanRuntime::VulkanRuntime() : impl_(new Impl) {}

VulkanRuntime::~VulkanRuntime()
{
	Shutdown();
	delete impl_;
	impl_ = nullptr;
}

bool VulkanRuntime::Initialize(oeApplication *app,
	const CapturedPreferredState &preferred)
{
	if (!impl_ || !app || preferred.width == 0 || preferred.height == 0 ||
		preferred.window_width == 0 || preferred.window_height == 0 ||
		(preferred.supersampling_factor != 1 &&
		 preferred.supersampling_factor != 2 &&
		 preferred.supersampling_factor != 4))
		return impl_ && impl_->Fail("initialize", "invalid runtime arguments");
	if (impl_->initialized)
		return impl_->Fail("initialize", "runtime is already initialized");

	impl_->fatal = false;
	impl_->application = app;
	impl_->preferred = preferred;
	// The Win32 application starts at a legacy desktop-sized default. Establish
	// the requested client/fullscreen state before the surface and swapchain
	// observe its drawable extent.
	if (!impl_->ConfigureApplicationWindow(preferred))
		return impl_->Fail("initialize", "platform window configuration failed");
	SurfaceSource surface;
	uint32_t drawable_width = 0;
	uint32_t drawable_height = 0;
	if (!impl_->QueryWindow(&surface, &drawable_width, &drawable_height))
		return false;

	PlatformCreateInfo platform_info;
	platform_info.application_name = "PiccuEngine";
	platform_info.application_version = VK_MAKE_API_VERSION(0, 1, 0, 0);
	platform_info.surface = surface;
	platform_info.requested_profile = BuildRequestedProfile(preferred);
	platform_info.enable_validation = EnvironmentFlag(
		"PICCU_VULKAN_VALIDATION") ? 1u : 0u;
	platform_info.require_validation = EnvironmentFlag(
		"PICCU_VULKAN_VALIDATION_REQUIRED") ? 1u : 0u;
	platform_info.verbose_validation = EnvironmentFlag(
		"PICCU_VULKAN_VALIDATION_VERBOSE") ? 1u : 0u;
	platform_info.log_callback = &Impl::Log;
	platform_info.log_user = impl_;
	const PlatformStatus platform_status = impl_->platform.Initialize(platform_info);
	if (platform_status != PlatformStatus::Success)
	{
		impl_->Fail("platform", std::string(PlatformStatusName(platform_status)) +
			": " + impl_->platform.DiagnosticsText());
		impl_->ShutdownModules(false);
		return false;
	}

	if (!impl_->allocator.Initialize(impl_->platform.Instance(),
		impl_->platform.PhysicalDevice(), impl_->platform.Device(),
		impl_->platform.EnabledApiVersion(),
		impl_->platform.SelectedDevice().memory_budget_enabled != 0))
	{
		impl_->Fail("allocator", "VMA initialization failed");
		impl_->ShutdownModules(false);
		return false;
	}

	WsiCreateInfo wsi_info;
	wsi_info.drawable_width = drawable_width;
	wsi_info.drawable_height = drawable_height;
	const WsiStatus wsi_status = impl_->wsi.Initialize(wsi_info);
	if (wsi_status != WsiStatus::Success && wsi_status != WsiStatus::Suboptimal &&
		wsi_status != WsiStatus::Paused)
	{
		impl_->Fail("wsi", std::string(WsiStatusName(wsi_status)) + ": " +
			impl_->wsi.LastError());
		impl_->ShutdownModules(wsi_status == WsiStatus::DeviceLost);
		return false;
	}

	const uint32_t supported_samples = impl_->platform.SelectedDevice().
		contract_candidate.profile.common_sample_count_mask;
	VkExtent2D initial_target_extent = impl_->wsi.Extent();
	if (initial_target_extent.width == 0 || initial_target_extent.height == 0)
	{
		initial_target_extent.width = preferred.window_width;
		initial_target_extent.height = preferred.window_height;
	}
	if (!impl_->targets.Initialize(&impl_->allocator, &impl_->state_tracker,
		supported_samples) ||
		!impl_->targets.Configure(preferred, initial_target_extent, 0))
	{
		impl_->Fail("targets", "initial target generation allocation failed");
		impl_->ShutdownModules(false);
		return false;
	}
	if (!impl_->frames.Initialize(&impl_->platform, &impl_->allocator))
	{
		impl_->Fail("frames", "two-slot frame scheduler initialization failed");
		impl_->ShutdownModules(false);
		return false;
	}
	impl_->state_tracker.Reserve(256, 512, 256);
	if (!impl_->textures.Initialize(&impl_->platform, &impl_->allocator,
		&impl_->frames, &impl_->state_tracker))
	{
		impl_->Fail("textures", "texture manager initialization failed");
		impl_->ShutdownModules(false);
		return false;
	}
	if (!impl_->retained_world.Initialize(&impl_->allocator, &impl_->frames,
		&impl_->state_tracker))
	{
		impl_->Fail("retained-world",
			"device-local retained room/model/terrain arena initialization failed");
		impl_->ShutdownModules(false);
		return false;
	}
	if (!impl_->pipelines->Initialize(impl_->PipelineInfo(preferred)))
	{
		impl_->Fail("pipelines", impl_->pipelines->LastError());
		impl_->ShutdownModules(false);
		return false;
	}
	if (!impl_->InitializeCompiler())
	{
		impl_->ShutdownModules(false);
		return false;
	}

	const CaptureReserve reserve = DefaultCaptureReserve();
	impl_->captures[0].Reserve(reserve);
	impl_->captures[1].Reserve(reserve);
	if (!impl_->captures[0].Reset(kInitialPresentedFrameSerial, 0, 0))
	{
		impl_->Fail("capture", "initial capture reset failed");
		impl_->ShutdownModules(false);
		return false;
	}
	impl_->active_capture = 0;
	impl_->drawable_width = drawable_width;
	impl_->drawable_height = drawable_height;
	impl_->initialized = true;
	impl_->selected_pipeline = 0;
	impl_->SetDiagnostic(impl_->platform.DiagnosticsText());
	return true;
}

void VulkanRuntime::Shutdown()
{
	if (!impl_)
		return;
	const bool device_lost = impl_->platform.DeviceLost();
	impl_->ShutdownModules(device_lost);
	impl_->application = nullptr;
	impl_->native_window = nullptr;
	impl_->drawable_width = impl_->drawable_height = 0;
	impl_->selected_pipeline = 0;
	impl_->preferred = CapturedPreferredState();
}

bool VulkanRuntime::ApplyPreferredState(
	const CapturedPreferredState &preferred)
{
	if (!impl_ || !impl_->initialized || impl_->fatal)
		return impl_ && impl_->Fail("preferences", "runtime is not available");
	if (preferred.width == 0 || preferred.height == 0 ||
		preferred.window_width == 0 || preferred.window_height == 0 ||
		(preferred.supersampling_factor != 1 &&
		 preferred.supersampling_factor != 2 &&
		 preferred.supersampling_factor != 4))
		return impl_->Fail("preferences", "invalid preferred-state dimensions");
	if (!impl_->Capture().IsEmpty() || !impl_->pending_readbacks.empty())
		return impl_->Fail("preferences",
			"preference replacement requires an empty presented-frame boundary");

	const CapturedPreferredState old = impl_->preferred;
	const bool window_changed = old.window_width != preferred.window_width ||
		old.window_height != preferred.window_height ||
		old.fullscreen != preferred.fullscreen;
	const bool full_target_change = FullTargetSignatureChanged(old, preferred);
	const bool gtao_size_change = !full_target_change &&
		old.gtao_resolution != preferred.gtao_resolution;
	const uint32_t supported = impl_->platform.SelectedDevice().
		contract_candidate.profile.common_sample_count_mask;
	const bool pipeline_rebuild = AppliedMsaa(old, supported) !=
		AppliedMsaa(preferred, supported);
	if (!window_changed && !full_target_change && !gtao_size_change &&
		!pipeline_rebuild)
	{
		// Sampler choice, gamma, lighting enable, and all remaining scalar/
		// branch fields are captured dynamically and preserve every GPU object.
		impl_->preferred = preferred;
		impl_->targets.UpdateDynamicPreferredState(preferred);
		return true;
	}
	std::unique_ptr<PipelineLibrary> replacement;
	if (pipeline_rebuild)
	{
		replacement.reset(new PipelineLibrary);
		if (!replacement->Initialize(impl_->PipelineInfo(preferred)))
			return impl_->Fail("preferences", std::string(
				"replacement pipeline set failed: ") + replacement->LastError());
	}

	if (window_changed && !impl_->ConfigureApplicationWindow(preferred))
		return impl_->Fail("preferences", "platform window reconfiguration failed");

	uint32_t requested_width = impl_->drawable_width;
	uint32_t requested_height = impl_->drawable_height;
	if (!impl_->QueryDrawable(&requested_width, &requested_height))
	{
		if (window_changed)
			impl_->ConfigureApplicationWindow(old);
		return false;
	}
	WsiCreateInfo wsi_info;
	wsi_info.drawable_width = requested_width;
	wsi_info.drawable_height = requested_height;
	WsiStatus wsi_status = WsiStatus::Success;
	const bool surface_changed = requested_width != impl_->drawable_width ||
		requested_height != impl_->drawable_height;
	const bool recreate_wsi = window_changed || surface_changed;
	if (recreate_wsi)
		wsi_status = impl_->wsi.Recreate(wsi_info);
	if (wsi_status != WsiStatus::Success && wsi_status != WsiStatus::Suboptimal)
	{
		if (window_changed)
		{
			impl_->ConfigureApplicationWindow(old);
			uint32_t rollback_width = 0;
			uint32_t rollback_height = 0;
			if (impl_->QueryDrawable(&rollback_width, &rollback_height))
			{
				WsiCreateInfo rollback;
				rollback.drawable_width = rollback_width;
				rollback.drawable_height = rollback_height;
				impl_->wsi.Recreate(rollback);
			}
		}
		return impl_->Fail("preferences", std::string(
			"swapchain replacement failed: ") + WsiStatusName(wsi_status) +
			(impl_->wsi.LastError().empty() ? "" :
				std::string(": ") + impl_->wsi.LastError()));
	}

	const VkExtent2D extent = impl_->wsi.Extent();
	const bool rebuild_targets = recreate_wsi || full_target_change ||
		gtao_size_change;
	const uint64_t prior_target_use = impl_->frames.LastAllocatedTimeline();
	if (rebuild_targets && prior_target_use != 0 &&
		!impl_->frames.WaitTimeline(prior_target_use, UINT64_MAX))
		return impl_->Fail("preferences",
			"waiting to replace the prior target generation failed", true);
	if (rebuild_targets && !impl_->targets.Configure(preferred, extent,
		prior_target_use))
	{
		if (window_changed)
			impl_->ConfigureApplicationWindow(old);
		if (recreate_wsi)
		{
			WsiCreateInfo rollback;
			rollback.drawable_width = impl_->drawable_width;
			rollback.drawable_height = impl_->drawable_height;
			impl_->wsi.Recreate(rollback);
		}
		return impl_->Fail("preferences",
			"target replacement failed; prior target generation restored when possible",
			!impl_->targets.Ready());
	}

	if (pipeline_rebuild)
	{
		const uint64_t prior_use = impl_->frames.LastAllocatedTimeline();
		if (prior_use != 0 && !impl_->frames.WaitTimeline(prior_use, UINT64_MAX))
			return impl_->Fail("preferences",
				"waiting to retire the prior pipeline set failed", true);
		impl_->compiler.Shutdown(false);
		std::unique_ptr<PipelineLibrary> prior = std::move(impl_->pipelines);
		impl_->pipelines = std::move(replacement);
		if (!impl_->InitializeCompiler())
		{
			impl_->pipelines->Shutdown(false);
			impl_->pipelines = std::move(prior);
			impl_->InitializeCompiler();
			return impl_->Fail("preferences",
				"replacement compiler publication failed", true);
		}
		prior->Shutdown(false);
	}
	impl_->preferred = preferred;
	impl_->targets.UpdateDynamicPreferredState(preferred);
	impl_->drawable_width = extent.width;
	impl_->drawable_height = extent.height;
	if (rebuild_targets)
		impl_->targets.InvalidateHistories();
	return true;
}

RenderCaptureSegment *VulkanRuntime::ActiveCapture()
{
	if (!impl_ || !impl_->initialized || impl_->fatal ||
		impl_->Capture().GetLifecycle() != RenderCaptureSegment::Lifecycle::Capturing)
		return nullptr;
	return &impl_->Capture();
}

IRetainedWorld *VulkanRuntime::RetainedWorldBridge()
{
	if (!impl_ || !impl_->initialized || impl_->fatal ||
		!impl_->retained_world.Ready())
		return nullptr;
	return &impl_->retained_world;
}

bool VulkanRuntime::DescribeTarget(const TargetRequest &request,
	TargetDescription *description)
{
	if (!impl_ || !impl_->initialized || impl_->fatal || !description ||
		request.logical_clip.width <= 0 || request.logical_clip.height <= 0)
		return impl_ && impl_->Fail("target", "invalid target description request");
	if (impl_->Capture().IsEmpty() && !impl_->EnsureCurrentDrawable())
		return false;
	const CapturedTargetLayout *layout = LayoutFor(impl_->targets, request.target);
	if (!layout || layout->logical_width == 0 || layout->logical_height == 0)
		return impl_->Fail("target", "requested target class is unavailable");
	LogicalRect target_bounds = {};
	if (!BuildLogicalTargetBounds(*layout, &target_bounds))
		return impl_->Fail("target", "target has invalid logical bounds");
	const int64_t right = int64_t(request.logical_clip.x) +
		request.logical_clip.width;
	const int64_t bottom = int64_t(request.logical_clip.y) +
		request.logical_clip.height;
	const int64_t target_right = int64_t(target_bounds.x) + target_bounds.width;
	const int64_t target_bottom = int64_t(target_bounds.y) + target_bounds.height;
	if (request.logical_clip.x < target_bounds.x ||
		request.logical_clip.y < target_bounds.y || right > target_right ||
		bottom > target_bottom)
		return impl_->Fail("target", "logical clip lies outside the target");

	*description = TargetDescription();
	description->layout = *layout;
	const TargetVersionId latest = LatestVersionFor(impl_->Capture(),
		request.target);
	const CapturedTargetVersion *captured = VersionAt(impl_->Capture(), latest);
	const uint32_t index = TargetIndex(request.target);
	const uint32_t color_epoch = captured ? captured->color_epoch :
		impl_->target_color_epoch[index];
	const uint32_t depth_epoch = captured ? captured->depth_epoch :
		impl_->target_depth_epoch[index];
	description->version = impl_->targets.DescribeVersion(request.target,
		0, color_epoch, depth_epoch);

	const uint32_t factor = layout->ssaa_factor;
	const int32_t overscan_x = -target_bounds.x;
	const int32_t overscan_y = -target_bounds.y;
	description->viewport.logical_rect = request.logical_clip;
	description->viewport.physical_rect = {
		(request.logical_clip.x + overscan_x) * static_cast<int32_t>(factor),
		(request.logical_clip.y + overscan_y) * static_cast<int32_t>(factor),
		request.logical_clip.width * static_cast<int32_t>(factor),
		request.logical_clip.height * static_cast<int32_t>(factor),
	};
	description->viewport.target_width = layout->internal_width;
	description->viewport.target_height = layout->internal_height;
	description->viewport.ssaa_factor = factor;
	description->viewport.scissor_enabled = 1;

	if (!impl_->Capture().Views().empty())
		description->view = impl_->Capture().Views().back();
	else
	{
		std::memset(&description->view, 0, sizeof(description->view));
		float *matrices[] = { description->view.unscaled_view,
			description->view.projection, description->view.view,
			description->view.view_projection,
			description->view.inverse_modelview,
			description->view.inverse_view_projection,
			description->view.previous_view_projection,
			description->view.cockpit_previous_view_projection };
		for (float *matrix : matrices)
			matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0f;
		description->view.matrix_scale[0] =
			description->view.matrix_scale[1] =
			description->view.matrix_scale[2] = 1.0f;
	}
	description->view.logical_clip = request.logical_clip;
	description->view.window_center_extent[0] =
		request.logical_clip.x + request.logical_clip.width * 0.5f;
	description->view.window_center_extent[1] =
		request.logical_clip.y + request.logical_clip.height * 0.5f;
	description->view.window_center_extent[2] =
		request.logical_clip.width * 0.5f;
	description->view.window_center_extent[3] =
		request.logical_clip.height * 0.5f;
	description->view.anchor_offset[0] =
		static_cast<float>(request.logical_clip.x);
	description->view.anchor_offset[1] =
		static_cast<float>(request.logical_clip.y);
	return true;
}

bool VulkanRuntime::DescribePresentation(
	const CapturedPreferredState &preferred,
	PresentationDescription *description)
{
	if (!impl_ || !impl_->initialized || impl_->fatal || !description ||
		!impl_->wsi.Ready() || !impl_->targets.Ready())
		return impl_ && impl_->Fail("presentation",
			"presentation state is unavailable");
	*description = PresentationDescription();
	description->scene_layout = impl_->targets.SceneLayout();
	description->post_present_layout = impl_->targets.PostLayout();
	description->cockpit_scene_layout = impl_->targets.CockpitLayout();
	description->wsi = impl_->wsi.Signature();
	uint32_t present_width = impl_->wsi.Extent().width;
	uint32_t present_height = impl_->wsi.Extent().height;
	if (impl_->wsi.Generation() == 0 && impl_->wsi.Paused())
	{
		// An application may start minimized before a native swapchain exists.
		// Freeze a valid, non-acquirable signature over the target fallback size;
		// Wsi::Acquire still returns Paused and no binary semaphore is consumed.
		present_width = description->scene_layout.drawable_width;
		present_height = description->scene_layout.drawable_height;
		description->wsi.swapchain_generation = 0;
		description->wsi.format = SurfacePixelFormat::B8G8R8A8Unorm;
		description->wsi.color_space = SurfaceColorSpace::SrgbNonlinear;
		description->wsi.present_mode = PresentModeContract::Fifo;
		description->wsi.composite_alpha = CompositeAlphaContract::Opaque;
		description->wsi.surface_transform = 1;
		description->wsi.drawable_width = present_width;
		description->wsi.drawable_height = present_height;
		description->wsi.image_count = 3;
		description->wsi.graphics_queue_family =
			impl_->platform.GraphicsQueueFamily();
		description->wsi.present_queue_family =
			impl_->platform.PresentQueueFamily();
		description->wsi.concurrent_sharing =
			impl_->platform.UsesSeparatePresentQueue() ? 1u : 0u;
		description->wsi.safe_authored_unorm = 1;
	}
	description->present_rect.drawable_width = present_width;
	description->present_rect.drawable_height = present_height;
	description->present_rect.rect = ComputeCanonicalPresentRect(
		{ static_cast<int32_t>(preferred.width),
		  static_cast<int32_t>(preferred.height) },
		{ static_cast<int32_t>(present_width),
		  static_cast<int32_t>(present_height) });
	description->present_rect.surface_transform =
		description->wsi.surface_transform;
	description->present_rect.swapchain_generation = impl_->wsi.Generation();
	// paused/frozen/frame_time/afterburner are live engine inputs overlaid by
	// VulkanRenderer.  WSI pause is an acquire state, not a game-history freeze.
	description->dynamic.gtao_history_valid =
		impl_->targets.GtaoHistoryValid() ? 1u : 0u;
	description->dynamic.motion_history_valid =
		impl_->targets.MotionHistoryValid() ? 1u : 0u;
	description->dynamic.cockpit_history_valid =
		impl_->targets.CockpitHistoryValid() ? 1u : 0u;
	description->dynamic.frame_serial = impl_->Capture().PresentedFrameSerial();
	const uint32_t factor = std::max(1u, description->scene_layout.ssaa_factor);
	const uint32_t logical_internal_width =
		description->scene_layout.internal_width / factor;
	const uint32_t logical_internal_height =
		description->scene_layout.internal_height / factor;
	description->dynamic.visible_origin_size[0] = static_cast<float>(
		((logical_internal_width - description->scene_layout.logical_width + 1u) /
			2u) * factor);
	description->dynamic.visible_origin_size[1] = static_cast<float>(
		((logical_internal_height - description->scene_layout.logical_height + 1u) /
			2u) * factor);
	description->dynamic.visible_origin_size[2] = static_cast<float>(
		description->scene_layout.logical_width * factor);
	description->dynamic.visible_origin_size[3] = static_cast<float>(
		description->scene_layout.logical_height * factor);
	description->dynamic.source_destination_extent[0] =
		static_cast<float>(description->scene_layout.internal_width);
	description->dynamic.source_destination_extent[1] =
		static_cast<float>(description->scene_layout.internal_height);
	description->dynamic.source_destination_extent[2] =
		static_cast<float>(description->post_present_layout.internal_width);
	description->dynamic.source_destination_extent[3] =
		static_cast<float>(description->post_present_layout.internal_height);
	description->dynamic.motion_consumer_active =
		WantsMotionResources(preferred) ? 1u : 0u;
	for (const CaptureCommand &command : impl_->Capture().Commands())
		if (command.type == CaptureCommandType::CaptureBloomSource)
		{
			description->dynamic.captured_world_valid = 1;
			description->dynamic.captured_depth_valid = 1;
		}
	description->defer_bloom = 0;
	description->dynamic.defer_bloom = description->defer_bloom;
	return true;
}

bool VulkanRuntime::ResolveTexture(const TextureRequest &request,
	ResolvedTexture *resolved)
{
	if (!impl_ || !impl_->initialized || impl_->fatal || !resolved ||
		impl_->Capture().GetLifecycle() != RenderCaptureSegment::Lifecycle::Capturing)
		return impl_ && impl_->Fail("texture", "texture resolution is unavailable");
	if (!impl_->textures.Resolve(request, &impl_->Capture(), resolved))
		return impl_->Fail("texture", "legacy texture snapshot/resolution failed");
	return true;
}

bool VulkanRuntime::ResolveTerrainTextureArrays(
	const TerrainArrayRequest &request, ResolvedTexture *base,
	ResolvedTexture *lightmap)
{
	if (!impl_ || !impl_->initialized || impl_->fatal || !base || !lightmap ||
		impl_->Capture().GetLifecycle() != RenderCaptureSegment::Lifecycle::Capturing)
		return impl_ && impl_->Fail("terrain-texture",
			"terrain texture resolution is unavailable");
	if (!impl_->textures.ResolveTerrainArrays(request, &impl_->Capture(),
		base, lightmap))
		return impl_->Fail("terrain-texture",
			"terrain array snapshot/resolution failed");
	return true;
}

bool VulkanRuntime::NotifyTextureEvent(TextureEvent event,
	int32_t logical_handle, uint32_t map_type)
{
	if (!impl_ || !impl_->initialized || impl_->fatal)
		return impl_ && impl_->Fail("texture-event", "runtime is unavailable");
	(void)logical_handle;
	(void)map_type;
	switch (event)
	{
	case TextureEvent::PreUpload:
	case TextureEvent::ReleasePreUpload:
		// These are observable compatibility no-ops in the pinned GL4 renderer.
		return true;
	case TextureEvent::ResetCache:
		impl_->textures.Invalidate(TextureMappingInvalidationReason::ResetCache,
			impl_->frames.PollCompletedTimeline());
		return true;
	default:
		return impl_->Fail("texture-event", "unknown texture event");
	}
}

bool VulkanRuntime::CompleteReadback(const ReadbackCompletion &completion)
{
	if (!impl_ || !impl_->initialized || impl_->fatal ||
		completion.request == kInvalidId)
		return impl_ && impl_->Fail("readback", "invalid readback request");
	if ((completion.destination == ReadbackDestination::CpuColor &&
		 (!completion.cpu_bytes || completion.cpu_byte_size == 0)) ||
		(completion.destination == ReadbackDestination::BitmapHandle &&
		 completion.bitmap_handle < 0) ||
		(completion.destination == ReadbackDestination::PngPath &&
		 (!completion.png_path || !completion.png_path[0])))
		return impl_->Fail("readback", "invalid readback destination");
	const std::string png_path = completion.png_path ? completion.png_path : "";
	RenderCaptureSegment &prefix = impl_->Capture();
	if (prefix.Commands().empty())
		return impl_->Fail("readback", "capture contains no readback command");
	const CaptureCommand &terminal = prefix.Commands().back();
	const bool matching_image = terminal.type == CaptureCommandType::ReadImage &&
		terminal.payload.read_image.request == completion.request;
	if (matching_image && completion.destination == ReadbackDestination::PngPath)
	{
		for (const Impl::PendingReadback &existing : impl_->pending_readbacks)
			if (existing.completion.request == completion.request)
				return impl_->Fail("readback",
					"duplicate deferred readback request");
		Impl::PendingReadback pending;
		pending.completion = completion;
		pending.png_path = png_path;
		// The caller's path storage may be transient. Only the owned string is
		// consulted after this method returns.
		pending.completion.png_path = nullptr;
		impl_->pending_readbacks.push_back(pending);
		return true;
	}
	const bool matching_pixel = terminal.type == CaptureCommandType::ReadPixel &&
		terminal.payload.read_pixel.request == completion.request;
	if (!matching_image && !matching_pixel)
		return impl_->Fail("readback",
			"request does not match the terminal readback command");
	CaptureValidationResult validation = {};
	if (!prefix.Freeze(&validation))
	{
		std::ostringstream reason;
		reason << "capture prefix validation failed: errors=0x" << std::hex
			<< validation.errors << std::dec << " command="
			<< validation.command_index << " table=" << validation.table_index;
		return impl_->Fail("readback", reason.str());
	}
	CompilerSubmission submission;
	if (!impl_->compiler.CompileAndSubmit(&prefix, false, &submission) ||
		submission.timeline_value == 0)
		return impl_->Fail("readback", std::string("prefix submission failed: ") +
			impl_->compiler.LastError(), impl_->platform.DeviceLost());
	if (!impl_->frames.WaitTimeline(submission.timeline_value, UINT64_MAX))
		return impl_->Fail("readback", "timeline wait failed",
			impl_->platform.DeviceLost());
	++impl_->synchronous_readback_waits;

	const CompiledReadback *matching = nullptr;
	for (const CompiledReadback &readback : submission.readbacks)
		if (readback.request == completion.request)
		{
			matching = &readback;
			break;
		}
	const bool delivered = matching && impl_->CompleteBytes(*matching,
		completion, png_path);
	const ResourceStateSnapshot *snapshot = impl_->compiler.FindSnapshot(
		submission.resource_state_snapshot_serial);
	if (!snapshot || !impl_->state_tracker.Restore(*snapshot))
		return impl_->Fail("readback",
			"submitted resource-state snapshot is unavailable", true);
	CaptureContinuationState state;
	if (!BuildContinuationState(prefix, submission.timeline_value,
		submission.resource_state_snapshot_serial, &state))
		return impl_->Fail("readback", "continuation state construction failed",
			true);
	impl_->CaptureLatestEpochs(prefix);
	if (prefix.GetLifecycle() == RenderCaptureSegment::Lifecycle::Frozen &&
		!prefix.MarkCompiled())
		return impl_->Fail("readback", "prefix lifecycle publication failed", true);
	const uint32_t next_index = impl_->active_capture ^ 1u;
	RenderCaptureSegment &continuation = impl_->captures[next_index];
	const uint32_t next_segment = prefix.SegmentSerial() == UINT32_MAX ? 0 :
		prefix.SegmentSerial() + 1;
	const uint64_t first_serial = NextCommandSerial(prefix);
	if (next_segment == 0 || first_serial == 0 ||
		!continuation.ResetContinuationFrom(prefix, next_segment, first_serial,
			state))
		return impl_->Fail("readback", "capture continuation reset failed", true);
	impl_->active_capture = next_index;
	return delivered;
}

bool VulkanRuntime::SubmitPresentedFrame(uint32_t presented_frame_serial)
{
	if (!impl_ || !impl_->initialized || impl_->fatal ||
		impl_->Capture().PresentedFrameSerial() != presented_frame_serial ||
		presented_frame_serial == UINT32_MAX)
		return impl_ && impl_->Fail("present", "presented-frame serial mismatch");
	RenderCaptureSegment &capture = impl_->Capture();
	CaptureValidationResult validation = {};
	if (!capture.Freeze(&validation))
	{
		std::ostringstream reason;
		reason << "capture validation/freeze failed: errors=0x" << std::hex
			<< validation.errors << std::dec << " command="
			<< validation.command_index << " table=" << validation.table_index;
		if (validation.command_index < capture.Commands().size())
		{
			const CaptureCommand &bad = capture.Commands()[validation.command_index];
			reason << " type=" << static_cast<uint32_t>(bad.type);
			if (bad.type == CaptureCommandType::CaptureBloomSource)
			{
				const CaptureBloomSourceCommand &bloom =
					bad.payload.capture_bloom_source;
				reason << " bloomVersion=" << bloom.scene_target_version
					<< " bloomRect=" << bloom.visible_rect.x << ','
					<< bloom.visible_rect.y << ',' << bloom.visible_rect.width
					<< ',' << bloom.visible_rect.height;
			}
			StateId state = kInvalidId;
			if (bad.type == CaptureCommandType::DrawStream)
				state = bad.payload.draw_stream.state;
			else if (bad.type == CaptureCommandType::DrawRetained)
				state = bad.payload.draw_retained.state;
			if (state < capture.States().size())
				reason << " state=" << state << " stateLayout="
					<< capture.States()[state].target_layout << " samples="
					<< capture.States()[state].sample_count << " mrt=0x"
					<< std::hex << capture.States()[state].mrt_write_mask << std::dec;
			for (uint32_t i = validation.command_index + 1; i > 0; --i)
			{
				const CaptureCommand &prior = capture.Commands()[i - 1];
				if (prior.type == CaptureCommandType::BeginFrameTarget)
				{
					const BeginFrameTargetCommand &begin =
						prior.payload.begin_frame_target;
					reason << " activeTarget=" << static_cast<uint32_t>(begin.target)
						<< " activeVersion=" << begin.active_target_version
						<< " activeClip=" << begin.logical_clip.x << ','
						<< begin.logical_clip.y << ',' << begin.logical_clip.width
						<< ',' << begin.logical_clip.height;
					if (begin.active_target_version < capture.TargetVersions().size())
						reason << " activeLayout=" << capture.TargetVersions()[
							begin.active_target_version].target_layout;
					break;
				}
				if (prior.type == CaptureCommandType::EndFrame)
					break;
			}
		}
		if (validation.table_index < capture.States().size())
		{
			const CapturedShaderRasterState &state =
				capture.States()[validation.table_index];
			reason << " tableStateLayout=" << state.target_layout
				<< " tableStateSamples=" << state.sample_count
				<< " tableStateMrt=0x" << std::hex << state.mrt_write_mask
				<< std::dec;
			if (state.target_layout < capture.TargetLayouts().size())
				reason << " layoutSamples=" << capture.TargetLayouts()[
					state.target_layout].msaa_samples << " layoutMask=0x"
					<< std::hex << capture.TargetLayouts()[state.target_layout].
						attachment_mask << std::dec;
		}
		if (validation.table_index < capture.TargetVersions().size())
		{
			const CapturedTargetVersion &version =
				capture.TargetVersions()[validation.table_index];
			reason << " tableVersionTarget=" << static_cast<uint32_t>(version.target)
				<< " tableVersionLayout=" << version.target_layout << " extent="
				<< version.width << 'x' << version.height << " samples="
				<< version.samples << " epochs=" << version.color_epoch << ','
				<< version.depth_epoch;
			if (version.target_layout < capture.TargetLayouts().size())
			{
				const CapturedTargetLayout &layout =
					capture.TargetLayouts()[version.target_layout];
				reason << " expectedTarget=" << static_cast<uint32_t>(layout.target)
					<< " expectedExtent=" << layout.internal_width << 'x'
					<< layout.internal_height << " logical=" << layout.logical_width
					<< 'x' << layout.logical_height << " expectedSamples="
					<< layout.msaa_samples;
			}
		}
		return impl_->Fail("present", reason.str());
	}
	CompilerSubmission submission;
	bool compiled = impl_->compiler.CompileAndSubmit(&capture, true, &submission);
	if ((!compiled || submission.timeline_value == 0) &&
		impl_->wsi.LastVulkanResult() == VK_ERROR_SURFACE_LOST_KHR &&
		impl_->RecoverSurface())
	{
		submission = CompilerSubmission();
		compiled = impl_->compiler.CompileAndSubmit(&capture, true, &submission);
	}
	if (!compiled || submission.timeline_value == 0)
		return impl_->Fail("present", std::string("frame submission failed: ") +
			impl_->compiler.LastError(), impl_->platform.DeviceLost());
	if (!impl_->pending_readbacks.empty())
	{
		if (!impl_->frames.WaitTimeline(submission.timeline_value, UINT64_MAX))
			return impl_->Fail("present-readback", "timeline wait failed",
				impl_->platform.DeviceLost());
		++impl_->synchronous_readback_waits;
		for (const Impl::PendingReadback &pending : impl_->pending_readbacks)
		{
			const CompiledReadback *matching = nullptr;
			for (const CompiledReadback &readback : submission.readbacks)
				if (readback.request == pending.completion.request)
				{
					matching = &readback;
					break;
				}
			if (!matching || !impl_->CompleteBytes(*matching,
				pending.completion, pending.png_path))
				return impl_->Fail("present-readback",
					"deferred screenshot completion failed", true);
		}
		impl_->pending_readbacks.clear();
	}
	impl_->CaptureLatestEpochs(capture);
	impl_->targets.StampUse(submission.timeline_value);
	if (capture.GetLifecycle() == RenderCaptureSegment::Lifecycle::Frozen &&
		!capture.MarkCompiled())
		return impl_->Fail("present", "capture lifecycle publication failed", true);
	if (submission.presentation.accepted_presentation)
	{
		const CapturedPostDynamicState *dynamic =
			capture.TargetSignatures().empty() ? nullptr :
			&capture.TargetSignatures().back().dynamic;
		if (submission.gtao_active)
		{
			if (dynamic && dynamic->histories_frozen)
				impl_->targets.ResetGtaoJitter();
			else
				impl_->targets.AdvanceGtaoJitter();
		}
		const bool gtao_temporal = submission.gtao_active != 0 &&
			(impl_->preferred.gtao_temporal_blend > 0.0f ||
			 impl_->preferred.gtao_temporal_debug_preview != 0);
		if (gtao_temporal)
			impl_->targets.AdvanceGtaoHistory();
		impl_->targets.SetHistoryValidity(gtao_temporal,
			true, true);
		++impl_->presented_submissions;
	}
	const uint64_t completed = impl_->frames.PollCompletedTimeline();
	impl_->allocator.Reclaim(completed);
	impl_->textures.Collect(completed);
	impl_->wsi.CollectRetired(completed, true);
	if (impl_->platform.DeviceLost() ||
		submission.presentation.status == WsiStatus::DeviceLost)
		return impl_->Fail("present", "Vulkan device was lost", true);
	if (submission.presentation.status == WsiStatus::SurfaceLost &&
		!impl_->RecoverSurface())
		return false;
	return impl_->ResetFreshCapture(presented_frame_serial + 1u);
}

bool VulkanRuntime::DiscardFailedPresentedFrame(uint32_t presented_frame_serial)
{
	if (!impl_ || !impl_->initialized || impl_->fatal ||
		impl_->platform.DeviceLost() || presented_frame_serial == UINT32_MAX ||
		impl_->Capture().PresentedFrameSerial() != presented_frame_serial)
		return false;
	RenderCaptureSegment &failed = impl_->Capture();
	if (failed.GetLifecycle() == RenderCaptureSegment::Lifecycle::Capturing)
		return false; // Nothing was submitted/frozen, so do not hide caller misuse.
	impl_->textures.DiscardCapture(failed);
	impl_->pending_readbacks.clear();
	impl_->targets.InvalidateHistories();
	return impl_->ResetFreshCapture(presented_frame_serial + 1u);
}

bool VulkanRuntime::ResolvePipeline(const char *name, uint32_t *pipeline)
{
	if (!impl_ || !impl_->initialized || impl_->fatal || !name || !name[0] ||
		!pipeline)
		return impl_ && impl_->Fail("pipeline", "invalid pipeline lookup");
	for (uint32_t i = 0; i < sizeof(kNamedPipelines) /
		sizeof(kNamedPipelines[0]); ++i)
		if (std::strcmp(name, kNamedPipelines[i]) == 0)
		{
			*pipeline = i + 1;
			return true;
		}
	*pipeline = 0;
	return impl_->Fail("pipeline", std::string("unknown pipeline name: ") + name);
}

bool VulkanRuntime::SelectPipeline(uint32_t pipeline)
{
	if (!impl_ || !impl_->initialized || impl_->fatal ||
		pipeline > sizeof(kNamedPipelines) / sizeof(kNamedPipelines[0]))
		return impl_ && impl_->Fail("pipeline", "pipeline handle is unavailable");
	impl_->selected_pipeline = pipeline;
	return true;
}

bool VulkanRuntime::QueryMotionVectorSample(const float current_world[3],
	const float previous_world[3], float result_uv_velocity[4]) const
{
	if (!impl_ || !impl_->initialized || impl_->fatal || !current_world ||
		!previous_world || !result_uv_velocity ||
		impl_->Capture().Views().empty())
		return false;
	const CapturedWorldView &view = impl_->Capture().Views().back();
	float current_x = 0.0f;
	float current_y = 0.0f;
	float current_w = 0.0f;
	float previous_x = 0.0f;
	float previous_y = 0.0f;
	float previous_w = 0.0f;
	if (!MatrixProject(view.view_projection, current_world, &current_x,
		&current_y, &current_w) ||
		!MatrixProject(view.previous_view_projection, previous_world,
			&previous_x, &previous_y, &previous_w))
		return false;
	const float current_ndc_x = current_x / current_w;
	const float current_ndc_y = current_y / current_w;
	const float previous_ndc_x = previous_x / previous_w;
	const float previous_ndc_y = previous_y / previous_w;
	result_uv_velocity[0] = current_ndc_x * 0.5f + 0.5f;
	result_uv_velocity[1] = current_ndc_y * 0.5f + 0.5f;
	result_uv_velocity[2] = (current_ndc_x - previous_ndc_x) * 0.5f;
	result_uv_velocity[3] = (current_ndc_y - previous_ndc_y) * 0.5f;
	return true;
}

int VulkanRuntime::VideoMemoryPressure() const
{
	if (!impl_ || !impl_->initialized || impl_->fatal)
		return 0;
	const HeapBudgetSnapshot budgets = impl_->allocator.QueryBudgets();
	uint64_t total_budget = 0;
	uint64_t total_usage = 0;
	const VkPhysicalDeviceMemoryProperties &memory =
		impl_->platform.SelectedDevice().memory_properties;
	for (uint32_t i = 0; i < budgets.heap_count && i < memory.memoryHeapCount;
		++i)
		if ((memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0)
		{
			total_budget += budgets.budget_bytes[i];
			total_usage += budgets.usage_bytes[i];
		}
	if (!total_budget)
		return 0;
	const double ratio = static_cast<double>(total_usage) /
		static_cast<double>(total_budget);
	return ratio >= 0.90 ? 2 : ratio >= 0.75 ? 1 : 0;
}

double VulkanRuntime::DisplayRefreshRate() const
{
	if (!impl_ || !impl_->initialized || !impl_->native_window)
		return 0.0;
#if defined(SDL3)
	SDL_Window *window = static_cast<SDL_Window *>(impl_->native_window);
	const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
	const SDL_DisplayMode *mode = display ?
		SDL_GetCurrentDisplayMode(display) : nullptr;
	return mode && mode->refresh_rate > 1.0f ? mode->refresh_rate : 0.0;
#elif defined(WIN32)
	HWND window = static_cast<HWND>(impl_->native_window);
	HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
	MONITORINFOEX monitor_info = {};
	monitor_info.cbSize = sizeof(monitor_info);
	DEVMODE device_mode = {};
	device_mode.dmSize = sizeof(device_mode);
	if (monitor && GetMonitorInfo(monitor, &monitor_info) &&
		EnumDisplaySettings(monitor_info.szDevice, ENUM_CURRENT_SETTINGS,
			&device_mode) && device_mode.dmDisplayFrequency > 1)
		return static_cast<double>(device_mode.dmDisplayFrequency);
	return 0.0;
#else
	return 0.0;
#endif
}

void VulkanRuntime::ReportFailure(RuntimeFailure failure,
	const char *operation)
{
	if (!impl_)
		return;
	std::ostringstream stream;
	stream << "renderer facade failure " << FailureName(failure) << " at "
		<< (operation ? operation : "unknown operation");
	const std::string detail = impl_->DiagnosticCopy();
	// Do not recursively quote a prior facade failure. Repeated compatibility
	// errors used to grow the diagnostic without bound and obscure the first
	// concrete Vulkan failure that caused capture publication to stop.
	const char facade_prefix[] = "renderer facade failure ";
	const bool concrete_detail = !detail.empty() &&
		detail.compare(0, sizeof(facade_prefix) - 1, facade_prefix) != 0;
	if (concrete_detail)
		stream << "; Vulkan detail: " << detail;
	const std::string message = stream.str();
	impl_->SetDiagnostic(message);
	AppendAutomationDiagnostic(std::string("Vulkan failure: ") + message);
	if (concrete_detail)
		mprintf((0, "Vulkan failure [facade]: %s\n", message.c_str()));
}

const char *VulkanRuntime::LastDiagnostic() const noexcept
{
	if (!impl_)
		return "Vulkan runtime is unavailable";
	static thread_local std::string copy;
	copy = impl_->DiagnosticCopy();
	return copy.c_str();
}

bool VulkanRuntime::DeviceLost() const noexcept
{
	return impl_ && impl_->platform.DeviceLost();
}

IVulkanRuntime *VulkanRuntimeSingleton()
{
	static VulkanRuntime runtime;
	return &runtime;
}

const char *VulkanRuntimeLastDiagnostic()
{
	VulkanRuntime *runtime = static_cast<VulkanRuntime *>(
		VulkanRuntimeSingleton());
	return runtime->LastDiagnostic();
}

} // namespace vk
} // namespace render
} // namespace piccu
