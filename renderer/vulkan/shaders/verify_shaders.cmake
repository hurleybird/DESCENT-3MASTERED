cmake_minimum_required(VERSION 3.11)

include("${CMAKE_CURRENT_LIST_DIR}/shader_assets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../../thirdparty/vulkan/dependencies.lock.cmake")

set(GENERATED_ROOT "${CMAKE_CURRENT_LIST_DIR}/generated")
set(MANIFEST "${GENERATED_ROOT}/shaders.sha256")
if(NOT EXISTS "${MANIFEST}")
	message(FATAL_ERROR "Missing Vulkan shader hash manifest")
endif()

file(STRINGS "${MANIFEST}" MANIFEST_LINES)
set(EXPECTED_FILES)
foreach(LINE IN LISTS MANIFEST_LINES)
	if(LINE MATCHES "^([0-9a-f]+)  ([^ ]+)$")
		set(EXPECTED_HASH "${CMAKE_MATCH_1}")
		set(FILE_NAME "${CMAKE_MATCH_2}")
		string(LENGTH "${EXPECTED_HASH}" HASH_LENGTH)
		if(NOT HASH_LENGTH EQUAL 64)
			message(FATAL_ERROR "Malformed Vulkan shader hash: ${LINE}")
		endif()
		set(FILE_PATH "${GENERATED_ROOT}/${FILE_NAME}")
		if(NOT EXISTS "${FILE_PATH}")
			message(FATAL_ERROR "Missing generated Vulkan shader ${FILE_NAME}")
		endif()
		file(SHA256 "${FILE_PATH}" ACTUAL_HASH)
		if(NOT ACTUAL_HASH STREQUAL EXPECTED_HASH)
			message(FATAL_ERROR "Generated Vulkan shader hash mismatch: ${FILE_NAME}")
		endif()
		list(APPEND EXPECTED_FILES "${FILE_NAME}")
	else()
		message(FATAL_ERROR "Malformed Vulkan shader manifest row: ${LINE}")
	endif()
endforeach()

file(GLOB GENERATED_SHADERS RELATIVE "${GENERATED_ROOT}"
	"${GENERATED_ROOT}/*.spv")
list(SORT GENERATED_SHADERS)
list(SORT EXPECTED_FILES)
if(NOT GENERATED_SHADERS STREQUAL EXPECTED_FILES)
	message(FATAL_ERROR "Generated Vulkan shader set differs from manifest")
endif()

set(DECLARED_SHADERS)
foreach(SHADER IN LISTS PICCU_VULKAN_SHADER_MODULES)
	list(APPEND DECLARED_SHADERS "${SHADER}.spv")
endforeach()
list(SORT DECLARED_SHADERS)
if(NOT GENERATED_SHADERS STREQUAL DECLARED_SHADERS)
	message(FATAL_ERROR
		"Generated Vulkan shader set differs from shader_assets.cmake")
endif()

set(SOURCE_DEPENDENCIES ${PICCU_VULKAN_SHADER_MODULES}
	${PICCU_VULKAN_SHADER_INCLUDE_SOURCES}
	reflection_manifest.tsv
	compile_shaders.cmake
	shader_assets.cmake)
set(ACTUAL_SOURCE_HASH_ROWS "")
foreach(SOURCE_RELATIVE IN LISTS SOURCE_DEPENDENCIES)
	set(SOURCE_PATH "${CMAKE_CURRENT_LIST_DIR}/${SOURCE_RELATIVE}")
	if(NOT EXISTS "${SOURCE_PATH}")
		message(FATAL_ERROR "Missing Vulkan shader dependency ${SOURCE_RELATIVE}")
	endif()
	file(SHA256 "${SOURCE_PATH}" SOURCE_HASH)
	string(APPEND ACTUAL_SOURCE_HASH_ROWS
		"${SOURCE_HASH}  ${SOURCE_RELATIVE}\n")
endforeach()
file(SHA256
	"${CMAKE_CURRENT_LIST_DIR}/../../../thirdparty/vulkan/dependencies.lock.cmake"
	DEPENDENCY_LOCK_HASH)
string(APPEND ACTUAL_SOURCE_HASH_ROWS
	"${DEPENDENCY_LOCK_HASH}  @thirdparty/vulkan/dependencies.lock.cmake\n")
set(SOURCE_MANIFEST "${GENERATED_ROOT}/sources.sha256")
if(NOT EXISTS "${SOURCE_MANIFEST}")
	message(FATAL_ERROR "Missing Vulkan shader source hash manifest")
endif()
file(READ "${SOURCE_MANIFEST}" EXPECTED_SOURCE_HASH_ROWS)
if(NOT ACTUAL_SOURCE_HASH_ROWS STREQUAL EXPECTED_SOURCE_HASH_ROWS)
	message(FATAL_ERROR
		"Vulkan shader sources/toolchain lock changed without regenerating SPIR-V")
endif()

set(REFLECTION_FILE "${GENERATED_ROOT}/reflection.txt")
set(REFLECTION_MANIFEST "${GENERATED_ROOT}/reflection.sha256")
if(NOT EXISTS "${REFLECTION_FILE}" OR NOT EXISTS "${REFLECTION_MANIFEST}")
	message(FATAL_ERROR "Missing Vulkan shader reflection artifact")
endif()
file(SHA256 "${REFLECTION_FILE}" ACTUAL_REFLECTION_HASH)
file(READ "${REFLECTION_MANIFEST}" EXPECTED_REFLECTION_ROW)
if(NOT EXPECTED_REFLECTION_ROW STREQUAL
	"${ACTUAL_REFLECTION_HASH}  reflection.txt\n")
	message(FATAL_ERROR "Vulkan shader reflection hash mismatch")
endif()

set(EXPECTED_TOOLCHAIN
	"glslang ${PICCU_GLSLANG_TAG} ${PICCU_GLSLANG_COMMIT}\n")
string(APPEND EXPECTED_TOOLCHAIN
	"SPIRV-Tools ${PICCU_SPIRV_TOOLS_TAG} ${PICCU_SPIRV_TOOLS_COMMIT}\n")
string(APPEND EXPECTED_TOOLCHAIN "target vulkan1.3 spirv1.6\n")
set(TOOLCHAIN_FILE "${GENERATED_ROOT}/toolchain.txt")
if(NOT EXISTS "${TOOLCHAIN_FILE}")
	message(FATAL_ERROR "Missing Vulkan shader toolchain identity")
endif()
file(READ "${TOOLCHAIN_FILE}" ACTUAL_TOOLCHAIN)
if(NOT ACTUAL_TOOLCHAIN STREQUAL EXPECTED_TOOLCHAIN)
	message(FATAL_ERROR "Vulkan shader toolchain identity is stale")
endif()

file(GLOB GENERATED_ASSET_NAMES RELATIVE "${GENERATED_ROOT}"
	"${GENERATED_ROOT}/*")
set(EXPECTED_ASSET_NAMES ${DECLARED_SHADERS}
	${PICCU_VULKAN_SHADER_META_ASSETS})
list(SORT GENERATED_ASSET_NAMES)
list(SORT EXPECTED_ASSET_NAMES)
if(NOT GENERATED_ASSET_NAMES STREQUAL EXPECTED_ASSET_NAMES)
	message(FATAL_ERROR "Generated Vulkan runtime asset inventory differs from declaration")
endif()

list(LENGTH EXPECTED_FILES VERIFIED_SHADER_COUNT)
message(STATUS "Verified ${VERIFIED_SHADER_COUNT} Vulkan shaders and generated metadata")
