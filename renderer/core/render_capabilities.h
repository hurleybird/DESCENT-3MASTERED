/*
 * PiccuEngine renderer capability contract.
 *
 * This header is deliberately API-neutral.  Backend implementation details
 * (OpenGL objects, Vulkan handles, platform window types) must never leak into
 * this public query surface.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <type_traits>

enum renderer_backend : uint32_t
{
	RENDERER_BACKEND_GL1 = 0,
	RENDERER_BACKEND_GL4 = 1,
	RENDERER_BACKEND_VULKAN = 2,
};

constexpr uint32_t RENDERER_CAPABILITIES_ABI_VERSION = 1;

// Public ABI version 1.  Append-only changes require a new ABI version.
struct RendererCapabilities
{
	renderer_backend backend;
	bool retained_rooms;
	bool retained_terrain;
	bool retained_polymodels;
	bool particle_instance_batch;
	bool per_pixel_lighting;
	bool split_and_field_specular;
	bool post_processing;
	bool pixel_motion_vectors;
	bool late_close_screen_pass;
	bool editor_readback;
};

static_assert(sizeof(renderer_backend) == sizeof(uint32_t),
	"renderer_backend is part of renderer capability ABI v1");
static_assert(std::is_standard_layout<RendererCapabilities>::value,
	"RendererCapabilities must remain a standard-layout ABI type");
static_assert(offsetof(RendererCapabilities, backend) == 0,
	"RendererCapabilities ABI changed");
static_assert(offsetof(RendererCapabilities, retained_rooms) == sizeof(uint32_t),
	"RendererCapabilities ABI changed");
static_assert(offsetof(RendererCapabilities, retained_terrain) == 5, "RendererCapabilities ABI changed");
static_assert(offsetof(RendererCapabilities, retained_polymodels) == 6, "RendererCapabilities ABI changed");
static_assert(offsetof(RendererCapabilities, particle_instance_batch) == 7, "RendererCapabilities ABI changed");
static_assert(offsetof(RendererCapabilities, per_pixel_lighting) == 8, "RendererCapabilities ABI changed");
static_assert(offsetof(RendererCapabilities, split_and_field_specular) == 9, "RendererCapabilities ABI changed");
static_assert(offsetof(RendererCapabilities, post_processing) == 10, "RendererCapabilities ABI changed");
static_assert(offsetof(RendererCapabilities, pixel_motion_vectors) == 11, "RendererCapabilities ABI changed");
static_assert(offsetof(RendererCapabilities, late_close_screen_pass) == 12, "RendererCapabilities ABI changed");
static_assert(offsetof(RendererCapabilities, editor_readback) == 13, "RendererCapabilities ABI changed");
static_assert(sizeof(RendererCapabilities) == 16, "RendererCapabilities ABI v1 must be 16 bytes");
static_assert(alignof(RendererCapabilities) == 4, "RendererCapabilities ABI v1 alignment changed");
