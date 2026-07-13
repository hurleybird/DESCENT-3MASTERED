/* PiccuEngine immutable Vulkan pipeline library. */
#include "vk_pipelines.h"

#include "CFILE.H"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>
#include <utility>

namespace piccu
{
namespace render
{
namespace vk
{
namespace
{

constexpr uint32_t kCacheSchema = 1;
constexpr uint64_t kFnvOffset = UINT64_C(1469598103934665603);
constexpr uint64_t kFnvPrime = UINT64_C(1099511628211);

struct CacheFileHeader
{
	char magic[8];
	uint32_t schema;
	uint32_t header_size;
	uint32_t vendor_id;
	uint32_t device_id;
	uint32_t driver_version;
	uint32_t api_version;
	uint8_t pipeline_cache_uuid[VK_UUID_SIZE];
	uint64_t shader_fingerprint;
};

struct NodeVariantKey
{
	GraphNodeId node;
	PostPassVariant variant;
	bool operator<(const NodeVariantKey &right) const noexcept
	{
		if (node != right.node)
			return static_cast<uint32_t>(node) < static_cast<uint32_t>(right.node);
		return static_cast<uint32_t>(variant) < static_cast<uint32_t>(right.variant);
	}
};

struct PostPipelineKey
{
	NodeVariantKey descriptor;
	VkFormat color_format;
	VkSampleCountFlagBits samples;
	bool operator<(const PostPipelineKey &right) const noexcept
	{
		if (descriptor < right.descriptor) return true;
		if (right.descriptor < descriptor) return false;
		if (color_format != right.color_format)
			return color_format < right.color_format;
		return samples < right.samples;
	}
};

struct ShaderRecord
{
	std::string name;
	VkShaderModule module = VK_NULL_HANDLE;
};

struct WorldPipelineRecord
{
	WorldPipelineKey key;
	VkPipeline pipeline = VK_NULL_HANDLE;
};

uint64_t HashBytes(uint64_t hash, const void *data, size_t size)
{
	const uint8_t *bytes = static_cast<const uint8_t *>(data);
	for (size_t i = 0; i < size; ++i)
		hash = (hash ^ bytes[i]) * kFnvPrime;
	return hash;
}

template <typename Handle>
uint64_t HandleValue(Handle handle)
{
	static_assert(sizeof(handle) <= sizeof(uint64_t), "Vulkan handle exceeds debug-name ABI");
	uint64_t value = 0;
	std::memcpy(&value, &handle, sizeof(handle));
	return value;
}

VkShaderStageFlags Stages(uint32_t mask)
{
	VkShaderStageFlags stages = 0;
	if (mask & kStageVertex) stages |= VK_SHADER_STAGE_VERTEX_BIT;
	if (mask & kStageFragment) stages |= VK_SHADER_STAGE_FRAGMENT_BIT;
	if (mask & kStageCompute) stages |= VK_SHADER_STAGE_COMPUTE_BIT;
	return stages;
}

VkDescriptorType DescriptorType(DescriptorKind kind)
{
	switch (kind)
	{
	case DescriptorKind::DynamicUniformBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	case DescriptorKind::Sampler: return VK_DESCRIPTOR_TYPE_SAMPLER;
	case DescriptorKind::SampledFloat2D:
	case DescriptorKind::SampledFloat2DArray: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	case DescriptorKind::CombinedFloat2D:
	case DescriptorKind::CombinedFloat2DArray:
		return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	case DescriptorKind::StorageBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	default: return VK_DESCRIPTOR_TYPE_MAX_ENUM;
	}
}

VkDescriptorType DescriptorType(PostDescriptorKind kind)
{
	switch (kind)
	{
	case PostDescriptorKind::UniformBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	case PostDescriptorKind::Sampler: return VK_DESCRIPTOR_TYPE_SAMPLER;
	case PostDescriptorKind::SampledFloat2D:
	case PostDescriptorKind::SampledFloat2DMultisample:
	case PostDescriptorKind::SampledDepth2D:
	case PostDescriptorKind::SampledDepth2DMultisample:
	case PostDescriptorKind::SampledUint2D:
	case PostDescriptorKind::SampledUint2DMultisample:
	case PostDescriptorKind::SampledFloat2DArray: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	default: return VK_DESCRIPTOR_TYPE_MAX_ENUM;
	}
}

VkSamplerAddressMode Address(SamplerAddressMode mode)
{
	return mode == SamplerAddressMode::Repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT :
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
}

VkFilter Filter(SamplerFilterMode mode)
{
	return mode == SamplerFilterMode::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

VkSamplerMipmapMode Mip(SamplerMipMode mode)
{
	return mode == SamplerMipMode::Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR :
		VK_SAMPLER_MIPMAP_MODE_NEAREST;
}

VkBlendFactor BlendFactor(BlendFactorContract factor)
{
	switch (factor)
	{
	case BlendFactorContract::Zero: return VK_BLEND_FACTOR_ZERO;
	case BlendFactorContract::One: return VK_BLEND_FACTOR_ONE;
	case BlendFactorContract::SourceAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
	case BlendFactorContract::OneMinusSourceAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	case BlendFactorContract::DestinationColor: return VK_BLEND_FACTOR_DST_COLOR;
	default: return VK_BLEND_FACTOR_ZERO;
	}
}

const BlendClassContract &BlendContract(BlendClass blend)
{
	for (size_t i = 0; i < kBlendClassContractCount; ++i)
		if (kBlendClassContract[i].blend_class == blend) return kBlendClassContract[i];
	return kBlendClassContract[0];
}

VkColorComponentFlags ColorMask(uint32_t mask)
{
	VkColorComponentFlags result = 0;
	if (mask & kChannelRed) result |= VK_COLOR_COMPONENT_R_BIT;
	if (mask & kChannelGreen) result |= VK_COLOR_COMPONENT_G_BIT;
	if (mask & kChannelBlue) result |= VK_COLOR_COMPONENT_B_BIT;
	if (mask & kChannelAlpha) result |= VK_COLOR_COMPONENT_A_BIT;
	return result;
}

bool ReadCfile(const std::string &name, std::vector<uint8_t> *bytes)
{
	CFILE *file = cfopen(name.c_str(), "rb");
	if (!file) return false;
	const int length = cfilelength(file);
	if (length <= 0 || (length & 3) != 0)
	{
		cfclose(file);
		return false;
	}
	bytes->resize(static_cast<size_t>(length));
	const int read = cf_ReadBytes(reinterpret_cast<ubyte *>(bytes->data()), length, file);
	cfclose(file);
	return read == length;
}

bool ReadNativeFile(const char *path, std::vector<uint8_t> *bytes)
{
	if (!path || !path[0]) return false;
	FILE *file = nullptr;
#if defined(_WIN32)
	fopen_s(&file, path, "rb");
#else
	file = std::fopen(path, "rb");
#endif
	if (!file) return false;
	std::fseek(file, 0, SEEK_END);
	const long length = std::ftell(file);
	std::fseek(file, 0, SEEK_SET);
	if (length <= 0) { std::fclose(file); return false; }
	bytes->resize(static_cast<size_t>(length));
	const bool result = std::fread(bytes->data(), 1, bytes->size(), file) == bytes->size();
	std::fclose(file);
	return result;
}

bool AtomicWriteNativeFile(const char *path, const void *data, size_t size)
{
	if (!path || !path[0]) return false;
	const std::string temporary = std::string(path) + ".tmp";
	FILE *file = nullptr;
#if defined(_WIN32)
	fopen_s(&file, temporary.c_str(), "wb");
#else
	file = std::fopen(temporary.c_str(), "wb");
#endif
	if (!file) return false;
	const bool written = std::fwrite(data, 1, size, file) == size &&
		std::fflush(file) == 0;
	std::fclose(file);
	if (!written) { std::remove(temporary.c_str()); return false; }
#if defined(_WIN32)
	if (!MoveFileExA(temporary.c_str(), path,
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		std::remove(temporary.c_str());
		return false;
	}
	return true;
#else
	if (std::rename(temporary.c_str(), path) != 0)
	{
		std::remove(temporary.c_str());
		return false;
	}
	return true;
#endif
}

const char *PostFragment(GraphNodeId node, PostPassVariant variant)
{
	switch (node)
	{
	case GraphNodeId::CapWorld:
	case GraphNodeId::CockpitResolve:
		if (variant == PostPassVariant::Multisample) return "capture_color_ms.frag.spv";
		if (variant == PostPassVariant::SsaaFourToTwo) return "capture_ssaa_4_to_2.frag.spv";
		if (variant == PostPassVariant::SsaaTwoToOne) return "capture_ssaa_2_to_1.frag.spv";
		return "capture_color.frag.spv";
	case GraphNodeId::CapDepthLogical: return variant == PostPassVariant::Multisample ? "depth_map_ms.frag.spv" : "depth_map.frag.spv";
	case GraphNodeId::ResolveColor:
	case GraphNodeId::ResolveVelocity:
	case GraphNodeId::ResolveProtectionMask:
	case GraphNodeId::ResolveAoClass: return "resolve_float_ms.frag.spv";
	case GraphNodeId::ResolveDepth: return "resolve_depth_ms.frag.spv";
	case GraphNodeId::ResolveObjectId: return "resolve_uint_ms.frag.spv";
	case GraphNodeId::Ssaa4To2:
	case GraphNodeId::Ssaa2To1: return "ssaa_downsample.frag.spv";
	case GraphNodeId::PrepareDepthLogical: return "depth_map.frag.spv";
	case GraphNodeId::AoDepth: return "gtao_depth.frag.spv";
	case GraphNodeId::AoRaw: return "gtao_raw.frag.spv";
	case GraphNodeId::AoBlurX:
	case GraphNodeId::AoBlurY: return "gtao_blur.frag.spv";
	case GraphNodeId::AoTemporal: return "gtao_temporal.frag.spv";
	case GraphNodeId::AoSuppress: return "gtao_suppress.frag.spv";
	case GraphNodeId::AoApply: return "gtao_apply.frag.spv";
	case GraphNodeId::AoDeferredComposite: return "gtao_deferred.frag.spv";
	case GraphNodeId::BloomThreshold: return "bloom_threshold.frag.spv";
	case GraphNodeId::BloomDown: return "bloom_down.frag.spv";
	case GraphNodeId::BloomUp: return "bloom_up.frag.spv";
	case GraphNodeId::NormalComposite: return "bloom_composite.frag.spv";
	case GraphNodeId::NormalBlit:
	case GraphNodeId::CockpitLinearCopy:
	case GraphNodeId::CockpitGammaOnly: return "post_blit.frag.spv";
	case GraphNodeId::MotionNormal:
	case GraphNodeId::MotionCockpitPre: return "motion_blur.frag.spv";
	case GraphNodeId::MotionDebugNormal:
	case GraphNodeId::MotionDebugCockpitPre: return "motion_debug.frag.spv";
	case GraphNodeId::PostAlphaClear: return "alpha_clear.frag.spv";
	case GraphNodeId::BloomDeferred:
		if (variant == PostPassVariant::BloomThresholdPhase) return "bloom_threshold.frag.spv";
		if (variant == PostPassVariant::BloomDownsamplePhase) return "bloom_down.frag.spv";
		return "bloom_up.frag.spv";
	case GraphNodeId::CockpitOver: return "cockpit_over.frag.spv";
	case GraphNodeId::CockpitBloomGamma: return "cockpit_bloom_gamma.frag.spv";
	case GraphNodeId::Present: return "present.frag.spv";
	default: return nullptr;
	}
}

bool DepthOnlyNode(GraphNodeId node)
{
	return node == GraphNodeId::CapDepthLogical || node == GraphNodeId::ResolveDepth ||
		node == GraphNodeId::PrepareDepthLogical;
}

VkFormat PostFormat(GraphNodeId node)
{
	switch (node)
	{
	case GraphNodeId::ResolveVelocity: return VK_FORMAT_R16G16_SFLOAT;
	case GraphNodeId::ResolveObjectId: return VK_FORMAT_R32_UINT;
	case GraphNodeId::ResolveProtectionMask: return VK_FORMAT_R8G8_UNORM;
	case GraphNodeId::ResolveAoClass:
	case GraphNodeId::AoSuppress: return VK_FORMAT_R8_UNORM;
	case GraphNodeId::AoDepth: return VK_FORMAT_R32G32_SFLOAT;
	case GraphNodeId::AoRaw:
	case GraphNodeId::AoBlurX:
	case GraphNodeId::AoBlurY: return VK_FORMAT_R16G16_SFLOAT;
	case GraphNodeId::AoTemporal: return VK_FORMAT_R16G16B16A16_SFLOAT;
	default: return VK_FORMAT_R8G8B8A8_UNORM;
	}
}

bool IsWorldUse(PostPassDescriptorUse use)
{
	return use == PostPassDescriptorUse::WorldDescriptorAbi;
}

std::string ObjectName(const char *prefix, uint32_t a, uint32_t b)
{
	std::ostringstream stream;
	stream << prefix << "." << a << "." << b;
	return stream.str();
}

} // namespace

struct PipelineLibrary::Impl
{
	Platform *platform = nullptr;
	TargetManager *targets = nullptr;
	FrameScheduler *frames = nullptr;
	VkDevice device = VK_NULL_HANDLE;
	uint32_t page_tier = 0;
	VkSampleCountFlagBits target_samples = VK_SAMPLE_COUNT_1_BIT;
	std::string asset_prefix;
	std::string cache_path;
	std::string error;
	bool cache_write = false;
	bool ready = false;
	uint64_t shader_fingerprint = kFnvOffset;
	VkPipelineCache cache = VK_NULL_HANDLE;
	VkSampler semantic_samplers[static_cast<uint32_t>(SamplerSemantic::Count)] = {};
	VkSampler world_sampler_table[kWorldSamplerCount] = {};
	VkSampler post_sampler_table[kPostSamplerTableSize] = {};
	VkDescriptorSetLayout world_sets[3] = {};
	VkPipelineLayout world_layout = VK_NULL_HANDLE;
	VkDescriptorSetLayout terrain_set = VK_NULL_HANDLE;
	VkPipelineLayout terrain_layout = VK_NULL_HANDLE;
	VkPipeline terrain_pipelines[static_cast<uint32_t>(TerrainComputeStage::Count)] = {};
	std::map<NodeVariantKey, VkDescriptorSetLayout> post_sets;
	std::map<NodeVariantKey, VkPipelineLayout> post_layouts;
	std::vector<ShaderRecord> shaders;
	std::vector<WorldPipelineRecord> world_pipelines;
	std::map<PostPipelineKey, VkPipeline> post_pipelines;
	VkPipeline utility[static_cast<uint32_t>(UtilityPipeline::Count)] = {};

	void Fail(const std::string &message) { if (error.empty()) error = message; }

	void Name(VkObjectType type, uint64_t handle, const char *name)
	{
		if (!handle || !name || !vkSetDebugUtilsObjectNameEXT) return;
		VkDebugUtilsObjectNameInfoEXT info = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
		info.objectType = type; info.objectHandle = handle; info.pObjectName = name;
		vkSetDebugUtilsObjectNameEXT(device, &info);
	}

	ShaderRecord *Shader(const char *name)
	{
		for (size_t i = 0; i < shaders.size(); ++i)
			if (shaders[i].name == name) return &shaders[i];
		return nullptr;
	}

	bool LoadShader(const char *name)
	{
		if (Shader(name)) return true;
		std::vector<uint8_t> bytes;
		std::string path = asset_prefix + name;
		if (!ReadCfile(path, &bytes))
		{
			path = std::string("renderer/vulkan/shaders/generated/") + name;
			if (!ReadCfile(path, &bytes) && !ReadCfile(name, &bytes))
			{
				Fail(std::string("cannot read SPIR-V asset through CFILE: ") + name);
				return false;
			}
		}
		if (bytes.size() < 20 || *reinterpret_cast<const uint32_t *>(bytes.data()) != 0x07230203u)
		{
			Fail(std::string("invalid SPIR-V header: ") + name); return false;
		}
		VkShaderModuleCreateInfo create = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
		create.codeSize = bytes.size(); create.pCode = reinterpret_cast<const uint32_t *>(bytes.data());
		ShaderRecord record; record.name = name;
		if (vkCreateShaderModule(device, &create, nullptr, &record.module) != VK_SUCCESS)
		{
			Fail(std::string("vkCreateShaderModule failed: ") + name); return false;
		}
		shader_fingerprint = HashBytes(shader_fingerprint, name, std::strlen(name));
		shader_fingerprint = HashBytes(shader_fingerprint, bytes.data(), bytes.size());
		shaders.push_back(record);
		Name(VK_OBJECT_TYPE_SHADER_MODULE, HandleValue(record.module), name);
		return true;
	}

	bool LoadShaders()
	{
		static const char *names[] = {
			"world.vert.spv", "world.frag.spv", "ui.vert.spv", "ui.frag.spv",
			"font.vert.spv", "font.frag.spv", "particle.vert.spv", "particle.frag.spv",
			"line.vert.spv", "line.frag.spv", "point.vert.spv", "point.frag.spv",
			"terrain_classify.comp.spv", "terrain_scan.comp.spv", "terrain_emit.comp.spv",
			"fullscreen.vert.spv", "alpha_clear.frag.spv", "bloom_composite.frag.spv",
			"bloom_down.frag.spv", "bloom_threshold.frag.spv", "bloom_up.frag.spv",
			"capture_color.frag.spv", "capture_color_ms.frag.spv",
			"capture_ssaa_2_to_1.frag.spv", "capture_ssaa_4_to_2.frag.spv",
			"cockpit_bloom_gamma.frag.spv", "cockpit_over.frag.spv",
			"depth_map.frag.spv", "depth_map_ms.frag.spv", "gtao_apply.frag.spv",
			"gtao_blur.frag.spv", "gtao_deferred.frag.spv", "gtao_depth.frag.spv",
			"gtao_raw.frag.spv", "gtao_suppress.frag.spv", "gtao_temporal.frag.spv",
			"motion_blur.frag.spv", "motion_copy.frag.spv", "motion_debug.frag.spv",
			"post_blit.frag.spv", "present.frag.spv", "resolve_depth_ms.frag.spv",
			"resolve_float_ms.frag.spv", "resolve_uint_ms.frag.spv", "ssaa_downsample.frag.spv"
		};
		for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
			if (!LoadShader(names[i])) return false;
		return true;
	}

	bool CreateSamplers()
	{
		if (kSamplerContractCount != static_cast<size_t>(SamplerSemantic::Count))
		{
			Fail("sampler contract does not cover SamplerSemantic"); return false;
		}
		for (size_t i = 0; i < kSamplerContractCount; ++i)
		{
			const SamplerContract &contract = kSamplerContract[i];
			const uint32_t semantic = static_cast<uint32_t>(contract.semantic);
			VkSamplerCreateInfo create = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
			create.magFilter = Filter(contract.magnification);
			create.minFilter = Filter(contract.minification);
			create.mipmapMode = Mip(contract.mip);
			create.addressModeU = Address(contract.address_u);
			create.addressModeV = Address(contract.address_v);
			create.addressModeW = create.addressModeV;
			create.anisotropyEnable = contract.anisotropy_enabled ? VK_TRUE : VK_FALSE;
			create.maxAnisotropy = contract.anisotropy_enabled ? 1.0f : 1.0f;
			create.minLod = 0.0f;
			create.maxLod = contract.mip == SamplerMipMode::Disabled ? 0.0f : VK_LOD_CLAMP_NONE;
			if (vkCreateSampler(device, &create, nullptr, &semantic_samplers[semantic]) != VK_SUCCESS)
			{
				Fail("vkCreateSampler failed"); return false;
			}
			const std::string name = ObjectName("vk.sampler", semantic, 0);
			Name(VK_OBJECT_TYPE_SAMPLER, HandleValue(semantic_samplers[semantic]), name.c_str());
		}
		// This immutable table is indexed by the frozen world-sampler ABI, not
		// directly by SamplerSemantic.  Slots 16..27 duplicate the generic
		// samplers for array textures; terrain lightmaps use forced-repeat 13.
		for (uint32_t i = 0; i < 12; ++i)
			world_sampler_table[i] = semantic_samplers[i];
		world_sampler_table[12] = semantic_samplers[
			static_cast<uint32_t>(SamplerSemantic::LightmapClamp)];
		world_sampler_table[13] = semantic_samplers[
			static_cast<uint32_t>(SamplerSemantic::LightmapRepeat)];
		world_sampler_table[14] = semantic_samplers[
			static_cast<uint32_t>(SamplerSemantic::Font)];
		const VkSampler fallback = semantic_samplers[
			static_cast<uint32_t>(SamplerSemantic::GenericClampPointNoMip)];
		world_sampler_table[15] = fallback;
		for (uint32_t i = 0; i < 12; ++i)
			world_sampler_table[16 + i] = semantic_samplers[i];
		for (uint32_t i = 28; i < kWorldSamplerCount; ++i)
			world_sampler_table[i] = fallback;
		for (size_t i = 0; i < kPostSamplerSlotCount; ++i)
			post_sampler_table[kPostSamplerSlots[i].slot] = semantic_samplers[static_cast<uint32_t>(kPostSamplerSlots[i].semantic)];
		return true;
	}

	bool CreateWorldLayouts()
	{
		for (uint32_t set = 0; set < 3; ++set)
		{
			std::vector<VkDescriptorSetLayoutBinding> bindings;
			for (size_t i = 0; i < kWorldDescriptorBindingCount; ++i)
			{
				const WorldDescriptorBindingContract &contract = kWorldDescriptorBindings[i];
				if (contract.set != set) continue;
				VkDescriptorSetLayoutBinding binding = {};
				binding.binding = contract.binding;
				binding.descriptorType = DescriptorType(contract.kind);
				binding.descriptorCount = contract.page_tier_count ? page_tier : contract.count;
				binding.stageFlags = Stages(contract.stage_mask);
				if (set == 0 && contract.binding == kSet0Samplers) binding.pImmutableSamplers = world_sampler_table;
				bindings.push_back(binding);
			}
			VkDescriptorSetLayoutCreateInfo create = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
			create.bindingCount = static_cast<uint32_t>(bindings.size()); create.pBindings = bindings.data();
			if (vkCreateDescriptorSetLayout(device, &create, nullptr, &world_sets[set]) != VK_SUCCESS)
			{
				Fail("world descriptor-set layout creation failed"); return false;
			}
			const std::string name = ObjectName("vk.world.set_layout", set, page_tier);
			Name(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, HandleValue(world_sets[set]), name.c_str());
		}
		VkPushConstantRange push = {};
		push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		push.offset = 0; push.size = sizeof(WorldBatchPush);
		VkPipelineLayoutCreateInfo create = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		create.setLayoutCount = 3; create.pSetLayouts = world_sets;
		create.pushConstantRangeCount = 1; create.pPushConstantRanges = &push;
		if (vkCreatePipelineLayout(device, &create, nullptr, &world_layout) != VK_SUCCESS)
		{
			Fail("world pipeline layout creation failed"); return false;
		}
		Name(VK_OBJECT_TYPE_PIPELINE_LAYOUT, HandleValue(world_layout), "vk.world.pipeline_layout");
		return true;
	}

	bool CreatePostLayouts()
	{
		for (size_t set_index = 0; set_index < kPostPassDescriptorSetCount; ++set_index)
		{
			const PostPassDescriptorSetContract &set_contract = kPostPassDescriptorSets[set_index];
			if (IsWorldUse(set_contract.use)) continue;
			NodeVariantKey key = { set_contract.node, set_contract.variant };
			VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
			if (set_contract.use == PostPassDescriptorUse::ImmutablePostSet)
			{
				std::vector<VkDescriptorSetLayoutBinding> bindings;
				for (size_t i = 0; i < kPostPassDescriptorBindingCount; ++i)
				{
					const PostPassDescriptorBindingContract &contract = kPostPassDescriptorBindings[i];
					if (contract.node != key.node || contract.variant != key.variant) continue;
					VkDescriptorSetLayoutBinding binding = {};
					binding.binding = contract.binding;
					binding.descriptorType = DescriptorType(contract.kind);
					binding.descriptorCount = contract.count;
					binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
					if (contract.kind == PostDescriptorKind::Sampler) binding.pImmutableSamplers = post_sampler_table;
					bindings.push_back(binding);
				}
				VkDescriptorSetLayoutCreateInfo create = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
				create.bindingCount = static_cast<uint32_t>(bindings.size()); create.pBindings = bindings.data();
				if (vkCreateDescriptorSetLayout(device, &create, nullptr, &set_layout) != VK_SUCCESS)
				{
					Fail("post descriptor-set layout creation failed"); return false;
				}
				post_sets[key] = set_layout;
			}
			VkPipelineLayoutCreateInfo layout_create = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
			if (set_layout) { layout_create.setLayoutCount = 1; layout_create.pSetLayouts = &set_layout; }
			VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
			if (vkCreatePipelineLayout(device, &layout_create, nullptr, &pipeline_layout) != VK_SUCCESS)
			{
				Fail("post pipeline layout creation failed"); return false;
			}
			post_layouts[key] = pipeline_layout;
			const std::string name = ObjectName("vk.post.layout", static_cast<uint32_t>(key.node), static_cast<uint32_t>(key.variant));
			if (set_layout) Name(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, HandleValue(set_layout), name.c_str());
			Name(VK_OBJECT_TYPE_PIPELINE_LAYOUT, HandleValue(pipeline_layout), name.c_str());
		}
		return true;
	}

	bool CreateTerrainLayout()
	{
		VkDescriptorSetLayoutBinding bindings[kTerrainDescriptorBindingCount] = {};
		for (uint32_t i = 0; i < kTerrainDescriptorBindingCount; ++i)
		{
			bindings[i].binding = i;
			bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			bindings[i].descriptorCount = 1;
			bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		}
		VkDescriptorSetLayoutCreateInfo set_create = {
			VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		set_create.bindingCount = kTerrainDescriptorBindingCount;
		set_create.pBindings = bindings;
		if (vkCreateDescriptorSetLayout(device, &set_create, nullptr,
				&terrain_set) != VK_SUCCESS)
		{
			Fail("terrain emitter descriptor-set layout creation failed");
			return false;
		}
		VkPushConstantRange push = {};
		push.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		push.size = sizeof(TerrainEmitterPush);
		VkPipelineLayoutCreateInfo layout_create = {
			VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		layout_create.setLayoutCount = 1;
		layout_create.pSetLayouts = &terrain_set;
		layout_create.pushConstantRangeCount = 1;
		layout_create.pPushConstantRanges = &push;
		if (vkCreatePipelineLayout(device, &layout_create, nullptr,
				&terrain_layout) != VK_SUCCESS)
		{
			Fail("terrain emitter pipeline layout creation failed");
			return false;
		}
		Name(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, HandleValue(terrain_set),
			"vk.terrain.set_layout");
		Name(VK_OBJECT_TYPE_PIPELINE_LAYOUT, HandleValue(terrain_layout),
			"vk.terrain.pipeline_layout");
		return true;
	}

	bool CreateCache()
	{
		const VkPhysicalDeviceProperties &properties = platform->SelectedDevice().properties;
		std::vector<uint8_t> file;
		const uint8_t *initial = nullptr; size_t initial_size = 0;
		if (ReadNativeFile(cache_path.c_str(), &file) && file.size() >= sizeof(CacheFileHeader))
		{
			const CacheFileHeader *header = reinterpret_cast<const CacheFileHeader *>(file.data());
			if (std::memcmp(header->magic, "PICCUVKC", 8) == 0 && header->schema == kCacheSchema &&
				header->header_size == sizeof(CacheFileHeader) && header->vendor_id == properties.vendorID &&
				header->device_id == properties.deviceID && header->driver_version == properties.driverVersion &&
				header->api_version == properties.apiVersion &&
				std::memcmp(header->pipeline_cache_uuid, properties.pipelineCacheUUID, VK_UUID_SIZE) == 0 &&
				header->shader_fingerprint == shader_fingerprint)
			{
				initial = file.data() + sizeof(CacheFileHeader);
				initial_size = file.size() - sizeof(CacheFileHeader);
			}
		}
		VkPipelineCacheCreateInfo create = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
		create.initialDataSize = initial_size; create.pInitialData = initial;
		VkResult result = vkCreatePipelineCache(device, &create, nullptr, &cache);
		if (result != VK_SUCCESS && initial_size)
		{
			create.initialDataSize = 0; create.pInitialData = nullptr;
			result = vkCreatePipelineCache(device, &create, nullptr, &cache);
		}
		if (result != VK_SUCCESS) { Fail("pipeline cache creation failed"); return false; }
		Name(VK_OBJECT_TYPE_PIPELINE_CACHE, HandleValue(cache), "vk.pipeline_cache");
		return true;
	}

	void SaveCache()
	{
		if (!cache_write || !cache || !device || cache_path.empty()) return;
		size_t size = 0;
		if (vkGetPipelineCacheData(device, cache, &size, nullptr) != VK_SUCCESS || !size) return;
		std::vector<uint8_t> bytes(sizeof(CacheFileHeader) + size);
		if (vkGetPipelineCacheData(device, cache, &size, bytes.data() + sizeof(CacheFileHeader)) != VK_SUCCESS) return;
		bytes.resize(sizeof(CacheFileHeader) + size);
		CacheFileHeader header = {};
		std::memcpy(header.magic, "PICCUVKC", 8); header.schema = kCacheSchema; header.header_size = sizeof(header);
		const VkPhysicalDeviceProperties &properties = platform->SelectedDevice().properties;
		header.vendor_id = properties.vendorID; header.device_id = properties.deviceID;
		header.driver_version = properties.driverVersion; header.api_version = properties.apiVersion;
		std::memcpy(header.pipeline_cache_uuid, properties.pipelineCacheUUID, VK_UUID_SIZE);
		header.shader_fingerprint = shader_fingerprint;
		std::memcpy(bytes.data(), &header, sizeof(header));
		AtomicWriteNativeFile(cache_path.c_str(), bytes.data(), bytes.size());
	}

	bool CreateGraphics(VkShaderModule vertex, VkShaderModule fragment,
		VkPipelineLayout layout, VkPrimitiveTopology topology,
		const VkVertexInputBindingDescription *vertex_binding,
		const VkVertexInputAttributeDescription *attributes, uint32_t attribute_count,
		VkSampleCountFlagBits samples, const VkFormat *colors, uint32_t color_count,
		VkFormat depth_format, const VkPipelineColorBlendAttachmentState *blends,
		uint32_t depth_test, uint32_t depth_write, VkCompareOp depth_compare,
		uint32_t cull, VkFrontFace front_face, uint32_t depth_bias,
		uint32_t vertex_depth_interpretation,
		VkPipeline *output)
	{
		VkPipelineShaderStageCreateInfo stages[2] = {};
		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vertex; stages[0].pName = "main";
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fragment; stages[1].pName = "main";
		VkSpecializationMapEntry depth_map = { 0, 0, sizeof(uint32_t) };
		VkSpecializationInfo depth_specialization = { 1, &depth_map,
			sizeof(vertex_depth_interpretation), &vertex_depth_interpretation };
		if (vertex_depth_interpretation != UINT32_MAX)
			stages[0].pSpecializationInfo = &depth_specialization;
		VkPipelineVertexInputStateCreateInfo vertex_input = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
		if (vertex_binding)
		{
			vertex_input.vertexBindingDescriptionCount = 1; vertex_input.pVertexBindingDescriptions = vertex_binding;
			vertex_input.vertexAttributeDescriptionCount = attribute_count; vertex_input.pVertexAttributeDescriptions = attributes;
		}
		VkPipelineInputAssemblyStateCreateInfo input = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
		input.topology = topology;
		VkPipelineViewportStateCreateInfo viewport = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
		viewport.viewportCount = 1; viewport.scissorCount = 1;
		VkPipelineRasterizationStateCreateInfo raster = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
		raster.polygonMode = VK_POLYGON_MODE_FILL; raster.cullMode = cull ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
		raster.frontFace = front_face; raster.lineWidth = 1.0f; raster.depthBiasEnable = depth_bias ? VK_TRUE : VK_FALSE;
		VkPipelineMultisampleStateCreateInfo multisample = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
		multisample.rasterizationSamples = samples;
		VkPipelineDepthStencilStateCreateInfo depth = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
		depth.depthTestEnable = depth_test ? VK_TRUE : VK_FALSE; depth.depthWriteEnable = depth_write ? VK_TRUE : VK_FALSE;
		depth.depthCompareOp = depth_compare;
		VkPipelineColorBlendStateCreateInfo blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
		blend.attachmentCount = color_count; blend.pAttachments = blends;
		const VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS };
		VkPipelineDynamicStateCreateInfo dynamic = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
		dynamic.dynamicStateCount = depth_bias ? 3u : 2u; dynamic.pDynamicStates = dynamic_states;
		VkPipelineRenderingCreateInfo rendering = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
		rendering.colorAttachmentCount = color_count; rendering.pColorAttachmentFormats = colors;
		rendering.depthAttachmentFormat = depth_format;
		VkGraphicsPipelineCreateInfo create = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
		create.pNext = &rendering; create.stageCount = 2; create.pStages = stages;
		create.pVertexInputState = &vertex_input; create.pInputAssemblyState = &input;
		create.pViewportState = &viewport; create.pRasterizationState = &raster;
		create.pMultisampleState = &multisample; create.pDepthStencilState = &depth;
		create.pColorBlendState = &blend; create.pDynamicState = &dynamic; create.layout = layout;
		return vkCreateGraphicsPipelines(device, cache, 1, &create, nullptr, output) == VK_SUCCESS;
	}

	bool CreateWorldPipeline(const WorldPipelineKey &key)
	{
		const char *vertex_name = "world.vert.spv"; const char *fragment_name = "world.frag.spv";
		switch (key.family)
		{
		case WorldPipelineFamily::Ui: vertex_name="ui.vert.spv"; fragment_name="ui.frag.spv"; break;
		case WorldPipelineFamily::Font: vertex_name="font.vert.spv"; fragment_name="font.frag.spv"; break;
		case WorldPipelineFamily::ExpandedLine: vertex_name="line.vert.spv"; fragment_name="line.frag.spv"; break;
		case WorldPipelineFamily::ExpandedPoint: vertex_name="point.vert.spv"; fragment_name="point.frag.spv"; break;
		case WorldPipelineFamily::Particle: vertex_name="particle.vert.spv"; fragment_name="particle.frag.spv"; break;
		default: break;
		}
		VkVertexInputBindingDescription binding = { 0, sizeof(BaseVertex), VK_VERTEX_INPUT_RATE_VERTEX };
		VkVertexInputAttributeDescription attributes[4] = {
			{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(BaseVertex, position)) },
			{ 1, 0, VK_FORMAT_R32_UINT, static_cast<uint32_t>(offsetof(BaseVertex, rgba8)) },
			{ 2, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(BaseVertex, uv0)) },
			{ 3, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(BaseVertex, uv1)) },
		};
		const bool expanded = key.family == WorldPipelineFamily::ExpandedLine ||
			key.family == WorldPipelineFamily::ExpandedPoint;
		if (expanded && key.topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
		{
			Fail("expanded line/point pipelines require triangle-list topology");
			return false;
		}
		VkPipelineColorBlendAttachmentState blends[5] = {};
		const BlendClassContract &legacy = BlendContract(key.blend);
		for (uint32_t location = 0; location < 5; ++location)
		{
			const SceneAttachmentContract &attachment = kSceneAttachmentContract[location];
			VkPipelineColorBlendAttachmentState &state = blends[location];
			state.colorWriteMask = (key.mrt_write_mask & (1u << location)) ?
				ColorMask(kChannelRgba) : 0;
			if (attachment.blend_mode == AttachmentBlendMode::LegacyColorByDraw)
			{
				state.blendEnable = legacy.blend_enabled ? VK_TRUE : VK_FALSE;
				state.srcColorBlendFactor = BlendFactor(legacy.source_rgb);
				state.dstColorBlendFactor = BlendFactor(legacy.destination_rgb);
				state.srcAlphaBlendFactor = BlendFactor(legacy.source_alpha);
				state.dstAlphaBlendFactor = BlendFactor(legacy.destination_alpha);
			}
			else if (attachment.blend_mode == AttachmentBlendMode::ComponentMax)
			{
				state.blendEnable = VK_TRUE; state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
				state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE; state.colorBlendOp = VK_BLEND_OP_MAX;
				state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				state.alphaBlendOp = VK_BLEND_OP_MAX;
			}
			else
			{
				state.blendEnable = VK_FALSE; state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
				state.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			}
			if (state.colorBlendOp == 0) state.colorBlendOp = VK_BLEND_OP_ADD;
			if (state.alphaBlendOp == 0) state.alphaBlendOp = VK_BLEND_OP_ADD;
		}
		WorldPipelineRecord record; record.key = key;
		uint32_t color_count = 5;
		while (color_count != 0 && key.formats.color[color_count - 1] == VK_FORMAT_UNDEFINED)
			--color_count;
		if (!CreateGraphics(Shader(vertex_name)->module, Shader(fragment_name)->module, world_layout,
			key.topology, expanded ? nullptr : &binding,
			expanded ? nullptr : attributes, expanded ? 0u : 4u,
			key.samples, key.formats.color, color_count,
			key.formats.depth, blends, key.depth_test_enabled, key.depth_write_enabled,
			key.depth_compare, key.cull_enabled, key.front_face, key.depth_bias_enabled,
			static_cast<uint32_t>(key.depth_interpretation), &record.pipeline))
		{
			Fail("world graphics-pipeline creation failed"); return false;
		}
		world_pipelines.push_back(record);
		const std::string name = ObjectName("vk.world.pipeline", static_cast<uint32_t>(key.family), static_cast<uint32_t>(world_pipelines.size()-1));
		Name(VK_OBJECT_TYPE_PIPELINE, HandleValue(record.pipeline), name.c_str());
		return true;
	}

	bool CreateDefaultWorldMatrix(const PipelineLibraryCreateInfo &info)
	{
		std::vector<WorldPipelineKey> keys = info.world_pipeline_keys;
		if (info.precreate_default_world_matrix)
		{
			WorldTargetFormats target_formats[3] = { info.world_formats,
				info.world_formats, info.world_formats };
			for (uint32_t location = 1; location < 5; ++location)
			{
				target_formats[1].color[location] = VK_FORMAT_UNDEFINED;
				target_formats[2].color[location] = VK_FORMAT_UNDEFINED;
			}
			target_formats[1].depth = VK_FORMAT_UNDEFINED; // PostPresent
			for (uint32_t family = 0; family < static_cast<uint32_t>(WorldPipelineFamily::Count); ++family)
			{
				const uint32_t blend_count = (family == static_cast<uint32_t>(WorldPipelineFamily::Stream) ||
					family == static_cast<uint32_t>(WorldPipelineFamily::RetainedWorld) ||
					family == static_cast<uint32_t>(WorldPipelineFamily::Particle)) ? 4u : 2u;
				for (uint32_t blend = 0; blend < blend_count; ++blend)
				{
					WorldPipelineKey base; base.family = static_cast<WorldPipelineFamily>(family);
					base.blend = static_cast<BlendClass>(blend); base.samples = info.target_samples;
					base.mrt_write_mask = kWriteColor;
					base.depth_test_enabled = family == static_cast<uint32_t>(WorldPipelineFamily::Ui) ||
						family == static_cast<uint32_t>(WorldPipelineFamily::Font) ? 0u : 1u;
					base.depth_write_enabled = (family == static_cast<uint32_t>(WorldPipelineFamily::Stream) ||
						family == static_cast<uint32_t>(WorldPipelineFamily::RetainedWorld)) ? 1u : 0u;
					if (family == static_cast<uint32_t>(WorldPipelineFamily::ExpandedLine))
						{ base.raster = RasterFamily::ExpandedLine; base.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
						  base.depth_interpretation = DepthInterpretation::AlreadyMapped; }
					if (family == static_cast<uint32_t>(WorldPipelineFamily::ExpandedPoint))
						{ base.raster = RasterFamily::ExpandedPoint; base.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
						  base.depth_interpretation = DepthInterpretation::AlreadyMapped; }
					if (family == static_cast<uint32_t>(WorldPipelineFamily::Ui) ||
						family == static_cast<uint32_t>(WorldPipelineFamily::Font))
						base.depth_interpretation = DepthInterpretation::Irrelevant;
					for (uint32_t target = 0; target < 3; ++target)
					{
						WorldPipelineKey key = base; key.formats = target_formats[target];
						if (target == 1) { key.samples = VK_SAMPLE_COUNT_1_BIT;
							key.depth_test_enabled = 0; key.depth_write_enabled = 0; }
						if (std::find(keys.begin(), keys.end(), key) == keys.end()) keys.push_back(key);
					}
				}
			}
		}
		for (size_t i = 0; i < keys.size(); ++i)
			if (!CreateWorldPipeline(keys[i])) return false;
		return true;
	}

	bool CreatePostPipeline(GraphNodeId node, PostPassVariant variant,
		VkFormat override_format, VkSampleCountFlagBits samples)
	{
		const char *fragment_name = PostFragment(node, variant);
		if (!fragment_name) return true;
		NodeVariantKey key = { node, variant };
		auto layout_it = post_layouts.find(key);
		if (layout_it == post_layouts.end()) { Fail("post pipeline has no layout"); return false; }
		const bool depth_only = DepthOnlyNode(node);
		VkFormat color = override_format != VK_FORMAT_UNDEFINED ? override_format : PostFormat(node);
		VkPipelineColorBlendAttachmentState blend = {};
		blend.colorWriteMask = ColorMask(kChannelRgba);
		blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; blend.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
		blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		blend.colorBlendOp = VK_BLEND_OP_ADD; blend.alphaBlendOp = VK_BLEND_OP_ADD;
		if (node == GraphNodeId::MotionDebugNormal || node == GraphNodeId::MotionDebugCockpitPre)
		{
			blend.blendEnable = VK_TRUE; blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		}
		if (node == GraphNodeId::CockpitOver)
		{
			blend.blendEnable = VK_TRUE; blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
			blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		}
		if (node == GraphNodeId::PostAlphaClear) blend.colorWriteMask = VK_COLOR_COMPONENT_A_BIT;
		VkPipeline pipeline = VK_NULL_HANDLE;
		if (!CreateGraphics(Shader("fullscreen.vert.spv")->module, Shader(fragment_name)->module,
			layout_it->second, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, nullptr, nullptr, 0,
			samples, depth_only ? nullptr : &color, depth_only ? 0u : 1u,
			depth_only ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_UNDEFINED, depth_only ? nullptr : &blend,
			depth_only ? 1u : 0u, depth_only ? 1u : 0u, VK_COMPARE_OP_ALWAYS, 0,
			VK_FRONT_FACE_COUNTER_CLOCKWISE, 0, UINT32_MAX, &pipeline))
		{
			Fail("post graphics-pipeline creation failed"); return false;
		}
		PostPipelineKey pipeline_key = { key,
			node == GraphNodeId::Present ? color : VK_FORMAT_UNDEFINED, samples };
		post_pipelines[pipeline_key] = pipeline;
		const std::string name = ObjectName("vk.post.pipeline", static_cast<uint32_t>(node), static_cast<uint32_t>(variant));
		Name(VK_OBJECT_TYPE_PIPELINE, HandleValue(pipeline), name.c_str());
		return true;
	}

	bool CreatePostPipelines()
	{
		for (size_t i = 0; i < kPostPassDescriptorSetCount; ++i)
		{
			const PostPassDescriptorSetContract &contract = kPostPassDescriptorSets[i];
			if (IsWorldUse(contract.use)) continue;
			if (contract.node == GraphNodeId::Present)
			{
				if (!CreatePostPipeline(contract.node, contract.variant,
						VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT) ||
					!CreatePostPipeline(contract.node, contract.variant,
						VK_FORMAT_B8G8R8A8_UNORM, VK_SAMPLE_COUNT_1_BIT)) return false;
			}
			else if (contract.node == GraphNodeId::PostAlphaClear)
			{
				if (!CreatePostPipeline(contract.node, contract.variant,
					VK_FORMAT_UNDEFINED, VK_SAMPLE_COUNT_1_BIT) ||
					(target_samples != VK_SAMPLE_COUNT_1_BIT &&
					 !CreatePostPipeline(contract.node, contract.variant,
						 VK_FORMAT_UNDEFINED, target_samples))) return false;
			}
			else if (!CreatePostPipeline(contract.node, contract.variant,
				VK_FORMAT_UNDEFINED, VK_SAMPLE_COUNT_1_BIT)) return false;
		}
		// The velocity copy is a graph-compiler utility with the standard post ABI.
		NodeVariantKey motion_layout_key = { GraphNodeId::MotionNormal, PostPassVariant::Only };
		auto layout = post_layouts.find(motion_layout_key);
		VkPipelineColorBlendAttachmentState blend = {};
		blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT;
		blend.srcColorBlendFactor = blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		blend.dstColorBlendFactor = blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		blend.colorBlendOp = blend.alphaBlendOp = VK_BLEND_OP_ADD;
		const VkFormat format = VK_FORMAT_R16G16_SFLOAT;
		if (layout == post_layouts.end() || !CreateGraphics(Shader("fullscreen.vert.spv")->module,
			Shader("motion_copy.frag.spv")->module, layout->second, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
			nullptr, nullptr, 0, VK_SAMPLE_COUNT_1_BIT, &format, 1, VK_FORMAT_UNDEFINED, &blend,
			0, 0, VK_COMPARE_OP_ALWAYS, 0, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0,
			UINT32_MAX,
			&utility[static_cast<uint32_t>(UtilityPipeline::MotionVelocityCopy)]))
		{
			Fail("motion velocity-copy pipeline creation failed"); return false;
		}
		return true;
	}

	bool CreateTerrainPipelines()
	{
		static const char *names[] = { "terrain_classify.comp.spv",
			"terrain_scan.comp.spv", "terrain_emit.comp.spv" };
		for (uint32_t i = 0;
			i < static_cast<uint32_t>(TerrainComputeStage::Count); ++i)
		{
			ShaderRecord *shader = Shader(names[i]);
			if (!shader) { Fail("terrain emitter shader module is missing"); return false; }
			VkPipelineShaderStageCreateInfo stage = {
				VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
			stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
			stage.module = shader->module;
			stage.pName = "main";
			VkComputePipelineCreateInfo create = {
				VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
			create.stage = stage;
			create.layout = terrain_layout;
			if (vkCreateComputePipelines(device, cache, 1, &create, nullptr,
					&terrain_pipelines[i]) != VK_SUCCESS)
			{
				Fail(std::string("terrain emitter pipeline creation failed: ") + names[i]);
				return false;
			}
			Name(VK_OBJECT_TYPE_PIPELINE, HandleValue(terrain_pipelines[i]), names[i]);
		}
		return true;
	}

	void Destroy()
	{
		if (!device) return;
		SaveCache();
		for (uint32_t i=0;i<static_cast<uint32_t>(TerrainComputeStage::Count);++i)
			if (terrain_pipelines[i]) vkDestroyPipeline(device, terrain_pipelines[i], nullptr);
		for (uint32_t i=0;i<static_cast<uint32_t>(UtilityPipeline::Count);++i) if (utility[i]) vkDestroyPipeline(device, utility[i], nullptr);
		for (auto &entry : post_pipelines) if (entry.second) vkDestroyPipeline(device, entry.second, nullptr);
		for (size_t i=0;i<world_pipelines.size();++i) if (world_pipelines[i].pipeline) vkDestroyPipeline(device, world_pipelines[i].pipeline, nullptr);
		for (auto &entry : post_layouts) if (entry.second) vkDestroyPipelineLayout(device, entry.second, nullptr);
		for (auto &entry : post_sets) if (entry.second) vkDestroyDescriptorSetLayout(device, entry.second, nullptr);
		if (terrain_layout) vkDestroyPipelineLayout(device, terrain_layout, nullptr);
		if (terrain_set) vkDestroyDescriptorSetLayout(device, terrain_set, nullptr);
		if (world_layout) vkDestroyPipelineLayout(device, world_layout, nullptr);
		for (uint32_t i=0;i<3;++i) if (world_sets[i]) vkDestroyDescriptorSetLayout(device, world_sets[i], nullptr);
		if (cache) vkDestroyPipelineCache(device, cache, nullptr);
		for (size_t i=0;i<shaders.size();++i) if (shaders[i].module) vkDestroyShaderModule(device, shaders[i].module, nullptr);
		for (uint32_t i=0;i<static_cast<uint32_t>(SamplerSemantic::Count);++i) if (semantic_samplers[i]) vkDestroySampler(device, semantic_samplers[i], nullptr);
		std::memset(utility, 0, sizeof(utility));
		std::memset(terrain_pipelines, 0, sizeof(terrain_pipelines));
		std::memset(semantic_samplers, 0, sizeof(semantic_samplers));
		std::memset(world_sampler_table, 0, sizeof(world_sampler_table));
		std::memset(post_sampler_table, 0, sizeof(post_sampler_table));
		std::memset(world_sets, 0, sizeof(world_sets));
		world_layout = VK_NULL_HANDLE; terrain_layout = VK_NULL_HANDLE;
		terrain_set = VK_NULL_HANDLE; cache = VK_NULL_HANDLE;
		post_pipelines.clear(); world_pipelines.clear(); post_layouts.clear();
		post_sets.clear(); shaders.clear(); shader_fingerprint = kFnvOffset;
		ready=false;
	}
};

bool operator==(const WorldPipelineKey &a, const WorldPipelineKey &b) noexcept
{
	return a.family==b.family && a.blend==b.blend && a.raster==b.raster && a.topology==b.topology &&
		a.samples==b.samples && a.depth_interpretation==b.depth_interpretation &&
		a.mrt_write_mask==b.mrt_write_mask && a.cull_enabled==b.cull_enabled &&
		a.front_face==b.front_face && a.depth_test_enabled==b.depth_test_enabled &&
		a.depth_write_enabled==b.depth_write_enabled && a.depth_compare==b.depth_compare &&
		a.depth_bias_enabled==b.depth_bias_enabled && a.formats.depth==b.formats.depth &&
		std::memcmp(a.formats.color,b.formats.color,sizeof(a.formats.color))==0;
}

PipelineLibrary::PipelineLibrary() : impl_(new Impl) {}
PipelineLibrary::~PipelineLibrary() { Shutdown(); delete impl_; }

bool PipelineLibrary::Initialize(const PipelineLibraryCreateInfo &info)
{
	if (impl_->ready) return true;
	impl_->error.clear();
	if (!info.platform || !info.platform->Ready()) { impl_->Fail("Platform is not ready"); return false; }
	impl_->platform=info.platform; impl_->targets=info.targets; impl_->frames=info.frames;
	impl_->device=info.platform->Device(); impl_->target_samples=info.target_samples;
	impl_->asset_prefix=info.shader_asset_prefix?info.shader_asset_prefix:"";
	if (!impl_->asset_prefix.empty() && impl_->asset_prefix.back()!='/' && impl_->asset_prefix.back()!='\\') impl_->asset_prefix += '/';
	impl_->cache_path=info.pipeline_cache_path?info.pipeline_cache_path:""; impl_->cache_write=info.enable_pipeline_cache_write!=0;
	const uint32_t supported=info.platform->SelectedDevice().contract_candidate.profile.descriptor_page_tier;
	impl_->page_tier=info.descriptor_page_tier?SelectDescriptorPageTier(std::min(info.descriptor_page_tier,supported)):supported;
	if (!impl_->page_tier) { impl_->Fail("No descriptor page tier is available"); return false; }
	if (!impl_->CreateSamplers() || !impl_->CreateWorldLayouts() ||
		!impl_->CreatePostLayouts() || !impl_->CreateTerrainLayout() ||
		!impl_->LoadShaders() || !impl_->CreateCache() || !impl_->CreateDefaultWorldMatrix(info) ||
		!impl_->CreatePostPipelines() || !impl_->CreateTerrainPipelines())
	{
		impl_->Destroy(); return false;
	}
	impl_->ready=true; return true;
}

void PipelineLibrary::Shutdown(bool device_lost) noexcept
{
	if (!impl_ || !impl_->device) return;
	if (device_lost) impl_->cache_write=false;
	impl_->Destroy(); impl_->device=VK_NULL_HANDLE;
}
bool PipelineLibrary::Ready() const noexcept { return impl_ && impl_->ready; }
const std::string &PipelineLibrary::LastError() const noexcept { return impl_->error; }
uint32_t PipelineLibrary::DescriptorPageTier() const noexcept { return impl_->page_tier; }
VkSampler PipelineLibrary::Sampler(SamplerSemantic semantic) const noexcept { uint32_t i=static_cast<uint32_t>(semantic); return i<static_cast<uint32_t>(SamplerSemantic::Count)?impl_->semantic_samplers[i]:VK_NULL_HANDLE; }
VkDescriptorSetLayout PipelineLibrary::WorldSetLayout(uint32_t set) const noexcept { return set<3?impl_->world_sets[set]:VK_NULL_HANDLE; }
VkPipelineLayout PipelineLibrary::WorldPipelineLayout() const noexcept { return impl_->world_layout; }
VkDescriptorSetLayout PipelineLibrary::PostSetLayout(GraphNodeId node, PostPassVariant variant) const noexcept { auto it=impl_->post_sets.find({node,variant}); return it==impl_->post_sets.end()?VK_NULL_HANDLE:it->second; }
VkPipelineLayout PipelineLibrary::PostPipelineLayout(GraphNodeId node, PostPassVariant variant) const noexcept { auto it=impl_->post_layouts.find({node,variant}); return it==impl_->post_layouts.end()?VK_NULL_HANDLE:it->second; }
bool PipelineLibrary::EnsureWorldPipelines(const std::vector<WorldPipelineKey> &keys)
{
	if (!Ready()) return false;
	for (const WorldPipelineKey &key : keys)
	{
		bool exists = false;
		for (const WorldPipelineRecord &record : impl_->world_pipelines)
			if (record.key == key)
			{
				exists = true;
				break;
			}
		if (!exists && !impl_->CreateWorldPipeline(key)) return false;
	}
	return true;
}
VkPipeline PipelineLibrary::FindWorldPipeline(const WorldPipelineKey &key) const noexcept { for(size_t i=0;i<impl_->world_pipelines.size();++i)if(impl_->world_pipelines[i].key==key)return impl_->world_pipelines[i].pipeline;return VK_NULL_HANDLE; }
VkPipeline PipelineLibrary::FindPostPipeline(GraphNodeId node,
	PostPassVariant variant, VkFormat format,
	VkSampleCountFlagBits samples) const noexcept
{
	PostPipelineKey key = { { node, variant },
		node == GraphNodeId::Present ? format : VK_FORMAT_UNDEFINED, samples };
	auto it = impl_->post_pipelines.find(key);
	return it == impl_->post_pipelines.end() ? VK_NULL_HANDLE : it->second;
}
VkPipeline PipelineLibrary::FindUtilityPipeline(UtilityPipeline p) const noexcept { uint32_t i=static_cast<uint32_t>(p);return i<static_cast<uint32_t>(UtilityPipeline::Count)?impl_->utility[i]:VK_NULL_HANDLE; }
VkDescriptorSetLayout PipelineLibrary::TerrainDescriptorSetLayout()const noexcept{return impl_->terrain_set;}
VkPipelineLayout PipelineLibrary::TerrainPipelineLayout()const noexcept{return impl_->terrain_layout;}
VkPipeline PipelineLibrary::TerrainPipeline(TerrainComputeStage stage)const noexcept{uint32_t i=static_cast<uint32_t>(stage);return i<static_cast<uint32_t>(TerrainComputeStage::Count)?impl_->terrain_pipelines[i]:VK_NULL_HANDLE;}

bool PipelineLibrary::PlanTerrainEmitter(const TerrainEmitterPush&push,TerrainEmitterDispatchPlan*plan)const noexcept
{
	if(!Ready()||!plan||push.flags!=0||push.work_item_count==0||push.batch_count==0||
		push.batch_count>push.work_item_count||uint64_t(push.work_item_count)*
		kTerrainMaximumOutputVerticesPerCell>push.output_vertex_capacity)return false;
	TerrainEmitterDispatchPlan result;
	result.classify_groups=(push.work_item_count+kTerrainEmitterContract.classify_group_size-1u)/
		kTerrainEmitterContract.classify_group_size;
	result.scan_groups=push.batch_count;
	result.emit_groups=(push.work_item_count+kTerrainEmitterContract.emit_group_size-1u)/
		kTerrainEmitterContract.emit_group_size;
	const uint32_t maximum=impl_->platform->SelectedDevice().properties.limits.maxComputeWorkGroupCount[0];
	if(result.classify_groups>maximum||result.scan_groups>maximum||result.emit_groups>maximum)return false;
	*plan=result;return true;
}

bool PipelineLibrary::AllocateTerrainDescriptorSet(VkDescriptorPool pool,VkDescriptorSet*out)const
{
	if(!Ready()||!pool||!out||!impl_->terrain_set)return false;
	VkDescriptorSetAllocateInfo info={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
	info.descriptorPool=pool;info.descriptorSetCount=1;info.pSetLayouts=&impl_->terrain_set;
	return vkAllocateDescriptorSets(impl_->device,&info,out)==VK_SUCCESS;
}
bool PipelineLibrary::AllocateTerrainDescriptorSet(const FrameContext&frame,VkDescriptorSet*out)const
{
	return AllocateTerrainDescriptorSet(frame.descriptor_pool,out);
}

bool PipelineLibrary::UpdateTerrainDescriptorSet(VkDescriptorSet set,
	const TerrainEmitterDescriptorWrite&write,const TerrainEmitterPush&push)const
{
	TerrainEmitterDispatchPlan plan;
	if(!set||!PlanTerrainEmitter(push,&plan))return false;
	const VkDeviceSize required[kTerrainDescriptorBindingCount]={
		sizeof(TerrainEmitterCell),
		VkDeviceSize(push.work_item_count)*sizeof(TerrainWorkItem),
		VkDeviceSize(push.batch_count)*sizeof(TerrainBatchInput),
		sizeof(TerrainViewInput),
		VkDeviceSize(push.work_item_count)*sizeof(uint32_t)*2u,
		VkDeviceSize(push.output_vertex_capacity)*sizeof(BaseVertex),
		VkDeviceSize(push.output_vertex_capacity)*sizeof(TerrainVertexPayload),
		VkDeviceSize(push.batch_count)*sizeof(TerrainIndirectCommand)};
	VkDescriptorBufferInfo buffers[kTerrainDescriptorBindingCount]={};
	VkWriteDescriptorSet writes[kTerrainDescriptorBindingCount]={};
	const VkPhysicalDeviceLimits&limits=
		impl_->platform->SelectedDevice().properties.limits;
	const VkDeviceSize offset_alignment=limits.minStorageBufferOffsetAlignment;
	for(uint32_t i=0;i<kTerrainDescriptorBindingCount;++i){
		const TerrainBufferBinding&binding=write.bindings[i];
		if(!binding.buffer||binding.range==VK_WHOLE_SIZE||binding.range<required[i]||
			binding.range>limits.maxStorageBufferRange||
			(offset_alignment&&binding.offset%offset_alignment)!=0||
			binding.offset>UINT64_MAX-binding.range)return false;
		buffers[i].buffer=binding.buffer;buffers[i].offset=binding.offset;buffers[i].range=binding.range;
		writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=set;
		writes[i].dstBinding=i;writes[i].descriptorCount=1;
		writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=&buffers[i];
	}
	vkUpdateDescriptorSets(impl_->device,kTerrainDescriptorBindingCount,writes,0,nullptr);
	return true;
}

bool PipelineLibrary::RecordTerrainEmitter(VkCommandBuffer command,VkDescriptorSet set,
	const TerrainEmitterPush&push)const
{
	TerrainEmitterDispatchPlan plan;
	if(!command||!set||!PlanTerrainEmitter(push,&plan))return false;
	for(uint32_t i=0;i<static_cast<uint32_t>(TerrainComputeStage::Count);++i)
		if(!impl_->terrain_pipelines[i])return false;
	vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,impl_->terrain_layout,
		0,1,&set,0,nullptr);
	vkCmdPushConstants(command,impl_->terrain_layout,VK_SHADER_STAGE_COMPUTE_BIT,
		0,sizeof(push),&push);
	auto barrier=[&](VkPipelineStageFlags2 destination_stages,
		VkAccessFlags2 destination_access){
		VkMemoryBarrier2 memory={VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
		memory.srcStageMask=VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		memory.srcAccessMask=VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
		memory.dstStageMask=destination_stages;memory.dstAccessMask=destination_access;
		VkDependencyInfo dependency={VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
		dependency.memoryBarrierCount=1;dependency.pMemoryBarriers=&memory;
		vkCmdPipelineBarrier2(command,&dependency);
	};
	vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,
		impl_->terrain_pipelines[static_cast<uint32_t>(TerrainComputeStage::Classify)]);
	vkCmdDispatch(command,plan.classify_groups,1,1);
	barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		VK_ACCESS_2_SHADER_STORAGE_READ_BIT|VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
	vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,
		impl_->terrain_pipelines[static_cast<uint32_t>(TerrainComputeStage::Scan)]);
	vkCmdDispatch(command,plan.scan_groups,1,1);
	barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		VK_ACCESS_2_SHADER_STORAGE_READ_BIT|VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
	vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,
		impl_->terrain_pipelines[static_cast<uint32_t>(TerrainComputeStage::Emit)]);
	vkCmdDispatch(command,plan.emit_groups,1,1);
	barrier(VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT|
		VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT|VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
		VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT|VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT|
		VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
	return true;
}

bool PipelineLibrary::AllocateWorldDescriptorSets(VkDescriptorPool pool, WorldDescriptorSets *out) const
{
	if(!Ready()||!pool||!out)return false; VkDescriptorSetLayout layouts[3]={impl_->world_sets[0],impl_->world_sets[1],impl_->world_sets[2]};VkDescriptorSet sets[3]={};VkDescriptorSetAllocateInfo info={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};info.descriptorPool=pool;info.descriptorSetCount=3;info.pSetLayouts=layouts;if(vkAllocateDescriptorSets(impl_->device,&info,sets)!=VK_SUCCESS)return false;out->set0=sets[0];out->set1=sets[1];out->set2=sets[2];out->image_page_tier=impl_->page_tier;return true;
}
bool PipelineLibrary::AllocateWorldDescriptorSets(const FrameContext &frame,WorldDescriptorSets*out)const
{
	return AllocateWorldDescriptorSets(frame.descriptor_pool,out);
}
bool PipelineLibrary::UpdateWorldSet0(VkDescriptorSet set,const WorldSet0Write&w)const
{
	if(!Ready()||!set||!w.frame_view_buffer||w.range<sizeof(FrameViewGlobals))return false;VkDescriptorBufferInfo b={w.frame_view_buffer,w.offset,w.range};VkWriteDescriptorSet d={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};d.dstSet=set;d.dstBinding=0;d.descriptorCount=1;d.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;d.pBufferInfo=&b;vkUpdateDescriptorSets(impl_->device,1,&d,0,nullptr);return true;
}
bool PipelineLibrary::UpdateWorldSet1(VkDescriptorSet set,const WorldSet1Write&w)const
{
	if(!Ready()||!set||!w.float_images_2d||!w.float_image_samplers||w.float_image_count!=impl_->page_tier||!w.float_image_arrays||!w.float_image_array_samplers||w.float_image_array_count!=kWorldArrayImageCount)return false;std::vector<VkDescriptorImageInfo>a(w.float_image_count),b(w.float_image_array_count);for(uint32_t i=0;i<w.float_image_count;++i){a[i].sampler=w.float_image_samplers[i];a[i].imageView=w.float_images_2d[i];a[i].imageLayout=w.layout;if(!a[i].sampler||!a[i].imageView)return false;}for(uint32_t i=0;i<w.float_image_array_count;++i){b[i].sampler=w.float_image_array_samplers[i];b[i].imageView=w.float_image_arrays[i];b[i].imageLayout=w.layout;if(!b[i].sampler||!b[i].imageView)return false;}VkWriteDescriptorSet d[2]={};for(int i=0;i<2;++i)d[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;d[0].dstSet=d[1].dstSet=set;d[0].dstBinding=0;d[0].descriptorCount=w.float_image_count;d[0].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;d[0].pImageInfo=a.data();d[1].dstBinding=1;d[1].descriptorCount=w.float_image_array_count;d[1].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;d[1].pImageInfo=b.data();vkUpdateDescriptorSets(impl_->device,2,d,0,nullptr);return true;
}
bool PipelineLibrary::UpdateWorldSet2(VkDescriptorSet set,const WorldSet2Write&w)const
{
	if(!Ready()||!set)return false;VkDescriptorBufferInfo b[8]={};VkWriteDescriptorSet d[8]={};for(uint32_t i=0;i<8;++i){if(!w.buffers[i]||!w.ranges[i])return false;b[i]={w.buffers[i],w.offsets[i],w.ranges[i]};d[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;d[i].dstSet=set;d[i].dstBinding=i;d[i].descriptorCount=1;d[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;d[i].pBufferInfo=&b[i];}vkUpdateDescriptorSets(impl_->device,8,d,0,nullptr);return true;
}
bool PipelineLibrary::AllocatePostDescriptorSet(VkDescriptorPool pool,GraphNodeId node,PostPassVariant variant,VkDescriptorSet*out)const
{
	VkDescriptorSetLayout layout=PostSetLayout(node,variant);if(!Ready()||!pool||!layout||!out)return false;VkDescriptorSetAllocateInfo info={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};info.descriptorPool=pool;info.descriptorSetCount=1;info.pSetLayouts=&layout;return vkAllocateDescriptorSets(impl_->device,&info,out)==VK_SUCCESS;
}
bool PipelineLibrary::AllocatePostDescriptorSet(const FrameContext&frame,GraphNodeId node,PostPassVariant variant,VkDescriptorSet*out)const
{
	return AllocatePostDescriptorSet(frame.descriptor_pool,node,variant,out);
}
bool PipelineLibrary::UpdatePostDescriptorSet(VkDescriptorSet set,GraphNodeId node,PostPassVariant variant,const PostDescriptorWrite&w)const
{
	if(!Ready()||!set||!w.uniform_buffer||w.uniform_range<sizeof(PostPassUniforms))return false;std::vector<const PostPassDescriptorBindingContract*>images;for(size_t i=0;i<kPostPassDescriptorBindingCount;++i){const auto&c=kPostPassDescriptorBindings[i];if(c.node==node&&c.variant==variant&&c.kind!=PostDescriptorKind::UniformBuffer&&c.kind!=PostDescriptorKind::Sampler)images.push_back(&c);}if(images.size()!=w.image_count||(!images.empty()&&!w.images))return false;std::vector<VkDescriptorImageInfo>infos(images.size());std::vector<VkWriteDescriptorSet>writes(images.size()+1);VkDescriptorBufferInfo uniform={w.uniform_buffer,w.uniform_offset,w.uniform_range};writes[0].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[0].dstSet=set;writes[0].dstBinding=0;writes[0].descriptorCount=1;writes[0].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;writes[0].pBufferInfo=&uniform;for(size_t i=0;i<images.size();++i){const PostImageWrite*found=nullptr;for(uint32_t j=0;j<w.image_count;++j)if(w.images[j].binding==images[i]->binding){if(found)return false;found=&w.images[j];}if(!found||found->kind!=images[i]->kind||!found->view)return false;infos[i].imageView=found->view;infos[i].imageLayout=found->layout;writes[i+1].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i+1].dstSet=set;writes[i+1].dstBinding=found->binding;writes[i+1].descriptorCount=1;writes[i+1].descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;writes[i+1].pImageInfo=&infos[i];}vkUpdateDescriptorSets(impl_->device,static_cast<uint32_t>(writes.size()),writes.data(),0,nullptr);return true;
}
uint32_t PipelineLibrary::WorldPipelineCount()const noexcept{return static_cast<uint32_t>(impl_->world_pipelines.size());}
uint32_t PipelineLibrary::PostPipelineCount()const noexcept{return static_cast<uint32_t>(impl_->post_pipelines.size())+static_cast<uint32_t>(UtilityPipeline::Count);}
uint32_t PipelineLibrary::LoadedShaderCount()const noexcept{return static_cast<uint32_t>(impl_->shaders.size());}

} // namespace vk
} // namespace render
} // namespace piccu
