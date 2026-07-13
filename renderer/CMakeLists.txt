SET (RENDERER_SOURCES
		renderer/core/render_capabilities.h
		renderer/core/CONTRACT_NOTES.md
		renderer/core/render_capture.cpp
		renderer/core/render_capture.h
		renderer/core/render_contract.cpp
		renderer/core/render_contract.h
		renderer/core/render_coordinate_contract.cpp
		renderer/core/render_coordinate_contract.h
		renderer/core/render_device_contract.cpp
		renderer/core/render_device_contract.h
		renderer/core/render_graph_contract.cpp
		renderer/core/render_graph_contract.h
		renderer/core/render_lifetime_contract.h
		renderer/core/render_state_contract.cpp
		renderer/core/render_state_contract.h
		renderer/core/render_trace_contract.h
		renderer/core/retained_world.h
		renderer/core/render_verification_contract.cpp
		renderer/core/render_verification_contract.h
		renderer/core/render_wsi_contract.h

		# Vulkan 1.3 renderer (Volk owns dispatch; VMA owns allocations).
		renderer/vulkan/vk_common.h
		renderer/vulkan/vk_compiler.cpp
		renderer/vulkan/vk_compiler.h
		renderer/vulkan/vk_frame.cpp
		renderer/vulkan/vk_frame.h
		renderer/vulkan/vk_frame_plan.cpp
		renderer/vulkan/vk_frame_plan.h
		renderer/vulkan/vk_pipelines.cpp
		renderer/vulkan/vk_pipelines.h
		renderer/vulkan/vk_platform.cpp
		renderer/vulkan/vk_platform.h
		renderer/vulkan/vk_renderer.cpp
		renderer/vulkan/vk_renderer.h
		renderer/vulkan/vk_renderer_runtime.h
		renderer/vulkan/vk_resources.cpp
		renderer/vulkan/vk_resources.h
		renderer/vulkan/vk_retained_world.cpp
		renderer/vulkan/vk_retained_world.h
		renderer/vulkan/vk_runtime.cpp
		renderer/vulkan/vk_runtime.h
		renderer/vulkan/vk_state_tracker.cpp
		renderer/vulkan/vk_state_tracker.h
		renderer/vulkan/vk_targets.cpp
		renderer/vulkan/vk_targets.h
		renderer/vulkan/vk_textures.cpp
		renderer/vulkan/vk_textures.h
		renderer/vulkan/vk_wsi.cpp
		renderer/vulkan/vk_wsi.h
		renderer/HardwareInternal.h
		renderer/RendererConfig.h
		renderer/globalvars.cpp
		renderer/gl_draw.cpp
		renderer/gl_framebuffer.cpp
		renderer/gl_gtao.cpp
		renderer/gl_image.cpp
		renderer/gl_init.cpp
		renderer/gl_local.h
		renderer/gl_main.cpp
		renderer/gl_mesh.cpp
		renderer/gl_mesh.h
		renderer/gl_shader.cpp
		renderer/gl_shadersource.cpp
		
		#legacy GL renderer
		renderer/gl1_draw.cpp
		renderer/gl1_image.cpp
		renderer/gl1_init.cpp
		renderer/gl1_local.h
		renderer/gl1_main.cpp
		renderer/gl1_shader.cpp
		
		renderer/gl_shared.h
		
		#renderer switchboard
		renderer/renderer.cpp
		renderer/IRenderer.h
		
		#GLAD gl loader
		renderer/gl.c
		renderer/wgl.c
		
		#3D system
		renderer/clipper.cpp
		renderer/draw.cpp
		renderer/instance.cpp
		renderer/points.cpp
		renderer/setup.cpp
		renderer/transforms.cpp)

set (RENDERER_SOURCES ${RENDERER_SOURCES} PARENT_SCOPE)

# Vulkan shaders are checked-in build artifacts: ordinary builds verify that
# sources, pinned dependency identity, reflection, hashes, and SPIR-V agree.
# Regeneration is explicit and requires the exact pinned tools.
set(PICCU_VULKAN_SHADER_DIR
	"${CMAKE_CURRENT_SOURCE_DIR}/vulkan/shaders")
include("${PICCU_VULKAN_SHADER_DIR}/shader_assets.cmake")
set(PICCU_GLSLANG_EXECUTABLE "" CACHE FILEPATH
	"Pinned glslang 16.3.0 executable used to regenerate Vulkan SPIR-V")
set(PICCU_SPIRV_VAL_EXECUTABLE "" CACHE FILEPATH
	"Pinned SPIRV-Tools spirv-val executable used to regenerate Vulkan SPIR-V")

add_custom_target(piccu_vulkan_shaders_verify ALL
	COMMAND ${CMAKE_COMMAND} -P
		"${PICCU_VULKAN_SHADER_DIR}/verify_shaders.cmake"
	SOURCES
		"${PICCU_VULKAN_SHADER_DIR}/shader_assets.cmake"
		"${PICCU_VULKAN_SHADER_DIR}/verify_shaders.cmake"
	VERBATIM)

add_custom_target(piccu_vulkan_shaders_generate
	COMMAND ${CMAKE_COMMAND}
		-DGLSLANG_EXECUTABLE=${PICCU_GLSLANG_EXECUTABLE}
		-DSPIRV_VAL_EXECUTABLE=${PICCU_SPIRV_VAL_EXECUTABLE}
		-P "${PICCU_VULKAN_SHADER_DIR}/compile_shaders.cmake"
	SOURCES
		"${PICCU_VULKAN_SHADER_DIR}/compile_shaders.cmake"
		"${PICCU_VULKAN_SHADER_DIR}/shader_assets.cmake"
	VERBATIM)

set(PICCU_VULKAN_RUNTIME_SHADER_DIR
	"${CMAKE_BINARY_DIR}/${PICCU_VULKAN_SHADER_RUNTIME_PREFIX}")
set(PICCU_VULKAN_RUNTIME_SHADER_STAMP
	"${PICCU_VULKAN_RUNTIME_SHADER_DIR}/.verified-assets")
set(PICCU_VULKAN_SHADER_COPY_COMMANDS)
foreach(PICCU_SHADER_ASSET IN LISTS PICCU_VULKAN_SHADER_GENERATED_ASSETS)
	get_filename_component(PICCU_SHADER_ASSET_NAME
		"${PICCU_SHADER_ASSET}" NAME)
	list(APPEND PICCU_VULKAN_SHADER_COPY_COMMANDS
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${PICCU_SHADER_ASSET}"
			"${PICCU_VULKAN_RUNTIME_SHADER_DIR}/${PICCU_SHADER_ASSET_NAME}")
endforeach()
set(PICCU_VULKAN_SHADER_SOURCE_DEPENDENCIES)
foreach(PICCU_SHADER_SOURCE IN LISTS PICCU_VULKAN_SHADER_MODULES
	PICCU_VULKAN_SHADER_INCLUDE_SOURCES)
	list(APPEND PICCU_VULKAN_SHADER_SOURCE_DEPENDENCIES
		"${PICCU_VULKAN_SHADER_DIR}/${PICCU_SHADER_SOURCE}")
endforeach()
add_custom_command(OUTPUT "${PICCU_VULKAN_RUNTIME_SHADER_STAMP}"
	COMMAND ${CMAKE_COMMAND} -P
		"${PICCU_VULKAN_SHADER_DIR}/verify_shaders.cmake"
	COMMAND ${CMAKE_COMMAND} -E make_directory
		"${PICCU_VULKAN_RUNTIME_SHADER_DIR}"
	${PICCU_VULKAN_SHADER_COPY_COMMANDS}
	COMMAND ${CMAKE_COMMAND} -E touch
		"${PICCU_VULKAN_RUNTIME_SHADER_STAMP}"
	DEPENDS
		${PICCU_VULKAN_SHADER_GENERATED_ASSETS}
		${PICCU_VULKAN_SHADER_SOURCE_DEPENDENCIES}
		"${PICCU_VULKAN_SHADER_DIR}/reflection_manifest.tsv"
		"${PICCU_VULKAN_SHADER_DIR}/compile_shaders.cmake"
		"${PICCU_VULKAN_SHADER_DIR}/verify_shaders.cmake"
		"${PICCU_VULKAN_SHADER_DIR}/shader_assets.cmake"
		"${CMAKE_SOURCE_DIR}/thirdparty/vulkan/dependencies.lock.cmake"
	VERBATIM)
add_custom_target(piccu_vulkan_shader_runtime_assets ALL
	DEPENDS "${PICCU_VULKAN_RUNTIME_SHADER_STAMP}")
add_dependencies(piccu_vulkan_shader_runtime_assets
	piccu_vulkan_shaders_verify)

install(FILES ${PICCU_VULKAN_SHADER_GENERATED_ASSETS}
	DESTINATION "${D3_GAMEDIR}${PICCU_VULKAN_SHADER_RUNTIME_PREFIX}")

if (BUILD_TESTING)
	add_executable(piccuengine_render_contract_tests
		tests/render_contract_tests.cpp
		core/render_contract.cpp
		core/render_capture.cpp
		core/render_coordinate_contract.cpp
		core/render_device_contract.cpp
		core/render_graph_contract.cpp
		core/render_state_contract.cpp
		core/render_verification_contract.cpp)
	target_include_directories(piccuengine_render_contract_tests PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR})
	target_compile_features(piccuengine_render_contract_tests PRIVATE cxx_std_11)
	add_test(NAME piccuengine_render_contract COMMAND piccuengine_render_contract_tests)
	add_test(NAME piccuengine_renderer_corpus_assets
		COMMAND ${CMAKE_COMMAND} -P
		${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_corpus_assets.cmake)
	add_test(NAME piccuengine_vulkan_shader_assets
		COMMAND ${CMAKE_COMMAND} -P
		${PICCU_VULKAN_SHADER_DIR}/verify_shaders.cmake)
endif()
