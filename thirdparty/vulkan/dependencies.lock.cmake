# Immutable Vulkan dependency lock for PiccuEngine.
# Update the vendored files, revisions, hashes, licenses, and shader corpus in
# one reviewed change.  Ordinary builds must not fetch these repositories.

# The manifest authenticates every regular file under the vendored component
# roots. The count makes additions and removals an explicit dependency-lock
# update while keeping PiccuEngine's build metadata outside the upstream lock.
set(PICCU_VULKAN_FILE_MANIFEST "dependency-files.sha256")
set(PICCU_VULKAN_FILE_MANIFEST_VERSION 1)
set(PICCU_VULKAN_FILE_COUNT 59)
set(PICCU_VULKAN_AUTHENTICATED_ROOTS
	"Vulkan-Headers"
	"volk"
	"VulkanMemoryAllocator"
	"shader-tools")

set(PICCU_VULKAN_HEADERS_TAG "vulkan-sdk-1.4.350.1")
set(PICCU_VULKAN_HEADERS_COMMIT "8864cdc896bbc2a9b6eb36b3218fc9ef57908d77")
set(PICCU_VULKAN_CORE_HEADER_SHA256 "2f7aa635db9a068294c70de0f90d74c869602c132c30f306cb73b973a3f9b502")

set(PICCU_VOLK_TAG "1.4.350")
set(PICCU_VOLK_COMMIT "3ca312a4f38baa63d8006b6905abbeeb89c8087d")
set(PICCU_VOLK_HEADER_SHA256 "a31873f8e4f698fdeacc440194e3836cc4dbe54d7173aeeaa82391004f2f9bb1")
set(PICCU_VOLK_SOURCE_SHA256 "89e165a7e5c9de480881febe9a4d31ec153a917762fefeddef621465f45e95ac")

set(PICCU_VMA_TAG "v3.4.0")
set(PICCU_VMA_COMMIT "3aa921224c154a0d2c43912bc88e1c42ce1f7607")
set(PICCU_VMA_HEADER_SHA256 "5a9e72076a28a2659586e7ab9e9bf1539c079e034726f19611f282c4ec093790")

# Build-time tools are not runtime dependencies.  Release/CI shader builds
# must identify exactly these revisions in the generated shader manifest.
set(PICCU_GLSLANG_TAG "16.3.0")
set(PICCU_GLSLANG_COMMIT "275822a6261ee689aadb1da5f09a0ec2f058685c")
set(PICCU_SPIRV_TOOLS_TAG "vulkan-sdk-1.4.350.0")
set(PICCU_SPIRV_TOOLS_COMMIT "0539c81f69a3daeb706fd3477dca61435b475156")
