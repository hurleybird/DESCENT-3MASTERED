/* API-neutral IRenderer facade for the Vulkan backend. */
#pragma once

#include "../IRenderer.h"
#include "../core/render_state_contract.h"
#include "vk_renderer_runtime.h"

#include <cstddef>
#include <vector>

class VulkanRenderer final : public IRenderer
{
public:
	// The runtime is non-owning and must outlive an initialized facade.
	explicit VulkanRenderer(piccu::render::vk::IVulkanRuntime *runtime = nullptr);
	~VulkanRenderer() override;
	static void *operator new(std::size_t size);
	static void operator delete(void *memory) noexcept;

	void AttachRuntime(piccu::render::vk::IVulkanRuntime *runtime);
	piccu::render::vk::RuntimeFailure LastFailure() const { return last_failure_; }

	int Init(oeApplication *app, renderer_preferred_state *pref_state) override;
	void Close() override;
	RendererCapabilities GetCapabilities() const override;

	void SetFrameDynamicState(
		const renderer_frame_dynamic_state &state) override;
	void SetMipState(sbyte state) override;
	void SetFogState(sbyte on) override;
	void SetFogBorders(float fog_near, float fog_far) override;
	void SetFlatColor(ddgr_color color) override;
	void SetTextureType(texture_type type) override;
	void SetFiltering(sbyte state) override;
	void SetZBufferState(sbyte state) override;
	void SetZValues(float nearz, float farz) override;
	void SetOverlayMap(int handle) override;
	void SetOverlayType(ubyte type) override;
	void SetFogColor(ddgr_color fogcolor) override;
	void SetAlphaType(sbyte alphatype) override;
	void SetAlphaValue(ubyte val) override;
	void SetAlphaFactor(float val) override;
	float GetAlphaFactor() override;
	void SetWrapType(wrap_type val) override;
	void SetZBufferWriteMask(int state) override;
	int SetPreferredState(renderer_preferred_state *pref_state) override;
	void SetCoplanarPolygonOffset(float factor) override;
	void SetCullFace(bool state) override;
	void SetColorModel(color_model model) override;
	void SetLighting(light_state state) override;
	void SetPerPixelLightingDirection(const vector *lightdir) override;
	void SetPerPixelDynamicLighting(const vector *face_normal, int count,
		const renderer_per_pixel_light *lights) override;
	void SetPerPixelSpecularMode(int mode) override;
	void SetPerPixelSpecularMap(int handle) override;
	void SetZBias(float z_bias) override;
	void SetBumpmapReadyState(int state, int map) override;
	void SetGammaValue(float val) override;

	void GetStatistics(tRendererStats *stats) override;
	void GetProjectionParameters(int *width, int *height) override;
	void GetProjectionScreenParameters(int &screenLX, int &screenTY,
		int &screenW, int &screenH) override;
	float GetAspectRatio() override;
	void GetRenderState(rendering_state *rstate) override;
	void DLLGetRenderState(DLLrendering_state *rstate) override;
	int LowVidMem() override;
	int SupportsBumpmapping() override;

	void PreUploadTextureToCard(int handle, int map_type) override;
	void FreePreUploadedTexture(int handle, int map_type) override;
	void ResetCache() override;

	void StartFrame(int x1, int y1, int x2, int y2,
		int clear_flags = RF_CLEAR_ZBUFFER) override;
	void EndFrame() override;
	void CaptureBloomSource() override;
	void PerfGpuSceneMark(renderer_gpu_scene_mark mark) override;
	void ClearScreen(ddgr_color color) override;
	void ClearZBuffer() override;

	void DrawPolygon3D(int handle, g3Point **p, int nv,
		int map_type = MAP_TYPE_BITMAP) override;
	void DrawPolygon3DBatch(int handle, const renderer_poly_batch_item *items,
		int count, int map_type = MAP_TYPE_BITMAP) override;
	void DrawRetainedPolygon3DBatch(int handle,
		const renderer_retained_poly_batch_item *items, int count,
		int map_type = MAP_TYPE_BITMAP) override;
	bool DrawRetainedTerrain(
		const renderer_retained_terrain_submission *submission) override;
	bool SupportsParticleInstanceBatch() const override;
	bool CanDrawParticleInstanceBatch() const override;
	bool DrawParticleInstanceBatch(int handle,
		const renderer_particle_instance *items, int count,
		int map_type = MAP_TYPE_BITMAP) override;
	void DrawPolygon2D(int handle, g3Point **p, int nv) override;

	void BeginMotionObject(int object_handle,
		int motion_object_flags = RENDERER_MOTION_OBJECT_DEFAULT) override;
	void EndMotionObject() override;
	void SuspendMotionVectorWrites() override;
	void ResumeMotionVectorWrites() override;
	void FillMotionVectorRegion(int object_handle) override;
	bool GetMotionVectorSample(const vector *current_world,
		const vector *previous_world, float *current_u, float *current_v,
		float *velocity_u, float *velocity_v) override;
	void SetAOSuppression(float value) override;
	void SetBloomSuppression(float value) override;
	void SetAOClass(int value) override;
	void SetPostMaskOnly(int state) override;
	void SetSoftParticleState(int state) override;
	void NotifyDepthBufferWrite() override;
	void SetCockpitBackingEffect(
		const renderer_cockpit_backing_effect *effect) override;

	void DrawScaledBitmap(int x1, int y1, int x2, int y2, int bm,
		float u0, float v0, float u1, float v1, int color = -1,
		float *alphas = nullptr) override;
	void DrawScaledBitmapWithZ(int x1, int y1, int x2, int y2, int bm,
		float u0, float v0, float u1, float v1, float zval, int color,
		float *alphas = nullptr) override;
	void DrawChunkedBitmap(chunked_bitmap *chunk, int x, int y,
		ubyte alpha) override;
	void DrawScaledChunkedBitmap(chunked_bitmap *chunk, int x, int y,
		int neww, int newh, ubyte alpha) override;
	void DrawSimpleBitmap(int bm_handle, int x, int y) override;
	void FillRect(ddgr_color color, int x1, int y1, int x2, int y2) override;
	void SetPixel(ddgr_color color, int x, int y) override;
	ddgr_color GetPixel(int x, int y) override;
	void SetCharacterParameters(ddgr_color color1, ddgr_color color2,
		ddgr_color color3, ddgr_color color4) override;
	void DrawFontCharacter(int bm_handle, int x1, int y1, int x2, int y2,
		float u, float v, float w, float h) override;
	void FlushTextLayer() override;
	void DrawLine(int x1, int y1, int x2, int y2) override;
	void FillCircle(ddgr_color col, int x, int y, int rad) override;
	void DrawCircle(int x, int y, int rad) override;

	void Flip() override;
	bool BeginPostPresentFrame() override;
	bool IsPostPresentFramePending() const override;
	void StartPostPresentFrame(int x1, int y1, int x2, int y2,
		int clear_flags = RF_CLEAR_ZBUFFER) override;
	void EndPostPresentFrame() override;
	bool BeginCockpitFrame() override;
	void EndCockpitFrame() override;
	void DrawSpecialLine(g3Point *p0, g3Point *p1) override;
	void DrawSpecialLineBatch(const renderer_line_batch_item *items,
		int count) override;

	void CopyBitmapToFramebuffer(int bm_handle, int x, int y) override;
	void SetFrameBufferCopyState(bool state) override;
	void Screenshot(int bm_handle) override;
	int SaveScreenshotPNG(const char *filename) override;

	void UpdateCommon(float *projection, float *modelview, int depth = 0) override;
	void SetCommonDepth(int depth) override;
	uint32_t GetPipelineByName(const char *name) override;
	void BindPipeline(uint32_t handle) override;
	void UpdateSpecular(SpecularBlock *specularstate) override;
	void UpdateFogBrightness(RoomBlock *roomstate, int numrooms) override;
	void SetCurrentRoomNum(int roomblocknum) override;
	void UpdateTerrainFog(float color[4], float start, float end) override;
	void UseShaderTest() override;
	void EndShaderTest() override;
	void BindBitmap(int handle) override;
	void BindLightmap(int handle) override;
	void RestoreLegacy() override;
	void GetScreenSize(int &screen_width, int &screen_height) override;
	double GetDisplayRefreshRate() override;
	void ClearBoundTextures() override;

private:
	using RuntimeFailure = piccu::render::vk::RuntimeFailure;
	using RenderCaptureSegment = piccu::render::RenderCaptureSegment;

	void ResetFacadeState();
	void Fail(RuntimeFailure failure, const char *operation) const;
	RenderCaptureSegment *Capture(const char *operation) const;
	bool Append(const piccu::render::CaptureCommand &command,
		const char *operation);
	piccu::render::CapturedPreferredState CapturedPreferred() const;
	bool BeginTarget(piccu::render::RenderTargetClass target,
		int x1, int y1, int x2, int y2, int clear_flags,
		const char *operation);
	bool DescribeAndInternPresentation(bool defer_bloom);
	bool BeginPostPresentFrameInternal(bool defer_bloom);
	bool ResolveTexture(const piccu::render::vk::TextureRequest &request,
		piccu::render::vk::ResolvedTexture *resolved,
		const char *operation);
	bool BuildMaterial(int handle, int map_type,
		piccu::render::CapturedMaterial *material);
	bool BuildDrawState(piccu::render::PrimitiveSourceKind source,
		uint32_t category, uint32_t category_3d,
		piccu::render::CapturedShaderRasterState *state,
		piccu::render::MrtDrawRoutingDecision *routing);
	bool BuildPayload(g3Point *const *points, uint32_t vertex_count,
		const piccu::render::CapturedShaderRasterState &state,
		piccu::render::PayloadRef *payload);
	bool EmitStream(const piccu::render::BaseVertex *vertices,
		uint32_t vertex_count, const uint32_t *indices, uint32_t index_count,
		piccu::render::DepthInterpretation depth,
		piccu::render::PrimitiveSourceKind source, uint32_t category,
		uint32_t category_3d, int handle, int map_type,
		g3Point *const *source_points, const char *operation);
	bool BuildPointVertex(const g3Point &point,
		piccu::render::BaseVertex *vertex);
	bool BuildRetainedPointVertex(const g3Point &point,
		const vector &object_position, piccu::render::BaseVertex *vertex);
	bool EmitRetainedBatch(int handle,
		const renderer_retained_poly_batch_item *items, int count, int map_type);
	bool EmitRetainedTerrain(
		const renderer_retained_terrain_submission &submission);
	void ReleaseRetainedMeshCache();
	bool AppendColorClear(const piccu::render::LogicalRect &rect,
		bool whole_target, ddgr_color color, float alpha,
		uint32_t selected_attachments, const char *operation);
	bool AppendDepthClear(const piccu::render::LogicalRect &rect,
		bool whole_target, const char *operation);
	bool AdvanceTargetVersion(bool color_written, bool depth_written,
		const char *operation);
	void ResetPerPresentedFrameState();
	uint32_t SamplerIndex(bool array_texture) const;
	float AOWeight(uint32_t ao_class) const;

	piccu::render::vk::IVulkanRuntime *runtime_;
	mutable RuntimeFailure last_failure_;
	bool initialized_;
	renderer_preferred_state preferred_;
	renderer_frame_dynamic_state frame_dynamic_;
	rendering_state public_state_;
	piccu::render::LegacyRenderState legacy_state_;
	piccu::render::LegacyTextureBindingShadow texture_shadow_;
	piccu::render::LegacyPrimitiveScratch0 primitive_scratch_;

	tRendererStats current_stats_;
	tRendererStats last_stats_;
	piccu::render::RenderTargetClass active_target_;
	piccu::render::LogicalRect active_clip_;
	piccu::render::TargetLayoutId active_layout_;
	piccu::render::TargetVersionId active_target_version_;
	piccu::render::ViewStateId active_view_;
	piccu::render::CapturedTargetVersion active_version_record_;
	piccu::render::CapturedWorldView current_view_;
	piccu::render::vk::PresentationDescription presentation_;
	piccu::render::RenderTargetSignatureId presentation_signature_;
	piccu::render::WsiSignatureId presentation_wsi_;
	piccu::render::PresentRectId presentation_rect_;

	bool frame_interval_open_;
	bool post_present_pending_;
	bool cockpit_open_;
	bool font_glyphs_pending_;
	bool framebuffer_copy_state_;
	bool soft_depth_acquired_;
	uint32_t presented_frame_serial_;
	uint32_t view_interval_serial_;
	uint32_t font_enqueue_serial_;
	uint32_t font_flush_serial_;
	uint32_t font_texture_width_;
	uint32_t font_texture_height_;
	uint32_t cockpit_capture_serial_;
	uint32_t perf_marker_serial_;
	uint32_t readback_request_serial_;
	uint32_t soft_depth_snapshot_serial_;
	uint32_t common_depth_;
	uint32_t selected_pipeline_;
	piccu::render::MeshHandle retained_terrain_mesh_;
	uint32_t retained_terrain_source_id_;
	uint32_t retained_terrain_source_generation_;
	uint32_t retained_terrain_mesh_generation_;
	piccu::render::TextureVersionId retained_terrain_base_version_;
	piccu::render::TextureVersionId retained_terrain_lightmap_version_;
	static constexpr uint32_t kCommonTransformDepths = 64;
	float common_object_transforms_[kCommonTransformDepths][16];
	bool common_object_transform_valid_[kCommonTransformDepths];

	ddgr_color font_colors_[4];
	piccu::render::CapturedCockpitBackingEffect cockpit_backing_;
	piccu::render::GpuDynamicLight dynamic_lights_[
		piccu::render::kMaxDynamicLights];
	uint32_t dynamic_light_count_;
	piccu::render::GpuSpecularBlock specular_block_;
	bool have_specular_block_;
	std::vector<piccu::render::GpuWorldAux> room_aux_;
	int32_t current_room_;
	piccu::render::GpuWorldAux terrain_fog_;

	std::vector<piccu::render::BaseVertex> scratch_vertices_;
	std::vector<uint32_t> scratch_indices_;
	std::vector<piccu::render::PerspectiveVertexPayload> scratch_perspective_;
	std::vector<piccu::render::MotionVertexPayload> scratch_motion_;
	std::vector<piccu::render::SpecularVertexPayload> scratch_specular_;
	std::vector<piccu::render::RetainedFaceRangeUpload> scratch_retained_faces_;
	struct RetainedMeshCacheEntry
	{
		piccu::render::RetainedFaceToken token = {};
		piccu::render::RetainedRange range = {};
		uint64_t content_fingerprint = 0;
		uint32_t last_used_frame = 0;
		std::vector<piccu::render::BaseVertex> vertices;
		std::vector<uint32_t> indices;
		std::vector<piccu::render::MotionVertexPayload> motion;
	};
	std::vector<RetainedMeshCacheEntry> retained_mesh_cache_;
	std::vector<piccu::render::GpuDynamicLight> scratch_terrain_lights_;
};
