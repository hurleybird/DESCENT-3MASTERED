/* Immutable Vulkan shader, descriptor, sampler, and graphics-pipeline library. */
#pragma once

#include "vk_frame.h"
#include "vk_targets.h"
#include "../core/render_device_contract.h"
#include "../core/retained_world.h"

#include <stdint.h>
#include <string>
#include <vector>

namespace piccu
{
namespace render
{
namespace vk
{

enum class WorldPipelineFamily : uint32_t
{
	Stream = 0,
	RetainedWorld,
	Ui,
	Font,
	ExpandedLine,
	ExpandedPoint,
	Particle,
	Count,
};

// Shader-expanded analytic primitives. The compiler packs these records into
// set 2/binding 6, stores a word-relative base in GpuDrawHeader::
// vertex_payload_offset, and emits one six-vertex triangle-list instance per
// record. firstVertex and firstInstance are both zero.
struct alignas(16) ExpandedLineInstance
{
	BaseVertex endpoints[2];
	float half_width_pixels = 0.5f;
	uint32_t reserved[3] = {};
};

struct alignas(16) ExpandedPointInstance
{
	BaseVertex point;
	float half_size_pixels = 0.5f;
	uint32_t reserved[3] = {};
};

constexpr uint32_t kExpandedPrimitiveVertexCount = 6;
constexpr uint32_t kExpandedLineInstanceWords =
	static_cast<uint32_t>(sizeof(ExpandedLineInstance) / sizeof(uint32_t));
constexpr uint32_t kExpandedPointInstanceWords =
	static_cast<uint32_t>(sizeof(ExpandedPointInstance) / sizeof(uint32_t));
static_assert(sizeof(ExpandedLineInstance) == 80,
	"expanded-line shader ABI changed");
static_assert(sizeof(ExpandedPointInstance) == 48,
	"expanded-point shader ABI changed");

struct WorldTargetFormats
{
	VkFormat color[5] = {
		VK_FORMAT_R8G8B8A8_UNORM,
		VK_FORMAT_R16G16_SFLOAT,
		VK_FORMAT_R8G8_UNORM,
		VK_FORMAT_R8_UNORM,
		VK_FORMAT_R32_UINT,
	};
	VkFormat depth = VK_FORMAT_D32_SFLOAT;
};

// This is the complete world-pipeline identity. FindWorldPipeline never
// compiles: a key must have been included in PipelineLibraryCreateInfo.
struct WorldPipelineKey
{
	WorldPipelineFamily family = WorldPipelineFamily::Stream;
	BlendClass blend = BlendClass::Opaque;
	RasterFamily raster = RasterFamily::Ordinary;
	VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
	DepthInterpretation depth_interpretation = DepthInterpretation::EyeZLegacyMapped;
	uint32_t mrt_write_mask = kWriteColor;
	uint32_t cull_enabled = 0;
	// Positive-height upper-left Vulkan viewports invert GL4's winding.
	VkFrontFace front_face = VK_FRONT_FACE_CLOCKWISE;
	uint32_t depth_test_enabled = 0;
	uint32_t depth_write_enabled = 0;
	VkCompareOp depth_compare = VK_COMPARE_OP_LESS_OR_EQUAL;
	uint32_t depth_bias_enabled = 0;
	WorldTargetFormats formats;
};

bool operator==(const WorldPipelineKey &left,
	const WorldPipelineKey &right) noexcept;

struct PipelineLibraryCreateInfo
{
	Platform *platform = nullptr;
	TargetManager *targets = nullptr;
	FrameScheduler *frames = nullptr;
	// CFILE names are used verbatim, and therefore work from loose files or a
	// mounted HOG. The default matches packaged renderer assets.
	const char *shader_asset_prefix = "vulkan/shaders/generated/";
	const char *pipeline_cache_path = "piccu-vulkan-pipeline.cache";
	uint32_t descriptor_page_tier = 0; // zero selects the device tier.
	VkSampleCountFlagBits target_samples = VK_SAMPLE_COUNT_1_BIT;
	WorldTargetFormats world_formats;
	std::vector<WorldPipelineKey> world_pipeline_keys;
	uint32_t precreate_default_world_matrix = 1;
	uint32_t enable_pipeline_cache_write = 1;
};

struct WorldDescriptorSets
{
	VkDescriptorSet set0 = VK_NULL_HANDLE;
	VkDescriptorSet set1 = VK_NULL_HANDLE;
	VkDescriptorSet set2 = VK_NULL_HANDLE;
	uint32_t image_page_tier = 0;

	bool Valid() const noexcept
	{
		return set0 != VK_NULL_HANDLE && set1 != VK_NULL_HANDLE &&
			set2 != VK_NULL_HANDLE;
	}
};

struct WorldSet0Write
{
	VkBuffer frame_view_buffer = VK_NULL_HANDLE;
	VkDeviceSize offset = 0;
	VkDeviceSize range = sizeof(FrameViewGlobals);
};

struct WorldSet1Write
{
	const VkImageView *float_images_2d = nullptr;
	const VkSampler *float_image_samplers = nullptr;
	uint32_t float_image_count = 0;
	const VkImageView *float_image_arrays = nullptr;
	const VkSampler *float_image_array_samplers = nullptr;
	uint32_t float_image_array_count = 0;
	VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
};

struct WorldSet2Write
{
	VkBuffer buffers[8] = {};
	VkDeviceSize offsets[8] = {};
	VkDeviceSize ranges[8] = {};
};

struct PostImageWrite
{
	uint32_t binding = 0;
	PostDescriptorKind kind = PostDescriptorKind::SampledFloat2D;
	VkImageView view = VK_NULL_HANDLE;
	VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
};

struct PostDescriptorWrite
{
	VkBuffer uniform_buffer = VK_NULL_HANDLE;
	VkDeviceSize uniform_offset = 0;
	VkDeviceSize uniform_range = sizeof(PostPassUniforms);
	const PostImageWrite *images = nullptr;
	uint32_t image_count = 0;
};

enum class TerrainComputeStage : uint32_t
{
	Classify = 0,
	Scan,
	Emit,
	Count,
};

enum TerrainDescriptorBinding : uint32_t
{
	kTerrainCellsBinding = 0,
	kTerrainWorkBinding = 1,
	kTerrainBatchesBinding = 2,
	kTerrainViewBinding = 3,
	kTerrainScratchBinding = 4,
	kTerrainBaseOutputBinding = 5,
	kTerrainPayloadOutputBinding = 6,
	kTerrainIndirectOutputBinding = 7,
	kTerrainDescriptorBindingCount = 8,
};

struct alignas(16) TerrainEmitterPush
{
	uint32_t work_item_count = 0;
	uint32_t batch_count = 0;
	uint32_t output_vertex_capacity = 0;
	uint32_t flags = 0;
};

struct TerrainBufferBinding
{
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceSize offset = 0;
	VkDeviceSize range = 0;
};

struct TerrainEmitterDescriptorWrite
{
	// Every offset must satisfy minStorageBufferOffsetAlignment and every range
	// must fit maxStorageBufferRange. Cells must cover every cell_index reachable
	// from the work range; the push ABI deliberately does not carry cell_count.
	TerrainBufferBinding bindings[kTerrainDescriptorBindingCount];
};

struct TerrainEmitterDispatchPlan
{
	uint32_t classify_groups = 0;
	uint32_t scan_groups = 0;
	uint32_t emit_groups = 0;
	uint32_t local_size = kTerrainEmitterContract.classify_group_size;
};

static_assert(sizeof(TerrainEmitterPush) == 16,
	"terrain emitter push ABI changed");
static_assert(kTerrainEmitterContract.classify_group_size == 256 &&
	kTerrainEmitterContract.scan_group_size == 256 &&
	kTerrainEmitterContract.emit_group_size == 256,
	"terrain emitter shader local-size contract changed");

enum class UtilityPipeline : uint32_t
{
	MotionVelocityCopy = 0,
	Count,
};

class PipelineLibrary final
{
public:
	PipelineLibrary();
	~PipelineLibrary();

	PipelineLibrary(const PipelineLibrary &) = delete;
	PipelineLibrary &operator=(const PipelineLibrary &) = delete;

	bool Initialize(const PipelineLibraryCreateInfo &create_info);
	void Shutdown(bool device_lost = false) noexcept;
	bool Ready() const noexcept;
	const std::string &LastError() const noexcept;

	uint32_t DescriptorPageTier() const noexcept;
	VkSampler Sampler(SamplerSemantic semantic) const noexcept;
	VkDescriptorSetLayout WorldSetLayout(uint32_t set) const noexcept;
	VkPipelineLayout WorldPipelineLayout() const noexcept;
	VkDescriptorSetLayout PostSetLayout(GraphNodeId node,
		PostPassVariant variant) const noexcept;
	VkPipelineLayout PostPipelineLayout(GraphNodeId node,
		PostPassVariant variant) const noexcept;

	// Creates any missing exact-key pipelines.  The frame compiler calls this
	// after planning and before command-buffer recording begins; creation during
	// recording is forbidden.
	bool EnsureWorldPipelines(const std::vector<WorldPipelineKey> &keys);
	VkPipeline FindWorldPipeline(const WorldPipelineKey &key) const noexcept;
	// Present has one pipeline per supported UNORM swapchain format.  The
	// attachment-only alpha clear additionally keys the target sample count;
	// all other nodes use the frozen format and single-sample contract.
	VkPipeline FindPostPipeline(GraphNodeId node, PostPassVariant variant,
		VkFormat color_format = VK_FORMAT_UNDEFINED,
		VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT) const noexcept;
	VkPipeline FindUtilityPipeline(UtilityPipeline pipeline) const noexcept;
	VkDescriptorSetLayout TerrainDescriptorSetLayout() const noexcept;
	VkPipelineLayout TerrainPipelineLayout() const noexcept;
	VkPipeline TerrainPipeline(TerrainComputeStage stage) const noexcept;
	bool PlanTerrainEmitter(const TerrainEmitterPush &push,
		TerrainEmitterDispatchPlan *plan) const noexcept;
	bool AllocateTerrainDescriptorSet(VkDescriptorPool pool,
		VkDescriptorSet *output) const;
	bool AllocateTerrainDescriptorSet(const FrameContext &frame,
		VkDescriptorSet *output) const;
	bool UpdateTerrainDescriptorSet(VkDescriptorSet set,
		const TerrainEmitterDescriptorWrite &write,
		const TerrainEmitterPush &push) const;
	// Records classify -> barrier -> stable per-batch scan -> barrier -> emit,
	// followed by compute-write visibility for vertex/storage/indirect reads.
	bool RecordTerrainEmitter(VkCommandBuffer command, VkDescriptorSet set,
		const TerrainEmitterPush &push) const;

	bool AllocateWorldDescriptorSets(VkDescriptorPool pool,
		WorldDescriptorSets *output) const;
	bool AllocateWorldDescriptorSets(const FrameContext &frame,
		WorldDescriptorSets *output) const;
	bool UpdateWorldSet0(VkDescriptorSet set, const WorldSet0Write &write) const;
	bool UpdateWorldSet1(VkDescriptorSet set, const WorldSet1Write &write) const;
	bool UpdateWorldSet2(VkDescriptorSet set, const WorldSet2Write &write) const;
	bool AllocatePostDescriptorSet(VkDescriptorPool pool, GraphNodeId node,
		PostPassVariant variant, VkDescriptorSet *output) const;
	bool AllocatePostDescriptorSet(const FrameContext &frame, GraphNodeId node,
		PostPassVariant variant, VkDescriptorSet *output) const;
	// Validates binding presence and numeric/MSAA/depth class against the frozen
	// descriptor contract before issuing vkUpdateDescriptorSets.
	bool UpdatePostDescriptorSet(VkDescriptorSet set, GraphNodeId node,
		PostPassVariant variant, const PostDescriptorWrite &write) const;

	uint32_t WorldPipelineCount() const noexcept;
	uint32_t PostPipelineCount() const noexcept;
	uint32_t LoadedShaderCount() const noexcept;

private:
	struct Impl;
	Impl *impl_;
};

} // namespace vk
} // namespace render
} // namespace piccu
