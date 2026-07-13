cmake_minimum_required(VERSION 3.11)

include("${CMAKE_SOURCE_DIR}/renderer/vulkan/shaders/shader_assets.cmake")
set(PICCU_HOG_VERIFY_SCRIPT
	"${CMAKE_SOURCE_DIR}/renderer/vulkan/shaders/verify_shaders.cmake")
add_hog_file(piccuengine.hog ${CMAKE_CURRENT_SOURCE_DIR}/piccuengine
	${PICCU_VULKAN_SHADER_GENERATED_ASSETS})
