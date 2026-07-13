/* Versioned device-local retained rooms, polymodels, and T2 terrain inputs. */
#pragma once

#include "../core/retained_world.h"
#include "vk_frame.h"
#include "vk_resources.h"
#include "vk_state_tracker.h"

#include <stdint.h>

namespace piccu
{
namespace render
{
namespace vk
{

struct RetainedWorldConfig
{
	VkDeviceSize initial_vertex_bytes = 32u * 1024u * 1024u;
	VkDeviceSize initial_index_bytes = 16u * 1024u * 1024u;
	VkDeviceSize initial_perspective_bytes = 8u * 1024u * 1024u;
	VkDeviceSize initial_motion_bytes = 16u * 1024u * 1024u;
	VkDeviceSize initial_specular_bytes = 32u * 1024u * 1024u;
	VkDeviceSize initial_terrain_cell_bytes = 4u * 1024u * 1024u;
	VkDeviceSize initial_terrain_work_bytes = 2u * 1024u * 1024u;
	VkDeviceSize initial_terrain_batch_bytes = 1u * 1024u * 1024u;
	VkDeviceSize initial_terrain_indirect_bytes = 256u * 1024u;
	VkDeviceSize maximum_shard_bytes = 256u * 1024u * 1024u;
};

// The byte range is the allocation owned by one mesh. first_element is the
// allocation's logical offset from the beginning of the physical shard. The
// physical buffer is bound at byte zero so retained ranges from the same shard
// can remain in one indirect run.
struct RetainedBufferReference
{
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceSize byte_offset = 0;
	VkDeviceSize byte_size = 0;
	uint32_t first_element = 0;
	uint32_t element_count = 0;
	uint32_t pool_generation = 0;

	bool Valid() const noexcept
	{
		return buffer != VK_NULL_HANDLE && byte_size != 0 && element_count != 0;
	}
};

struct RetainedMeshBufferReferences
{
	MeshHandle mesh = { kInvalidId, 0 };
	RetainedSourceKey source = {};
	bool terrain = false;
	bool upload_pending = false;
	RetainedBufferReference vertices;
	RetainedBufferReference indices;
	RetainedBufferReference perspective_payload;
	RetainedBufferReference motion_payload;
	RetainedBufferReference specular_payload;
	RetainedBufferReference terrain_cells;
	RetainedBufferReference terrain_work;
	RetainedBufferReference terrain_batches;
	RetainedBufferReference terrain_indirect;
	TextureVersionId terrain_base_texture_array = kInvalidId;
	TextureVersionId terrain_lightmap_array = kInvalidId;
	uint32_t terrain_maximum_output_vertices = 0;
};

struct RetainedWorldStatistics
{
	uint32_t live_meshes = 0;
	uint32_t live_terrains = 0;
	uint32_t arena_shards = 0;
	uint32_t pending_copies = 0;
	uint32_t retired_ranges = 0;
	VkDeviceSize live_bytes = 0;
	VkDeviceSize arena_bytes = 0;
	VkDeviceSize pending_upload_bytes = 0;
};

class RetainedWorld final : public IRetainedWorld
{
public:
	RetainedWorld();
	~RetainedWorld() override;

	RetainedWorld(const RetainedWorld &) = delete;
	RetainedWorld &operator=(const RetainedWorld &) = delete;

	bool Initialize(ResourceAllocator *allocator, FrameScheduler *frames,
		ResourceStateTracker *state_tracker,
		const RetainedWorldConfig &config = RetainedWorldConfig());
	void Shutdown(bool device_lost = false) noexcept;
	bool Ready() const noexcept;

	bool CreateMesh(const RetainedMeshUpload &upload,
		const RetainedFaceRangeUpload *face_ranges, uint32_t face_range_count,
		MeshHandle *out_handle) override;
	bool ReplaceMesh(MeshHandle old_handle,
		const RetainedMeshUpload &upload,
		const RetainedFaceRangeUpload *face_ranges, uint32_t face_range_count,
		MeshHandle *out_handle) override;
	bool ResolveFace(const RetainedFaceToken &token,
		RetainedRange *out_range) const override;
	bool CreateTerrain(const RetainedTerrainUpload &upload,
		MeshHandle *out_handle) override;
	bool ReplaceTerrain(MeshHandle old_handle,
		const RetainedTerrainUpload &upload, MeshHandle *out_handle) override;
	void ReleaseMesh(MeshHandle handle) override;
	void ReleaseSource(const RetainedSourceKey &source) override;
	void ResetAll() override;

	// Add this manager's exact staging demand to the shared frame preflight.
	// The overload taking combined requirements performs the actual scheduler
	// reserve and prepares resource-state records before recording begins.
	bool AccumulateFrameRequirements(FrameRequirements *requirements) const;
	bool PreflightReserve(
		const FrameRequirements &combined_without_retained = FrameRequirements());

	// Record only after a successful PreflightReserve and BeginFrame. This call
	// performs no heap/VMA allocation and no resource creation. Commit only after
	// the command buffer containing these copies has been submitted successfully.
	bool RecordPendingUploads(VkCommandBuffer command_buffer);
	void CommitPendingUploads(uint64_t submission_timeline);
	void AbandonPendingUploadRecording() noexcept;

	bool GetMeshBufferReferences(MeshHandle handle,
		RetainedMeshBufferReferences *out_references) const;
	void StampUse(MeshHandle handle, uint64_t submission_timeline);
	void Reclaim(uint64_t completed_timeline);
	RetainedWorldStatistics Statistics() const;

private:
	struct Impl;
	Impl *impl_;
};

} // namespace vk
} // namespace render
} // namespace piccu

