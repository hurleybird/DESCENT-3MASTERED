/*
 * Backend-neutral seam between the legacy IRenderer facade and the Vulkan
 * platform/device/compiler runtime.  No Vulkan type is permitted here.
 */
#pragma once

#include "../core/render_capture.h"
#include "../core/retained_world.h"

#include <stddef.h>
#include <stdint.h>

class oeApplication;

namespace piccu
{
namespace render
{
namespace vk
{

enum class RuntimeFailure : uint32_t
{
	None = 0,
	NotInitialized,
	MissingRuntime,
	InvalidArgument,
	InvalidLifecycle,
	CaptureUnavailable,
	CaptureRejected,
	TargetDescriptionFailed,
	PresentationDescriptionFailed,
	TextureResolutionFailed,
	TextureEventFailed,
	ReadbackFailed,
	PresentationFailed,
	PipelineUnavailable,
	UnsupportedFeature,
	ResourceExhausted,
};

enum class TextureRole : uint32_t
{
	Base2D = 0,
	Lightmap2D,
	Specular2D,
	Auxiliary2D,
	FontArray,
	TerrainBaseArray,
	TerrainLightmapArray,
	ReservedArray,
};

struct TextureRequest
{
	int32_t logical_handle; // -1 requests the role's diagnostic fallback.
	uint32_t map_type;
	TextureRole role;
	uint32_t wrap_type;
	uint32_t filtering;
	uint32_t mipping;
};

struct ResolvedTexture
{
	CapturedTextureVersion version;
	uint32_t array_layer;
	uint32_t font_bucket;
	uint32_t sampler_index;
	float uv_scale[2];
};

struct TerrainArrayRequest
{
	const int32_t *base_bitmap_handles;
	uint32_t base_bitmap_count;
	int32_t lightmap_handles[4];
	uint32_t filtering;
	uint32_t mipping;
};

enum class TextureEvent : uint32_t
{
	PreUpload = 0,
	ReleasePreUpload,
	ResetCache,
};

struct TargetRequest
{
	RenderTargetClass target;
	LogicalRect logical_clip;
	uint32_t clear_flags;
	CapturedPreferredState preferred;
};

struct TargetDescription
{
	CapturedTargetLayout layout;
	CapturedTargetVersion version;
	CapturedViewport viewport;
	CapturedWorldView view;
};

struct PresentationDescription
{
	CapturedTargetLayout scene_layout;
	CapturedTargetLayout post_present_layout;
	CapturedTargetLayout cockpit_scene_layout;
	CapturedPostDynamicState dynamic;
	CapturedPresentRect present_rect;
	CapturedWsiSignature wsi;
	uint32_t defer_bloom;
};

enum class ReadbackDestination : uint32_t
{
	CpuColor = 0,
	BitmapHandle,
	PngPath,
};

struct ReadbackCompletion
{
	ReadbackRequestId request;
	ReadbackDestination destination;
	void *cpu_bytes;
	uint32_t cpu_byte_size;
	int32_t bitmap_handle;
	const char *png_path;
};

// Implemented by the platform/device/compiler package.  The facade owns no
// API objects and never records commands directly to a graphics API.
class IVulkanRuntime
{
public:
	virtual ~IVulkanRuntime() = default;

	virtual bool Initialize(oeApplication *app,
		const CapturedPreferredState &preferred) = 0;
	virtual void Shutdown() = 0;
	virtual bool ApplyPreferredState(const CapturedPreferredState &preferred) = 0;
	// The returned segment is already Reset for the runtime-selected presented
	// frame serial. After a synchronous readback it is a continuation with the
	// inherited target layout/version/view IDs installed at the same indices.
	virtual RenderCaptureSegment *ActiveCapture() = 0;
	// Backend-neutral retained-world ownership remains in the runtime so mesh
	// lifetime can be tied to compiler timeline submissions.
	virtual IRetainedWorld *RetainedWorldBridge() = 0;

	virtual bool DescribeTarget(const TargetRequest &request,
		TargetDescription *description) = 0;
	virtual bool DescribePresentation(const CapturedPreferredState &preferred,
		PresentationDescription *description) = 0;

	virtual bool ResolveTexture(const TextureRequest &request,
		ResolvedTexture *resolved) = 0;
	virtual bool ResolveTerrainTextureArrays(const TerrainArrayRequest &request,
		ResolvedTexture *base, ResolvedTexture *lightmap) = 0;
	virtual bool NotifyTextureEvent(TextureEvent event, int32_t logical_handle,
		uint32_t map_type) = 0;

	// The matching ReadPixel/ReadImage command has already been appended.
	// ReadPixel compiles/submits the synchronous prefix and resumes capture as a
	// continuation. ReadImage may instead register a deferred completion when
	// the destination API permits it; deferred implementations must deep-copy
	// png_path before this call returns.
	virtual bool CompleteReadback(const ReadbackCompletion &completion) = 0;
	// On success ActiveCapture names the newly Reset next presented frame.
	virtual bool SubmitPresentedFrame(uint32_t presented_frame_serial) = 0;
	// A non-terminal compiler/WSI rejection may leave the submitted capture
	// frozen. Discard it explicitly so the legacy caller can continue with a
	// fresh presented-frame interval instead of cascading lifecycle failures.
	virtual bool DiscardFailedPresentedFrame(uint32_t presented_frame_serial) = 0;

	virtual bool ResolvePipeline(const char *name, uint32_t *pipeline) = 0;
	// Pipeline 0 is the normalized legacy world pipeline selected by
	// RestoreLegacy; named/custom pipelines use nonzero stable handles.
	virtual bool SelectPipeline(uint32_t pipeline) = 0;
	virtual bool QueryMotionVectorSample(const float current_world[3],
		const float previous_world[3], float result_uv_velocity[4]) const = 0;

	virtual int VideoMemoryPressure() const = 0;
	virtual double DisplayRefreshRate() const = 0;
	virtual void ReportFailure(RuntimeFailure failure, const char *operation) = 0;
};

} // namespace vk
} // namespace render
} // namespace piccu
