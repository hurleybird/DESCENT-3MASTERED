#include "vk_retained_world.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <utility>
#include <vector>

namespace piccu
{
namespace render
{
namespace vk
{

namespace
{

enum class ArenaKind : uint32_t
{
	Vertex = 0,
	Index,
	Perspective,
	Motion,
	Specular,
	TerrainCell,
	TerrainWork,
	TerrainBatch,
	TerrainIndirect,
	Count,
};

constexpr uint32_t kArenaCount = static_cast<uint32_t>(ArenaKind::Count);

bool SameSource(const RetainedSourceKey &left,
	const RetainedSourceKey &right)
{
	return left.kind == right.kind && left.source_id == right.source_id &&
		left.source_generation == right.source_generation;
}

bool SameSourceIdentity(const RetainedSourceKey &left,
	const RetainedSourceKey &right)
{
	return left.kind == right.kind && left.source_id == right.source_id;
}

bool SameHandle(MeshHandle left, MeshHandle right)
{
	return left.id == right.id && left.generation == right.generation;
}

bool TokenLess(const RetainedFaceToken &left, const RetainedFaceToken &right)
{
	if (left.source.kind != right.source.kind)
		return static_cast<uint32_t>(left.source.kind) <
			static_cast<uint32_t>(right.source.kind);
	if (left.source.source_id != right.source.source_id)
		return left.source.source_id < right.source.source_id;
	if (left.source.source_generation != right.source.source_generation)
		return left.source.source_generation < right.source.source_generation;
	if (left.subobject != right.subobject)
		return left.subobject < right.subobject;
	if (left.face != right.face)
		return left.face < right.face;
	return left.classification < right.classification;
}

bool SameToken(const RetainedFaceToken &left, const RetainedFaceToken &right)
{
	return !TokenLess(left, right) && !TokenLess(right, left);
}

bool ValidSource(const RetainedSourceKey &source)
{
	return static_cast<uint32_t>(source.kind) <=
		static_cast<uint32_t>(RetainedSourceKind::Polymodel) &&
		source.source_id != kInvalidId;
}

bool AddSize(VkDeviceSize value, VkDeviceSize addition, VkDeviceSize *result)
{
	if (!result || value > std::numeric_limits<VkDeviceSize>::max() - addition)
		return false;
	*result = value + addition;
	return true;
}

bool MultiplySize(uint32_t count, VkDeviceSize stride, VkDeviceSize *result)
{
	if (!result || (count != 0 && stride >
		std::numeric_limits<VkDeviceSize>::max() / count))
		return false;
	*result = VkDeviceSize(count) * stride;
	return true;
}

bool AlignSize(VkDeviceSize value, VkDeviceSize alignment,
	VkDeviceSize *result)
{
	if (!result || alignment == 0 || (alignment & (alignment - 1)) != 0)
		return false;
	const VkDeviceSize mask = alignment - 1;
	if (value > std::numeric_limits<VkDeviceSize>::max() - mask)
		return false;
	*result = (value + mask) & ~mask;
	return true;
}

Span32 EmptySpan()
{
	Span32 span = { kInvalidId, 0 };
	return span;
}

bool SpanFits(const Span32 &span, uint32_t count)
{
	if (span.count == 0)
		return span.offset == kInvalidId;
	return span.offset != kInvalidId && span.offset <= count &&
		span.count <= count - span.offset;
}

bool AddSpanBase(const Span32 &span, uint32_t base, Span32 *result)
{
	if (!result)
		return false;
	if (span.count == 0)
	{
		*result = EmptySpan();
		return span.offset == kInvalidId;
	}
	if (span.offset == kInvalidId || base > UINT32_MAX - span.offset)
		return false;
	result->offset = base + span.offset;
	result->count = span.count;
	return result->offset <= UINT32_MAX - result->count;
}

const char *ArenaName(ArenaKind kind)
{
	switch (kind)
	{
	case ArenaKind::Vertex: return "retained vertex shard";
	case ArenaKind::Index: return "retained index shard";
	case ArenaKind::Perspective: return "retained perspective shard";
	case ArenaKind::Motion: return "retained motion shard";
	case ArenaKind::Specular: return "retained specular shard";
	case ArenaKind::TerrainCell: return "retained terrain cell shard";
	case ArenaKind::TerrainWork: return "retained terrain work shard";
	case ArenaKind::TerrainBatch: return "retained terrain batch shard";
	case ArenaKind::TerrainIndirect: return "retained terrain indirect shard";
	default: return "retained shard";
	}
}

} // namespace

struct RetainedWorld::Impl
{
	struct ArenaShard
	{
		AllocatedBuffer buffer;
		VmaVirtualBlock virtual_block = VK_NULL_HANDLE;
		VkDeviceSize element_capacity = 0;
		VkDeviceSize live_bytes = 0;
		uint32_t generation = 0;
		uint32_t allocation_count = 0;
	};

	struct Arena
	{
		ArenaKind kind = ArenaKind::Vertex;
		VkDeviceSize element_size = 1;
		VkDeviceSize element_alignment = 1;
		VkDeviceSize initial_bytes = 1;
		VkBufferUsageFlags usage = 0;
		uint32_t next_generation = 1;
		std::vector<ArenaShard> shards;
	};

	struct AllocationRef
	{
		ArenaKind arena = ArenaKind::Vertex;
		uint32_t shard_generation = 0;
		VmaVirtualAllocation allocation = VK_NULL_HANDLE;
		VkDeviceSize byte_offset = 0;
		VkDeviceSize byte_size = 0;
		uint32_t first_element = 0;
		uint32_t element_count = 0;

		bool Valid() const noexcept
		{
			return allocation != VK_NULL_HANDLE && byte_size != 0 &&
				element_count != 0;
		}
	};

	struct FaceRecord
	{
		RetainedFaceToken token;
		RetainedRange range;
	};

	struct MeshSlot
	{
		uint32_t generation = 0;
		bool alive = false;
		bool terrain = false;
		bool upload_pending = false;
		bool deferred_release = false;
		RetainedSourceKey source = {};
		AllocationRef allocations[kArenaCount];
		std::vector<FaceRecord> faces;
		TextureVersionId terrain_base = kInvalidId;
		TextureVersionId terrain_lightmap = kInvalidId;
		uint32_t terrain_maximum_output_vertices = 0;
		uint64_t last_use_timeline = 0;
	};

	struct PendingCopy
	{
		MeshHandle owner = { kInvalidId, 0 };
		AllocationRef destination;
		std::vector<uint8_t> bytes;
		bool state_prepared = false;
	};

	struct RetiredRange
	{
		AllocationRef allocation;
		uint64_t retire_after = 0;
	};

	ResourceAllocator *allocator = nullptr;
	FrameScheduler *frames = nullptr;
	ResourceStateTracker *state_tracker = nullptr;
	RetainedWorldConfig config;
	Arena arenas[kArenaCount];
	std::vector<MeshSlot> meshes;
	std::vector<uint32_t> free_mesh_ids;
	std::vector<PendingCopy> pending_copies;
	std::vector<RetiredRange> retired_ranges;
	uint32_t recorded_copy_count = 0;
	bool uploads_recorded = false;
	bool ready = false;

	Arena &GetArena(ArenaKind kind)
	{
		return arenas[static_cast<uint32_t>(kind)];
	}

	const Arena &GetArena(ArenaKind kind) const
	{
		return arenas[static_cast<uint32_t>(kind)];
	}

	ArenaShard *FindShard(ArenaKind kind, uint32_t generation)
	{
		Arena &arena = GetArena(kind);
		for (ArenaShard &shard : arena.shards)
			if (shard.generation == generation && shard.virtual_block != VK_NULL_HANDLE)
				return &shard;
		return nullptr;
	}

	const ArenaShard *FindShard(ArenaKind kind, uint32_t generation) const
	{
		const Arena &arena = GetArena(kind);
		for (const ArenaShard &shard : arena.shards)
			if (shard.generation == generation && shard.virtual_block != VK_NULL_HANDLE)
				return &shard;
		return nullptr;
	}

	void ConfigureArena(ArenaKind kind, VkDeviceSize stride,
		VkDeviceSize initial_bytes, VkBufferUsageFlags usage)
	{
		Arena &arena = GetArena(kind);
		arena.kind = kind;
		arena.element_size = stride;
		arena.initial_bytes = initial_bytes;
		arena.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		arena.next_generation = 1;
	}

	void ConfigureArenas()
	{
		const VkBufferUsageFlags storage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		ConfigureArena(ArenaKind::Vertex, sizeof(BaseVertex),
			config.initial_vertex_bytes,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | storage);
		ConfigureArena(ArenaKind::Index, sizeof(uint32_t),
			config.initial_index_bytes,
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | storage);
		ConfigureArena(ArenaKind::Perspective, sizeof(PerspectiveVertexPayload),
			config.initial_perspective_bytes, storage);
		ConfigureArena(ArenaKind::Motion, sizeof(MotionVertexPayload),
			config.initial_motion_bytes, storage);
		ConfigureArena(ArenaKind::Specular, sizeof(SpecularVertexPayload),
			config.initial_specular_bytes, storage);
		ConfigureArena(ArenaKind::TerrainCell, sizeof(TerrainEmitterCell),
			config.initial_terrain_cell_bytes, storage);
		ConfigureArena(ArenaKind::TerrainWork, sizeof(TerrainWorkItem),
			config.initial_terrain_work_bytes, storage);
		ConfigureArena(ArenaKind::TerrainBatch, sizeof(TerrainBatchInput),
			config.initial_terrain_batch_bytes, storage);
		ConfigureArena(ArenaKind::TerrainIndirect, sizeof(TerrainIndirectCommand),
			config.initial_terrain_indirect_bytes,
			storage | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
		VkPhysicalDeviceProperties properties = {};
		vkGetPhysicalDeviceProperties(allocator->PhysicalDevice(), &properties);
		const VkDeviceSize storage_alignment = std::max<VkDeviceSize>(1,
			properties.limits.minStorageBufferOffsetAlignment);
		for (uint32_t value = static_cast<uint32_t>(ArenaKind::TerrainCell);
			value <= static_cast<uint32_t>(ArenaKind::TerrainIndirect); ++value)
		{
			Arena &arena = arenas[value];
			// All frozen terrain strides divide the Vulkan power-of-two storage
			// alignment. Express the byte constraint in virtual-block elements.
			arena.element_alignment = std::max<VkDeviceSize>(1,
				storage_alignment / arena.element_size);
		}
	}

	bool CreateShard(Arena *arena, uint32_t required_elements,
		ArenaShard **out_shard)
	{
		if (!arena || !out_shard || required_elements == 0 ||
			arena->element_size == 0 || config.maximum_shard_bytes == 0)
			return false;
		VkDeviceSize required_bytes = 0;
		if (!MultiplySize(required_elements, arena->element_size,
			&required_bytes) || required_bytes > config.maximum_shard_bytes)
			return false;

		VkDeviceSize target_bytes = std::max(arena->initial_bytes,
			required_bytes);
		for (const ArenaShard &old : arena->shards)
		{
			if (old.buffer.Valid() && old.buffer.size <=
				std::numeric_limits<VkDeviceSize>::max() / 2)
				target_bytes = std::max(target_bytes, old.buffer.size * 2);
		}
		target_bytes = std::min(target_bytes, config.maximum_shard_bytes);
		const VkDeviceSize element_capacity = target_bytes / arena->element_size;
		if (element_capacity < required_elements || element_capacity > UINT32_MAX)
			return false;

		BufferCreateRequest request = {};
		request.size = element_capacity * arena->element_size;
		request.usage = arena->usage;
		request.memory_class = BufferMemoryClass::DeviceLocal;
		request.minimum_alignment = 16;
		request.debug_name = ArenaName(arena->kind);
		ArenaShard shard;
		if (!allocator->CreateBuffer(request, &shard.buffer))
			return false;

		VmaVirtualBlockCreateInfo block_info = {};
		block_info.size = element_capacity;
		if (vmaCreateVirtualBlock(&block_info, &shard.virtual_block) != VK_SUCCESS)
		{
			allocator->RetireBuffer(&shard.buffer, 0);
			allocator->Reclaim(0);
			return false;
		}
		if (arena->next_generation == 0)
		{
			vmaDestroyVirtualBlock(shard.virtual_block);
			allocator->RetireBuffer(&shard.buffer, 0);
			allocator->Reclaim(0);
			return false;
		}
		shard.generation = arena->next_generation++;
		shard.element_capacity = element_capacity;
		try
		{
			arena->shards.push_back(shard);
		}
		catch (const std::bad_alloc &)
		{
			vmaDestroyVirtualBlock(shard.virtual_block);
			allocator->RetireBuffer(&shard.buffer, 0);
			allocator->Reclaim(0);
			return false;
		}
		*out_shard = &arena->shards.back();
		return true;
	}

	bool Allocate(ArenaKind kind, uint32_t count, AllocationRef *out)
	{
		if (!out || count == 0)
			return false;
		Arena &arena = GetArena(kind);
		ArenaShard *selected = nullptr;
		VmaVirtualAllocation allocation = VK_NULL_HANDLE;
		VkDeviceSize element_offset = 0;
		VmaVirtualAllocationCreateInfo allocation_info = {};
		allocation_info.size = count;
		allocation_info.alignment = arena.element_alignment;
		for (auto it = arena.shards.rbegin(); it != arena.shards.rend(); ++it)
		{
			if (it->virtual_block != VK_NULL_HANDLE &&
				vmaVirtualAllocate(it->virtual_block, &allocation_info,
					&allocation, &element_offset) == VK_SUCCESS)
			{
				selected = &*it;
				break;
			}
		}
		if (!selected)
		{
			if (!CreateShard(&arena, count, &selected) ||
				vmaVirtualAllocate(selected->virtual_block, &allocation_info,
					&allocation, &element_offset) != VK_SUCCESS)
				return false;
		}
		if (element_offset > UINT32_MAX ||
			element_offset > selected->element_capacity - count)
		{
			vmaVirtualFree(selected->virtual_block, allocation);
			return false;
		}
		VkDeviceSize byte_size = 0;
		if (!MultiplySize(count, arena.element_size, &byte_size))
		{
			vmaVirtualFree(selected->virtual_block, allocation);
			return false;
		}
		out->arena = kind;
		out->shard_generation = selected->generation;
		out->allocation = allocation;
		out->byte_offset = element_offset * arena.element_size;
		out->byte_size = byte_size;
		out->first_element = static_cast<uint32_t>(element_offset);
		out->element_count = count;
		++selected->allocation_count;
		selected->live_bytes += byte_size;
		return true;
	}

	void FreeNow(AllocationRef *allocation)
	{
		if (!allocation || !allocation->Valid())
			return;
		ArenaShard *shard = FindShard(allocation->arena,
			allocation->shard_generation);
		if (shard)
		{
			vmaVirtualFree(shard->virtual_block, allocation->allocation);
			if (shard->allocation_count)
				--shard->allocation_count;
			shard->live_bytes = allocation->byte_size <= shard->live_bytes ?
				shard->live_bytes - allocation->byte_size : 0;
		}
		*allocation = AllocationRef();
	}

	MeshSlot *FindMesh(MeshHandle handle)
	{
		if (handle.id >= meshes.size())
			return nullptr;
		MeshSlot &slot = meshes[handle.id];
		return slot.alive && slot.generation == handle.generation ? &slot : nullptr;
	}

	const MeshSlot *FindMesh(MeshHandle handle) const
	{
		if (handle.id >= meshes.size())
			return nullptr;
		const MeshSlot &slot = meshes[handle.id];
		return slot.alive && slot.generation == handle.generation ? &slot : nullptr;
	}

	bool AllocateMeshSlot(uint32_t *id, uint32_t *generation)
	{
		if (!id || !generation)
			return false;
		while (!free_mesh_ids.empty())
		{
			const uint32_t candidate = free_mesh_ids.back();
			free_mesh_ids.pop_back();
			MeshSlot &slot = meshes[candidate];
			if (slot.generation == UINT32_MAX)
				continue;
			*id = candidate;
			*generation = slot.generation + 1;
			return true;
		}
		if (meshes.size() >= kInvalidId)
			return false;
		try
		{
			meshes.emplace_back();
		}
		catch (const std::bad_alloc &)
		{
			return false;
		}
		*id = static_cast<uint32_t>(meshes.size() - 1);
		*generation = 1;
		return true;
	}

	void ReturnMeshSlot(uint32_t id)
	{
		if (id >= meshes.size())
			return;
		MeshSlot &slot = meshes[id];
		slot.alive = false;
		slot.terrain = false;
		slot.upload_pending = false;
		slot.deferred_release = false;
		slot.faces.clear();
		if (free_mesh_ids.size() >= free_mesh_ids.capacity() && frames &&
			frames->Current() && frames->Current()->recording)
			return;
		try
		{
			free_mesh_ids.push_back(id);
		}
		catch (const std::bad_alloc &)
		{
			// Losing one reusable ID is safe. The slot remains invalid.
		}
	}

	bool OwnerHasRecordedCopy(MeshHandle owner) const
	{
		for (uint32_t i = 0; i < recorded_copy_count; ++i)
			if (SameHandle(pending_copies[i].owner, owner))
				return true;
		return false;
	}

	void CancelPendingCopies(MeshHandle owner)
	{
		pending_copies.erase(std::remove_if(pending_copies.begin(),
			pending_copies.end(), [owner](const PendingCopy &copy) {
				return SameHandle(copy.owner, owner);
			}), pending_copies.end());
	}

	bool EnsureRetirementCapacity(const MeshSlot &slot)
	{
		size_t additional = 0;
		for (const AllocationRef &allocation : slot.allocations)
			if (allocation.Valid())
				++additional;
		if (additional > retired_ranges.max_size() - retired_ranges.size())
			return false;
		const size_t required = retired_ranges.size() + additional;
		if (required <= retired_ranges.capacity())
			return true;
		if (frames && frames->Current() && frames->Current()->recording)
			return false;
		try
		{
			retired_ranges.reserve(required);
		}
		catch (const std::bad_alloc &)
		{
			return false;
		}
		return true;
	}

	bool ScheduleSlotRanges(MeshSlot *slot, uint64_t timeline)
	{
		if (!slot)
			return false;
		if (!EnsureRetirementCapacity(*slot))
			return false;
		for (AllocationRef &allocation : slot->allocations)
		{
			if (!allocation.Valid())
				continue;
			retired_ranges.push_back({ allocation, timeline });
			allocation = AllocationRef();
		}
		return true;
	}

	RetainedBufferReference BufferReference(const AllocationRef &allocation) const
	{
		RetainedBufferReference result;
		if (!allocation.Valid())
			return result;
		const ArenaShard *shard = FindShard(allocation.arena,
			allocation.shard_generation);
		if (!shard)
			return result;
		result.buffer = shard->buffer.handle;
		result.byte_offset = allocation.byte_offset;
		result.byte_size = allocation.byte_size;
		result.first_element = allocation.first_element;
		result.element_count = allocation.element_count;
		result.pool_generation = allocation.shard_generation;
		return result;
	}

	template <typename T>
	static bool MakePendingCopy(MeshHandle owner,
		const AllocationRef &destination, const T *source, uint32_t count,
		PendingCopy *copy)
	{
		if (!copy || !destination.Valid() || !source || count == 0 ||
			destination.byte_size != VkDeviceSize(count) * sizeof(T) ||
			destination.byte_size > SIZE_MAX)
			return false;
		copy->owner = owner;
		copy->destination = destination;
		try
		{
			copy->bytes.resize(static_cast<size_t>(destination.byte_size));
		}
		catch (const std::bad_alloc &)
		{
			return false;
		}
		std::memcpy(copy->bytes.data(), source, copy->bytes.size());
		return true;
	}
};

namespace
{

bool ValidateMeshUpload(const RetainedMeshUpload &upload,
	const RetainedFaceRangeUpload *faces, uint32_t face_count)
{
	if (!ValidSource(upload.source) ||
		upload.source.kind == RetainedSourceKind::Terrain ||
		upload.vertex_count == 0 || !upload.vertices ||
		upload.index_count == 0 || !upload.indices ||
		upload.index_count % 3 != 0 || face_count == 0 || !faces)
		return false;
	if ((upload.perspective_payload_count != 0) !=
		(upload.perspective_payload != nullptr) ||
		(upload.motion_payload_count != 0) !=
		(upload.motion_payload != nullptr) ||
		(upload.specular_payload_count != 0) !=
		(upload.specular_payload != nullptr))
		return false;
	for (uint32_t i = 0; i < upload.index_count; ++i)
		if (upload.indices[i] >= upload.vertex_count)
			return false;
	for (uint32_t i = 0; i < face_count; ++i)
	{
		const RetainedFaceRangeUpload &face = faces[i];
		if (!SameSource(face.token.source, upload.source) ||
			face.index_count == 0 || face.index_count % 3 != 0 ||
			face.first_index > upload.index_count ||
			face.index_count > upload.index_count - face.first_index ||
			!SpanFits(face.perspective_payload,
				upload.perspective_payload_count) ||
			!SpanFits(face.motion_payload, upload.motion_payload_count) ||
			!SpanFits(face.specular_payload, upload.specular_payload_count))
			return false;
		for (uint32_t local = 0; local < face.index_count; ++local)
		{
			const int64_t vertex = int64_t(face.base_vertex) +
				upload.indices[face.first_index + local];
			if (vertex < 0 || vertex >= upload.vertex_count)
				return false;
		}
		for (uint32_t previous = 0; previous < i; ++previous)
			if (SameToken(face.token, faces[previous].token))
				return false;
	}
	return true;
}

bool ValidateTerrainUpload(const RetainedTerrainUpload &upload,
	uint32_t *maximum_output_vertices)
{
	if (!maximum_output_vertices || !ValidSource(upload.source) ||
		upload.source.kind != RetainedSourceKind::Terrain ||
		upload.cell_count == 0 || !upload.cells ||
		upload.full_draw_work_count == 0 || !upload.full_draw_work ||
		upload.batch_count == 0 || !upload.batches ||
		upload.base_texture_array == kInvalidId ||
		upload.lightmap_array == kInvalidId)
		return false;
	try
	{
		std::vector<uint8_t> seen_cells(upload.cell_count, 0);
		std::vector<uint8_t> seen_orders(upload.full_draw_work_count, 0);
		uint32_t expected_work_first = 0;
		uint32_t expected_output_first = 0;
		int32_t previous_source_texture = -1;
		for (uint32_t batch_index = 0; batch_index < upload.batch_count;
			++batch_index)
		{
			const TerrainBatchInput &batch = upload.batches[batch_index];
			const uint64_t capacity = uint64_t(batch.work_item_count) *
				kTerrainMaximumOutputVerticesPerCell;
			if (batch.reserved0 != 0 || batch.source_texture < 0 ||
				(batch_index != 0 &&
				 batch.source_texture <= previous_source_texture) ||
				batch.texture_layer != batch_index ||
				batch.indirect_command_index != batch_index ||
				batch.first_work_item != expected_work_first ||
				batch.work_item_count == 0 ||
				batch.work_item_count >
					upload.full_draw_work_count - expected_work_first ||
				capacity > UINT32_MAX ||
				batch.first_output_vertex != expected_output_first ||
				batch.output_vertex_capacity != static_cast<uint32_t>(capacity) ||
				batch.output_vertex_capacity >
					UINT32_MAX - expected_output_first)
				return false;
			previous_source_texture = batch.source_texture;

			uint32_t previous_page = 0;
			uint32_t previous_segment = 0;
			bool have_previous = false;
			for (uint32_t local = 0; local < batch.work_item_count; ++local)
			{
				const TerrainWorkItem &work =
					upload.full_draw_work[batch.first_work_item + local];
				if (work.cell_index >= upload.cell_count ||
					seen_cells[work.cell_index] ||
					work.source_texture != batch.source_texture ||
					(work.source_flags & ~kTerrainCellAllFlags) != 0 ||
					work.full_draw_order >= upload.full_draw_work_count ||
					seen_orders[work.full_draw_order])
					return false;
				seen_cells[work.cell_index] = 1;
				seen_orders[work.full_draw_order] = 1;
				const TerrainEmitterCell &cell = upload.cells[work.cell_index];
				const uint32_t segment = cell.packed[0];
				const uint32_t page = (cell.packed[1] &
					kTerrainLightmapPageMask) >> kTerrainLightmapPageShift;
				const uint32_t layer = (cell.packed[1] &
					kTerrainTextureLayerMask) >> kTerrainTextureLayerShift;
				if ((segment & ~(kTerrainSegmentXMask |
						kTerrainSegmentZMask)) != 0 ||
					page >= kTerrainLightmapPageCount ||
					layer != batch.texture_layer ||
					cell.packed[2] != batch_index ||
					cell.packed[3] != batch.first_output_vertex ||
					(have_previous && (page < previous_page ||
						(page == previous_page && segment <= previous_segment))))
					return false;
				for (float height : cell.height)
					if (!std::isfinite(height))
						return false;
				previous_page = page;
				previous_segment = segment;
				have_previous = true;
			}
			expected_work_first += batch.work_item_count;
			expected_output_first += batch.output_vertex_capacity;
		}
		if (expected_work_first != upload.full_draw_work_count)
			return false;
		*maximum_output_vertices = expected_output_first;
		return true;
	}
	catch (const std::bad_alloc &)
	{
		return false;
	}
}

} // namespace

RetainedWorld::RetainedWorld() : impl_(new (std::nothrow) Impl())
{
}

RetainedWorld::~RetainedWorld()
{
	Shutdown(false);
	delete impl_;
}

bool RetainedWorld::Initialize(ResourceAllocator *allocator,
	FrameScheduler *frames, ResourceStateTracker *state_tracker,
	const RetainedWorldConfig &config)
{
	if (!impl_ || impl_->ready || !allocator || !allocator->Ready() || !frames ||
		!frames->Ready() || !state_tracker || config.maximum_shard_bytes == 0)
		return false;
	impl_->allocator = allocator;
	impl_->frames = frames;
	impl_->state_tracker = state_tracker;
	impl_->config = config;
	impl_->ConfigureArenas();
	impl_->ready = true;
	return true;
}

void RetainedWorld::Shutdown(bool device_lost) noexcept
{
	if (!impl_ || !impl_->ready)
		return;
	impl_->uploads_recorded = false;
	impl_->recorded_copy_count = 0;
	impl_->pending_copies.clear();
	for (uint32_t i = 0; i < impl_->meshes.size(); ++i)
	{
		Impl::MeshSlot &slot = impl_->meshes[i];
		if (!impl_->ScheduleSlotRanges(&slot, slot.last_use_timeline))
		{
			// Shutdown is an ownership boundary. Freeing these CPU-side virtual
			// records here avoids losing their physical-buffer suballocations if
			// the death-row vector cannot grow.
			for (Impl::AllocationRef &allocation : slot.allocations)
				impl_->FreeNow(&allocation);
		}
		slot.alive = false;
		slot.deferred_release = false;
	}
	Reclaim(UINT64_MAX);
	for (uint32_t arena_index = 0; arena_index < kArenaCount; ++arena_index)
	{
		Impl::Arena &arena = impl_->arenas[arena_index];
		for (Impl::ArenaShard &shard : arena.shards)
		{
			if (shard.virtual_block)
			{
				vmaClearVirtualBlock(shard.virtual_block);
				vmaDestroyVirtualBlock(shard.virtual_block);
				shard.virtual_block = VK_NULL_HANDLE;
			}
			if (shard.buffer.Valid())
				impl_->allocator->RetireBuffer(&shard.buffer,
					device_lost ? 0 : shard.buffer.last_use_timeline);
		}
		arena.shards.clear();
	}
	impl_->allocator->Reclaim(UINT64_MAX);
	impl_->meshes.clear();
	impl_->free_mesh_ids.clear();
	impl_->retired_ranges.clear();
	impl_->allocator = nullptr;
	impl_->frames = nullptr;
	impl_->state_tracker = nullptr;
	impl_->ready = false;
}

bool RetainedWorld::Ready() const noexcept
{
	return impl_ && impl_->ready;
}

bool RetainedWorld::CreateMesh(const RetainedMeshUpload &upload,
	const RetainedFaceRangeUpload *face_ranges, uint32_t face_range_count,
	MeshHandle *out_handle)
{
	if (out_handle)
		*out_handle = { kInvalidId, 0 };
	if (!Ready() || !out_handle || impl_->uploads_recorded ||
		(impl_->frames->Current() && impl_->frames->Current()->recording) ||
		!ValidateMeshUpload(upload, face_ranges, face_range_count))
		return false;
	// A source is commonly streamed through several ordered material batches.
	// Permit those batches to own separate retained allocations, but never
	// permit two live allocations to publish the same face token.  ResolveFace
	// therefore remains deterministic while ReleaseSource can still invalidate
	// every allocation belonging to one source generation.
	for (const Impl::MeshSlot &slot : impl_->meshes)
	{
		if (!slot.alive || slot.terrain ||
			!SameSource(slot.source, upload.source))
			continue;
		for (const Impl::FaceRecord &existing : slot.faces)
			for (uint32_t incoming = 0; incoming < face_range_count; ++incoming)
				if (SameToken(existing.token, face_ranges[incoming].token))
					return false;
	}

	Impl::AllocationRef allocations[kArenaCount];
	auto allocate_optional = [&](ArenaKind kind, uint32_t count) {
		return count == 0 || impl_->Allocate(kind, count,
			&allocations[static_cast<uint32_t>(kind)]);
	};
	if (!allocate_optional(ArenaKind::Vertex, upload.vertex_count) ||
		!allocate_optional(ArenaKind::Index, upload.index_count) ||
		!allocate_optional(ArenaKind::Perspective,
			upload.perspective_payload_count) ||
		!allocate_optional(ArenaKind::Motion, upload.motion_payload_count) ||
		!allocate_optional(ArenaKind::Specular,
			upload.specular_payload_count))
	{
		for (Impl::AllocationRef &allocation : allocations)
			impl_->FreeNow(&allocation);
		return false;
	}

	uint32_t slot_id = kInvalidId;
	uint32_t slot_generation = 0;
	if (!impl_->AllocateMeshSlot(&slot_id, &slot_generation))
	{
		for (Impl::AllocationRef &allocation : allocations)
			impl_->FreeNow(&allocation);
		return false;
	}
	const MeshHandle handle = { slot_id, slot_generation };
	std::vector<Impl::FaceRecord> faces;
	std::vector<Impl::PendingCopy> copies;
	try
	{
		faces.reserve(face_range_count);
		copies.reserve(5);
	}
	catch (const std::bad_alloc &)
	{
		impl_->ReturnMeshSlot(slot_id);
		for (Impl::AllocationRef &allocation : allocations)
			impl_->FreeNow(&allocation);
		return false;
	}
	for (uint32_t i = 0; i < face_range_count; ++i)
	{
		const RetainedFaceRangeUpload &source = face_ranges[i];
		Impl::FaceRecord face = {};
		face.token = source.token;
		face.range.mesh = handle;
		const uint32_t index_base =
			allocations[static_cast<uint32_t>(ArenaKind::Index)].first_element;
		if (index_base > UINT32_MAX - source.first_index)
			goto create_mesh_failure;
		face.range.first_index = index_base + source.first_index;
		face.range.index_count = source.index_count;
		const int64_t base_vertex = int64_t(allocations[
			static_cast<uint32_t>(ArenaKind::Vertex)].first_element) +
			source.base_vertex;
		if (base_vertex < INT32_MIN || base_vertex > INT32_MAX)
			goto create_mesh_failure;
		face.range.base_vertex = static_cast<int32_t>(base_vertex);
		if (!AddSpanBase(source.perspective_payload, allocations[
				static_cast<uint32_t>(ArenaKind::Perspective)].first_element,
			&face.range.perspective_payload) ||
			!AddSpanBase(source.motion_payload, allocations[
				static_cast<uint32_t>(ArenaKind::Motion)].first_element,
				&face.range.motion_payload) ||
			!AddSpanBase(source.specular_payload, allocations[
				static_cast<uint32_t>(ArenaKind::Specular)].first_element,
				&face.range.specular_payload))
			goto create_mesh_failure;
		faces.push_back(face);
	}
	std::sort(faces.begin(), faces.end(), [](const Impl::FaceRecord &left,
		const Impl::FaceRecord &right) { return TokenLess(left.token, right.token); });

	{
		Impl::PendingCopy copy;
		if (!Impl::MakePendingCopy(handle, allocations[
				static_cast<uint32_t>(ArenaKind::Vertex)], upload.vertices,
				upload.vertex_count, &copy))
			goto create_mesh_failure;
		copies.push_back(std::move(copy));
	}
	{
		Impl::PendingCopy copy;
		if (!Impl::MakePendingCopy(handle, allocations[
				static_cast<uint32_t>(ArenaKind::Index)], upload.indices,
				upload.index_count, &copy))
			goto create_mesh_failure;
		copies.push_back(std::move(copy));
	}
#define PICCU_RETAINED_OPTIONAL_COPY(kind_value, pointer_value, count_value) \
	do { if ((count_value) != 0) { Impl::PendingCopy copy; \
		if (!Impl::MakePendingCopy(handle, allocations[static_cast<uint32_t>(kind_value)], \
			(pointer_value), (count_value), &copy)) goto create_mesh_failure; \
		copies.push_back(std::move(copy)); } } while (0)
	PICCU_RETAINED_OPTIONAL_COPY(ArenaKind::Perspective,
		upload.perspective_payload, upload.perspective_payload_count);
	PICCU_RETAINED_OPTIONAL_COPY(ArenaKind::Motion,
		upload.motion_payload, upload.motion_payload_count);
	PICCU_RETAINED_OPTIONAL_COPY(ArenaKind::Specular,
		upload.specular_payload, upload.specular_payload_count);
#undef PICCU_RETAINED_OPTIONAL_COPY

	try
	{
		impl_->pending_copies.reserve(impl_->pending_copies.size() + copies.size());
	}
	catch (const std::bad_alloc &)
	{
		goto create_mesh_failure;
	}
	{
		Impl::MeshSlot &slot = impl_->meshes[slot_id];
		slot.generation = slot_generation;
		slot.alive = true;
		slot.terrain = false;
		slot.upload_pending = true;
		slot.deferred_release = false;
		slot.source = upload.source;
		for (uint32_t i = 0; i < kArenaCount; ++i)
		{
			slot.allocations[i] = allocations[i];
			allocations[i] = Impl::AllocationRef();
		}
		slot.faces = std::move(faces);
		slot.last_use_timeline = 0;
	}
	for (Impl::PendingCopy &copy : copies)
		impl_->pending_copies.push_back(std::move(copy));
	*out_handle = handle;
	return true;

create_mesh_failure:
	impl_->ReturnMeshSlot(slot_id);
	for (Impl::AllocationRef &allocation : allocations)
		impl_->FreeNow(&allocation);
	return false;
}

bool RetainedWorld::ReplaceMesh(MeshHandle old_handle,
	const RetainedMeshUpload &upload,
	const RetainedFaceRangeUpload *face_ranges, uint32_t face_range_count,
	MeshHandle *out_handle)
{
	if (!out_handle)
		return false;
	*out_handle = { kInvalidId, 0 };
	const Impl::MeshSlot *old = impl_ ? impl_->FindMesh(old_handle) : nullptr;
	if (!old || old->terrain || !SameSourceIdentity(old->source, upload.source) ||
		upload.source.source_generation <= old->source.source_generation ||
		!impl_->EnsureRetirementCapacity(*old))
		return false;
	MeshHandle replacement = { kInvalidId, 0 };
	if (!CreateMesh(upload, face_ranges, face_range_count, &replacement))
		return false;
	ReleaseMesh(old_handle);
	*out_handle = replacement;
	return true;
}

bool RetainedWorld::ResolveFace(const RetainedFaceToken &token,
	RetainedRange *out_range) const
{
	if (out_range)
		*out_range = {};
	if (!Ready() || !out_range || !ValidSource(token.source))
		return false;
	for (const Impl::MeshSlot &slot : impl_->meshes)
	{
		if (!slot.alive || slot.terrain || !SameSource(slot.source, token.source))
			continue;
		auto found = std::lower_bound(slot.faces.begin(), slot.faces.end(), token,
			[](const Impl::FaceRecord &face, const RetainedFaceToken &value) {
				return TokenLess(face.token, value);
			});
		if (found != slot.faces.end() && SameToken(found->token, token))
		{
			*out_range = found->range;
			return true;
		}
	}
	return false;
}

bool RetainedWorld::CreateTerrain(const RetainedTerrainUpload &upload,
	MeshHandle *out_handle)
{
	if (out_handle)
		*out_handle = { kInvalidId, 0 };
	uint32_t maximum_output = 0;
	if (!Ready() || !out_handle || impl_->uploads_recorded ||
		(impl_->frames->Current() && impl_->frames->Current()->recording) ||
		!ValidateTerrainUpload(upload, &maximum_output))
		return false;
	for (const Impl::MeshSlot &slot : impl_->meshes)
		if (slot.alive && SameSource(slot.source, upload.source))
			return false;

	Impl::AllocationRef allocations[kArenaCount];
	if (!impl_->Allocate(ArenaKind::TerrainCell, upload.cell_count,
			&allocations[static_cast<uint32_t>(ArenaKind::TerrainCell)]) ||
		!impl_->Allocate(ArenaKind::TerrainWork, upload.full_draw_work_count,
			&allocations[static_cast<uint32_t>(ArenaKind::TerrainWork)]) ||
		!impl_->Allocate(ArenaKind::TerrainBatch, upload.batch_count,
			&allocations[static_cast<uint32_t>(ArenaKind::TerrainBatch)]) ||
		!impl_->Allocate(ArenaKind::TerrainIndirect, upload.batch_count,
			&allocations[static_cast<uint32_t>(ArenaKind::TerrainIndirect)]))
	{
		for (Impl::AllocationRef &allocation : allocations)
			impl_->FreeNow(&allocation);
		return false;
	}

	uint32_t slot_id = kInvalidId;
	uint32_t slot_generation = 0;
	if (!impl_->AllocateMeshSlot(&slot_id, &slot_generation))
	{
		for (Impl::AllocationRef &allocation : allocations)
			impl_->FreeNow(&allocation);
		return false;
	}
	const MeshHandle handle = { slot_id, slot_generation };
	std::vector<TerrainIndirectCommand> indirect;
	std::vector<Impl::PendingCopy> copies;
	try
	{
		indirect.resize(upload.batch_count);
		copies.reserve(4);
	}
	catch (const std::bad_alloc &)
	{
		impl_->ReturnMeshSlot(slot_id);
		for (Impl::AllocationRef &allocation : allocations)
			impl_->FreeNow(&allocation);
		return false;
	}
	for (uint32_t i = 0; i < upload.batch_count; ++i)
	{
		indirect[i].vertex_count = 0;
		indirect[i].instance_count = 1;
		indirect[i].first_vertex = upload.batches[i].first_output_vertex;
		indirect[i].first_instance = 0;
	}

#define PICCU_RETAINED_TERRAIN_COPY(kind_value, pointer_value, count_value) \
	do { Impl::PendingCopy copy; \
		if (!Impl::MakePendingCopy(handle, allocations[static_cast<uint32_t>(kind_value)], \
			(pointer_value), (count_value), &copy)) goto create_terrain_failure; \
		copies.push_back(std::move(copy)); } while (0)
	PICCU_RETAINED_TERRAIN_COPY(ArenaKind::TerrainCell,
		upload.cells, upload.cell_count);
	PICCU_RETAINED_TERRAIN_COPY(ArenaKind::TerrainWork,
		upload.full_draw_work, upload.full_draw_work_count);
	PICCU_RETAINED_TERRAIN_COPY(ArenaKind::TerrainBatch,
		upload.batches, upload.batch_count);
	PICCU_RETAINED_TERRAIN_COPY(ArenaKind::TerrainIndirect,
		indirect.data(), upload.batch_count);
#undef PICCU_RETAINED_TERRAIN_COPY

	try
	{
		impl_->pending_copies.reserve(impl_->pending_copies.size() + copies.size());
	}
	catch (const std::bad_alloc &)
	{
		goto create_terrain_failure;
	}
	{
		Impl::MeshSlot &slot = impl_->meshes[slot_id];
		slot.generation = slot_generation;
		slot.alive = true;
		slot.terrain = true;
		slot.upload_pending = true;
		slot.deferred_release = false;
		slot.source = upload.source;
		for (uint32_t i = 0; i < kArenaCount; ++i)
		{
			slot.allocations[i] = allocations[i];
			allocations[i] = Impl::AllocationRef();
		}
		slot.terrain_base = upload.base_texture_array;
		slot.terrain_lightmap = upload.lightmap_array;
		slot.terrain_maximum_output_vertices = maximum_output;
		slot.last_use_timeline = 0;
	}
	for (Impl::PendingCopy &copy : copies)
		impl_->pending_copies.push_back(std::move(copy));
	*out_handle = handle;
	return true;

create_terrain_failure:
	impl_->ReturnMeshSlot(slot_id);
	for (Impl::AllocationRef &allocation : allocations)
		impl_->FreeNow(&allocation);
	return false;
}

bool RetainedWorld::ReplaceTerrain(MeshHandle old_handle,
	const RetainedTerrainUpload &upload, MeshHandle *out_handle)
{
	if (!out_handle)
		return false;
	*out_handle = { kInvalidId, 0 };
	const Impl::MeshSlot *old = impl_ ? impl_->FindMesh(old_handle) : nullptr;
	if (!old || !old->terrain || !SameSourceIdentity(old->source, upload.source) ||
		upload.source.source_generation <= old->source.source_generation ||
		!impl_->EnsureRetirementCapacity(*old))
		return false;
	MeshHandle replacement = { kInvalidId, 0 };
	if (!CreateTerrain(upload, &replacement))
		return false;
	ReleaseMesh(old_handle);
	*out_handle = replacement;
	return true;
}

void RetainedWorld::ReleaseMesh(MeshHandle handle)
{
	if (!Ready())
		return;
	Impl::MeshSlot *slot = impl_->FindMesh(handle);
	if (!slot)
		return;
	if (!impl_->EnsureRetirementCapacity(*slot))
		return;
	if (impl_->uploads_recorded && impl_->OwnerHasRecordedCopy(handle))
	{
		slot->alive = false;
		slot->deferred_release = true;
		slot->faces.clear();
		return;
	}
	impl_->CancelPendingCopies(handle);
	if (!impl_->ScheduleSlotRanges(slot, slot->last_use_timeline))
		return;
	impl_->ReturnMeshSlot(handle.id);
}

void RetainedWorld::ReleaseSource(const RetainedSourceKey &source)
{
	if (!Ready() || !ValidSource(source))
		return;
	for (uint32_t id = 0; id < impl_->meshes.size(); ++id)
	{
		const Impl::MeshSlot &slot = impl_->meshes[id];
		if (slot.alive && SameSource(slot.source, source))
		{
			const MeshHandle handle = { id, slot.generation };
			ReleaseMesh(handle);
		}
	}
}

void RetainedWorld::ResetAll()
{
	if (!Ready())
		return;
	for (uint32_t id = 0; id < impl_->meshes.size(); ++id)
	{
		const Impl::MeshSlot &slot = impl_->meshes[id];
		if (slot.alive)
		{
			const MeshHandle handle = { id, slot.generation };
			ReleaseMesh(handle);
		}
	}
}

bool RetainedWorld::AccumulateFrameRequirements(
	FrameRequirements *requirements) const
{
	if (!Ready() || !requirements)
		return false;
	VkDeviceSize required = requirements->upload_bytes;
	for (const Impl::PendingCopy &copy : impl_->pending_copies)
	{
		if (!AlignSize(required, 16, &required) ||
			!AddSize(required, copy.destination.byte_size, &required))
			return false;
	}
	requirements->upload_bytes = required;
	return true;
}

bool RetainedWorld::PreflightReserve(
	const FrameRequirements &combined_without_retained)
{
	if (!Ready() || impl_->uploads_recorded ||
		(impl_->frames->Current() && impl_->frames->Current()->recording))
		return false;
	FrameRequirements requirements = combined_without_retained;
	if (!AccumulateFrameRequirements(&requirements) ||
		!impl_->frames->Reserve(requirements))
		return false;
	size_t retirement_capacity = impl_->retired_ranges.size();
	for (const Impl::MeshSlot &slot : impl_->meshes)
		for (const Impl::AllocationRef &allocation : slot.allocations)
			if (allocation.Valid())
			{
				if (retirement_capacity == impl_->retired_ranges.max_size())
					return false;
				++retirement_capacity;
			}
	try
	{
		impl_->retired_ranges.reserve(retirement_capacity);
		impl_->free_mesh_ids.reserve(impl_->meshes.size());
	}
	catch (const std::bad_alloc &)
	{
		return false;
	}
	impl_->state_tracker->Reserve(
		static_cast<uint32_t>(std::min<size_t>(UINT32_MAX,
			impl_->pending_copies.size() * 3 + 16)), 0,
		static_cast<uint32_t>(std::min<size_t>(UINT32_MAX,
			impl_->pending_copies.size() * 3 + 16)));
	const BufferUse transfer_write = {
		VK_PIPELINE_STAGE_2_COPY_BIT,
		VK_ACCESS_2_TRANSFER_WRITE_BIT,
		VK_QUEUE_FAMILY_IGNORED,
		ResourceIntent::Write
	};
	for (Impl::PendingCopy &copy : impl_->pending_copies)
	{
		const Impl::ArenaShard *shard = impl_->FindShard(copy.destination.arena,
			copy.destination.shard_generation);
		if (!shard)
			return false;
		impl_->state_tracker->ImportBuffer(shard->buffer.handle,
			copy.destination.byte_offset, copy.destination.byte_size,
			transfer_write, 0);
		copy.state_prepared = true;
	}
	return true;
}

bool RetainedWorld::RecordPendingUploads(VkCommandBuffer command_buffer)
{
	if (!Ready() || impl_->uploads_recorded || command_buffer == VK_NULL_HANDLE)
		return false;
	FrameContext *frame = impl_->frames->Current();
	if (!frame || !frame->recording || frame->recording_command != command_buffer)
		return false;
	for (const Impl::PendingCopy &copy : impl_->pending_copies)
		if (!copy.state_prepared)
			return false;
	for (const Impl::PendingCopy &copy : impl_->pending_copies)
	{
		const Impl::ArenaShard *shard = impl_->FindShard(copy.destination.arena,
			copy.destination.shard_generation);
		if (!shard || copy.bytes.size() != copy.destination.byte_size)
			return false;
		FrameBufferSlice upload = impl_->frames->AllocateUpload(
			copy.destination.byte_size, 16);
		if (!upload.Valid() || !upload.mapped)
			return false;
		std::memcpy(upload.mapped, copy.bytes.data(), copy.bytes.size());
		if (!impl_->allocator->Flush(frame->upload, upload.offset, upload.size))
			return false;
		VkBufferCopy2 region = { VK_STRUCTURE_TYPE_BUFFER_COPY_2 };
		region.srcOffset = upload.offset;
		region.dstOffset = copy.destination.byte_offset;
		region.size = copy.destination.byte_size;
		VkCopyBufferInfo2 info = { VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2 };
		info.srcBuffer = upload.buffer;
		info.dstBuffer = shard->buffer.handle;
		info.regionCount = 1;
		info.pRegions = &region;
		vkCmdCopyBuffer2(command_buffer, &info);
	}
	impl_->recorded_copy_count = static_cast<uint32_t>(impl_->pending_copies.size());
	impl_->uploads_recorded = impl_->recorded_copy_count != 0;
	return true;
}

void RetainedWorld::CommitPendingUploads(uint64_t submission_timeline)
{
	if (!Ready() || !impl_->uploads_recorded || submission_timeline == 0)
		return;
	for (uint32_t id = 0; id < impl_->meshes.size(); ++id)
	{
		Impl::MeshSlot &slot = impl_->meshes[id];
		const MeshHandle handle = { id, slot.generation };
		if (!impl_->OwnerHasRecordedCopy(handle))
			continue;
		slot.last_use_timeline = std::max(slot.last_use_timeline,
			submission_timeline);
		for (Impl::AllocationRef &allocation : slot.allocations)
		{
			Impl::ArenaShard *shard = impl_->FindShard(allocation.arena,
				allocation.shard_generation);
			if (allocation.Valid() && shard)
				shard->buffer.last_use_timeline = std::max(
					shard->buffer.last_use_timeline, submission_timeline);
		}
		if (slot.deferred_release)
		{
			if (impl_->ScheduleSlotRanges(&slot, submission_timeline))
				impl_->ReturnMeshSlot(id);
			else
			{
				slot.alive = true;
				slot.deferred_release = false;
				slot.upload_pending = false;
			}
		}
		else
			slot.upload_pending = false;
	}
	impl_->pending_copies.erase(impl_->pending_copies.begin(),
		impl_->pending_copies.begin() + impl_->recorded_copy_count);
	impl_->recorded_copy_count = 0;
	impl_->uploads_recorded = false;
}

void RetainedWorld::AbandonPendingUploadRecording() noexcept
{
	if (!impl_)
		return;
	for (uint32_t id = 0; id < impl_->meshes.size(); ++id)
	{
		Impl::MeshSlot &slot = impl_->meshes[id];
		if (!slot.deferred_release)
			continue;
		const MeshHandle handle = { id, slot.generation };
		impl_->CancelPendingCopies(handle);
		if (impl_->ScheduleSlotRanges(&slot, slot.last_use_timeline))
			impl_->ReturnMeshSlot(id);
		else
		{
			slot.alive = true;
			slot.deferred_release = false;
		}
	}
	impl_->recorded_copy_count = 0;
	impl_->uploads_recorded = false;
}

bool RetainedWorld::GetMeshBufferReferences(MeshHandle handle,
	RetainedMeshBufferReferences *out_references) const
{
	if (out_references)
		*out_references = RetainedMeshBufferReferences();
	const Impl::MeshSlot *slot = impl_ ? impl_->FindMesh(handle) : nullptr;
	if (!slot || !out_references)
		return false;
	out_references->mesh = handle;
	out_references->source = slot->source;
	out_references->terrain = slot->terrain;
	out_references->upload_pending = slot->upload_pending;
#define PICCU_RETAINED_REF(member_name, kind_value) \
	out_references->member_name = impl_->BufferReference(slot->allocations[ \
		static_cast<uint32_t>(kind_value)])
	PICCU_RETAINED_REF(vertices, ArenaKind::Vertex);
	PICCU_RETAINED_REF(indices, ArenaKind::Index);
	PICCU_RETAINED_REF(perspective_payload, ArenaKind::Perspective);
	PICCU_RETAINED_REF(motion_payload, ArenaKind::Motion);
	PICCU_RETAINED_REF(specular_payload, ArenaKind::Specular);
	PICCU_RETAINED_REF(terrain_cells, ArenaKind::TerrainCell);
	PICCU_RETAINED_REF(terrain_work, ArenaKind::TerrainWork);
	PICCU_RETAINED_REF(terrain_batches, ArenaKind::TerrainBatch);
	PICCU_RETAINED_REF(terrain_indirect, ArenaKind::TerrainIndirect);
#undef PICCU_RETAINED_REF
	out_references->terrain_base_texture_array = slot->terrain_base;
	out_references->terrain_lightmap_array = slot->terrain_lightmap;
	out_references->terrain_maximum_output_vertices =
		slot->terrain_maximum_output_vertices;
	return true;
}

void RetainedWorld::StampUse(MeshHandle handle, uint64_t submission_timeline)
{
	if (!Ready() || submission_timeline == 0)
		return;
	Impl::MeshSlot *slot = impl_->FindMesh(handle);
	if (!slot)
		return;
	slot->last_use_timeline = std::max(slot->last_use_timeline,
		submission_timeline);
	for (Impl::AllocationRef &allocation : slot->allocations)
	{
		Impl::ArenaShard *shard = impl_->FindShard(allocation.arena,
			allocation.shard_generation);
		if (allocation.Valid() && shard)
			shard->buffer.last_use_timeline = std::max(
				shard->buffer.last_use_timeline, submission_timeline);
	}
}

void RetainedWorld::Reclaim(uint64_t completed_timeline)
{
	if (!Ready())
		return;
	auto first_live = std::remove_if(impl_->retired_ranges.begin(),
		impl_->retired_ranges.end(), [&](Impl::RetiredRange &retired) {
			if (retired.retire_after > completed_timeline)
				return false;
			impl_->FreeNow(&retired.allocation);
			return true;
		});
	impl_->retired_ranges.erase(first_live, impl_->retired_ranges.end());
	for (uint32_t arena_index = 0; arena_index < kArenaCount; ++arena_index)
	{
		Impl::Arena &arena = impl_->arenas[arena_index];
		uint32_t newest_generation = 0;
		for (const Impl::ArenaShard &shard : arena.shards)
			if (shard.virtual_block)
				newest_generation = std::max(newest_generation, shard.generation);
		for (Impl::ArenaShard &shard : arena.shards)
		{
			if (!shard.virtual_block || shard.allocation_count != 0 ||
				shard.generation == newest_generation)
				continue;
			vmaDestroyVirtualBlock(shard.virtual_block);
			shard.virtual_block = VK_NULL_HANDLE;
			impl_->allocator->RetireBuffer(&shard.buffer,
				shard.buffer.last_use_timeline);
		}
	}
	impl_->allocator->Reclaim(completed_timeline);
}

RetainedWorldStatistics RetainedWorld::Statistics() const
{
	RetainedWorldStatistics result;
	if (!Ready())
		return result;
	for (const Impl::MeshSlot &slot : impl_->meshes)
		if (slot.alive)
		{
			++result.live_meshes;
			if (slot.terrain)
				++result.live_terrains;
		}
	for (uint32_t arena_index = 0; arena_index < kArenaCount; ++arena_index)
		for (const Impl::ArenaShard &shard : impl_->arenas[arena_index].shards)
			if (shard.virtual_block)
			{
				++result.arena_shards;
				result.live_bytes += shard.live_bytes;
				result.arena_bytes += shard.buffer.size;
			}
	result.pending_copies = static_cast<uint32_t>(
		std::min<size_t>(UINT32_MAX, impl_->pending_copies.size()));
	result.retired_ranges = static_cast<uint32_t>(
		std::min<size_t>(UINT32_MAX, impl_->retired_ranges.size()));
	FrameRequirements requirements = {};
	if (AccumulateFrameRequirements(&requirements))
		result.pending_upload_bytes = requirements.upload_bytes;
	return result;
}

} // namespace vk
} // namespace render
} // namespace piccu
