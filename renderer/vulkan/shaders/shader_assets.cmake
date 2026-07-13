# Frozen Vulkan shader source and generated-asset inventory. Keep this list
# explicit: additions/removals are packaging and runtime ABI changes.
set(PICCU_VULKAN_SHADER_MODULES
	world.vert
	world.frag
	ui.vert
	ui.frag
	font.vert
	font.frag
	particle.vert
	particle.frag
	line.vert
	line.frag
	point.vert
	point.frag
	terrain_classify.comp
	terrain_scan.comp
	terrain_emit.comp
	fullscreen.vert
	alpha_clear.frag
	bloom_composite.frag
	bloom_down.frag
	bloom_threshold.frag
	bloom_up.frag
	capture_color.frag
	capture_color_ms.frag
	capture_ssaa_2_to_1.frag
	capture_ssaa_4_to_2.frag
	cockpit_bloom_gamma.frag
	cockpit_over.frag
	depth_map.frag
	depth_map_ms.frag
	gtao_apply.frag
	gtao_blur.frag
	gtao_deferred.frag
	gtao_depth.frag
	gtao_raw.frag
	gtao_suppress.frag
	gtao_temporal.frag
	motion_blur.frag
	motion_copy.frag
	motion_debug.frag
	post_blit.frag
	present.frag
	resolve_depth_ms.frag
	resolve_float_ms.frag
	resolve_uint_ms.frag
	ssaa_downsample.frag)

set(PICCU_VULKAN_SHADER_INCLUDE_SOURCES
	post_math.glsl
	post_uniforms.glsl
	shared/world_abi.glsl
	shared/terrain_emitter.glsl)

set(PICCU_VULKAN_SHADER_META_ASSETS
	reflection.txt
	reflection.sha256
	shaders.sha256
	sources.sha256
	toolchain.txt)

set(PICCU_VULKAN_SHADER_RUNTIME_PREFIX "vulkan/shaders/generated")

set(PICCU_VULKAN_SHADER_GENERATED_ASSETS)
foreach(PICCU_SHADER IN LISTS PICCU_VULKAN_SHADER_MODULES)
	list(APPEND PICCU_VULKAN_SHADER_GENERATED_ASSETS
		"${CMAKE_CURRENT_LIST_DIR}/generated/${PICCU_SHADER}.spv")
endforeach()
foreach(PICCU_META IN LISTS PICCU_VULKAN_SHADER_META_ASSETS)
	list(APPEND PICCU_VULKAN_SHADER_GENERATED_ASSETS
		"${CMAKE_CURRENT_LIST_DIR}/generated/${PICCU_META}")
endforeach()
