# Pinned Vulkan dependencies

PiccuEngine vendors the runtime-facing Vulkan dependencies so ordinary builds
never fetch a mutable branch and the executable does not link to an SDK import
library.

| Component | Upstream | Pinned revision | Vendored content |
|---|---|---|---|
| Vulkan-Headers | KhronosGroup/Vulkan-Headers | `8864cdc896bbc2a9b6eb36b3218fc9ef57908d77` (`vulkan-sdk-1.4.350.1`) | `include/`, `LICENSE.md`, `LICENSES/` |
| Volk | zeux/volk | `3ca312a4f38baa63d8006b6905abbeeb89c8087d` (`1.4.350`) | `volk.h`, `volk.c`, `LICENSE.md` |
| Vulkan Memory Allocator | GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator | `3aa921224c154a0d2c43912bc88e1c42ce1f7607` (`v3.4.0`) | `vk_mem_alloc.h`, `LICENSE.txt` |
| glslang | KhronosGroup/glslang | `275822a6261ee689aadb1da5f09a0ec2f058685c` (`16.3.0`) | build-tool revision plus vendored license |
| SPIRV-Tools | KhronosGroup/SPIRV-Tools | `0539c81f69a3daeb706fd3477dca61435b475156` (`vulkan-sdk-1.4.350.0`) | validator revision plus vendored license |

The exact revisions live in `dependencies.lock.cmake`. Every regular file
under the vendored component directories is authenticated by
`dependency-files.sha256`; `verify_dependencies.cmake` rejects hash changes,
missing files, and unexpected files in those directories. A dependency update
must regenerate the manifest and validate all SPIR-V, reflection data, ABI
tests, and the renderer parity corpus.

Volk is instantiated once with `VOLK_IMPLEMENTATION`; VMA is instantiated once
with `VMA_IMPLEMENTATION`. No other translation unit may define either macro.
