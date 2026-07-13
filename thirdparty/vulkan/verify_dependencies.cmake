include("${CMAKE_CURRENT_LIST_DIR}/dependencies.lock.cmake")

if (NOT PICCU_VULKAN_FILE_MANIFEST_VERSION EQUAL 1)
	message(FATAL_ERROR
		"Unsupported Vulkan dependency manifest version: "
		"${PICCU_VULKAN_FILE_MANIFEST_VERSION}")
endif()

set(manifest_relative_path "${PICCU_VULKAN_FILE_MANIFEST}")
set(manifest_path "${CMAKE_CURRENT_LIST_DIR}/${manifest_relative_path}")
if (NOT EXISTS "${manifest_path}")
	message(FATAL_ERROR
		"Missing Vulkan dependency manifest: ${manifest_relative_path}")
endif()

# The manifest uses the sha256sum format: 64 lowercase hexadecimal digits,
# two spaces, then a forward-slash relative path.  It intentionally has no
# comments so regenerating it is deterministic.
file(STRINGS "${manifest_path}" manifest_entries)
set(manifest_files)
set(manifest_hashes)
foreach(entry IN LISTS manifest_entries)
	string(LENGTH "${entry}" entry_length)
	if (entry_length LESS 67)
		message(FATAL_ERROR "Malformed Vulkan dependency manifest line: ${entry}")
	endif()

	string(SUBSTRING "${entry}" 0 64 expected_hash)
	string(SUBSTRING "${entry}" 64 2 separator)
	string(SUBSTRING "${entry}" 66 -1 relative_path)
	string(REGEX MATCH "^[0-9a-f]+$" valid_hash "${expected_hash}")
	if (NOT separator STREQUAL "  " OR
		NOT valid_hash STREQUAL expected_hash OR
		relative_path STREQUAL "")
		message(FATAL_ERROR "Malformed Vulkan dependency manifest line: ${entry}")
	endif()
	if (relative_path MATCHES "^/" OR
		relative_path MATCHES "^[A-Za-z]:" OR
		relative_path MATCHES "(^|/)\\.\\.(/|$)" OR
		relative_path MATCHES "\\\\")
		message(FATAL_ERROR
			"Unsafe Vulkan dependency manifest path: ${relative_path}")
	endif()
	set(in_authenticated_root false)
	foreach(root IN LISTS PICCU_VULKAN_AUTHENTICATED_ROOTS)
		if (relative_path MATCHES "^${root}/")
			set(in_authenticated_root true)
		endif()
	endforeach()
	if (NOT in_authenticated_root)
		message(FATAL_ERROR
			"Vulkan dependency manifest path is outside authenticated roots: "
			"${relative_path}")
	endif()
	list(FIND manifest_files "${relative_path}" duplicate_index)
	if (NOT duplicate_index EQUAL -1)
		message(FATAL_ERROR
			"Duplicate Vulkan dependency manifest path: ${relative_path}")
	endif()

	list(APPEND manifest_files "${relative_path}")
	list(APPEND manifest_hashes "${expected_hash}")
endforeach()

list(LENGTH manifest_files manifest_file_count)
if (NOT manifest_file_count EQUAL PICCU_VULKAN_FILE_COUNT)
	message(FATAL_ERROR
		"Vulkan dependency manifest count mismatch\n"
		"lock:     ${PICCU_VULKAN_FILE_COUNT}\n"
		"manifest: ${manifest_file_count}")
endif()

# Authenticate every file under each vendored component root rather than only
# known extensions. This prevents a newly added source, generated file, or
# license from bypassing the lock merely because its filename is unfamiliar.
set(actual_files)
foreach(root IN LISTS PICCU_VULKAN_AUTHENTICATED_ROOTS)
	file(GLOB_RECURSE root_files
		LIST_DIRECTORIES false
		RELATIVE "${CMAKE_CURRENT_LIST_DIR}"
		"${CMAKE_CURRENT_LIST_DIR}/${root}/*")
	list(APPEND actual_files ${root_files})
endforeach()
list(SORT actual_files)

set(sorted_manifest_files ${manifest_files})
list(SORT sorted_manifest_files)
set(missing_files)
set(unexpected_files)
foreach(relative_path IN LISTS sorted_manifest_files)
	list(FIND actual_files "${relative_path}" actual_index)
	if (actual_index EQUAL -1)
		list(APPEND missing_files "${relative_path}")
	endif()
endforeach()
foreach(relative_path IN LISTS actual_files)
	list(FIND sorted_manifest_files "${relative_path}" manifest_index)
	if (manifest_index EQUAL -1)
		list(APPEND unexpected_files "${relative_path}")
	endif()
endforeach()

if (missing_files OR unexpected_files)
	string(REPLACE ";" "\n  " missing_text "${missing_files}")
	string(REPLACE ";" "\n  " unexpected_text "${unexpected_files}")
	if (missing_files)
		set(missing_text "  ${missing_text}")
	else()
		set(missing_text "  <none>")
	endif()
	if (unexpected_files)
		set(unexpected_text "  ${unexpected_text}")
	else()
		set(unexpected_text "  <none>")
	endif()
	message(FATAL_ERROR
		"Vulkan dependency file-set mismatch\n"
		"Missing files:\n${missing_text}\n"
		"Unexpected files:\n${unexpected_text}")
endif()

set(manifest_index 0)
foreach(relative_path IN LISTS manifest_files)
	list(GET manifest_hashes ${manifest_index} expected_hash)
	set(path "${CMAKE_CURRENT_LIST_DIR}/${relative_path}")
	file(SHA256 "${path}" actual_hash)
	string(TOLOWER "${actual_hash}" actual_hash)
	if (NOT actual_hash STREQUAL expected_hash)
		message(FATAL_ERROR
			"Pinned Vulkan dependency hash mismatch for ${relative_path}\n"
			"expected: ${expected_hash}\n"
			"actual:   ${actual_hash}")
	endif()
	math(EXPR manifest_index "${manifest_index} + 1")
endforeach()

message(STATUS
	"Verified ${manifest_file_count} pinned Vulkan dependency files")
