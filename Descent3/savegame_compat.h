/*
 * Save-game disk compatibility helpers.
 *
 * The original format writes a handful of live structures verbatim.  Their
 * pointer members make their native layouts architecture-dependent.  Keep the
 * established 32-bit disk layout on Win64 so original saves remain readable
 * and saves created by the 64-bit engine remain readable by 32-bit builds.
 */
#ifndef D3_SAVEGAME_COMPAT_H
#define D3_SAVEGAME_COMPAT_H

#if defined(_WIN64)

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "object.h"
#include "player.h"

namespace savegame_compat {

constexpr std::size_t kPlayer32Size = 496;
constexpr std::size_t kRenderInfo32Size = 84;
constexpr std::size_t kAiFrame32Size = 3364;

using Player32Bytes = std::array<ubyte, kPlayer32Size>;
using RenderInfo32Bytes = std::array<ubyte, kRenderInfo32Size>;
using AiFrame32Bytes = std::array<ubyte, kAiFrame32Size>;

static_assert(sizeof(Inventory) == 24);
static_assert(sizeof(player) == 536);
static_assert(offsetof(player, inventory) == 312);
static_assert(offsetof(player, counter_measures) == 336);
static_assert(offsetof(player, guided_obj) == 448);
static_assert(offsetof(player, user_timeout_obj) == 456);
static_assert(offsetof(player, zoom_distance) == 464);
static_assert(sizeof(decltype(object::rtype)) == 104);
static_assert(sizeof(polyobj_info) == 104);
static_assert(offsetof(polyobj_info, multi_turret_info) == 56);
static_assert(offsetof(polyobj_info, subobj_flags) == 96);
static_assert(sizeof(ai_frame) == 3488);
static_assert(sizeof(goal) == 296);
static_assert(offsetof(ai_frame, goals) == 168);
static_assert(offsetof(ai_frame, target_handle) == 3128);

inline Player32Bytes PlayerTo32(const player &source) {
  Player32Bytes disk{};
  const auto *native = reinterpret_cast<const ubyte *>(&source);

  // Everything before the Inventory objects has the original layout.
  std::memcpy(disk.data(), native, 308);

  // The two Inventory objects are serialized separately.  Their three native
  // pointers must never leak into the raw structure image.
  std::memcpy(disk.data() + 332, native + 360, 88);

  // Only guided_obj's presence is represented in the raw image; its stable
  // object handle immediately follows the player structure in the file.
  const std::uint32_t guided_present = source.guided_obj ? 1u : 0u;
  std::memcpy(disk.data() + 420, &guided_present, sizeof(guided_present));

  // user_timeout_obj never had a companion handle in the save format.  Do not
  // persist an invalid address from either architecture.
  std::memcpy(disk.data() + 428, native + 464, 68);
  return disk;
}

inline bool PlayerFrom32(player &dest, const Player32Bytes &disk) {
  auto *native = reinterpret_cast<ubyte *>(&dest);

  std::memcpy(native, disk.data(), 308);
  // Preserve the reset Inventory objects at native offsets 312 and 336.
  std::memcpy(native + 360, disk.data() + 332, 88);

  std::uint32_t guided_present = 0;
  std::memcpy(&guided_present, disk.data() + 420, sizeof(guided_present));
  dest.guided_obj = guided_present ? reinterpret_cast<object *>(1) : nullptr;
  dest.user_timeout_obj = nullptr;

  std::memcpy(native + 464, disk.data() + 428, 68);
  return guided_present != 0;
}

inline RenderInfo32Bytes RenderInfoTo32(const object &source) {
  RenderInfo32Bytes disk{};
  const auto *native = reinterpret_cast<const ubyte *>(&source.rtype);

  if (!(source.flags & OF_POLYGON_OBJECT)) {
    std::memcpy(disk.data(), native, disk.size());
    return disk;
  }

  std::memcpy(disk.data(), native, 52);
  // multi_turret_info's pointer-owned arrays are rebuilt by the loader.
  std::memcpy(disk.data() + 76, native + 96, 8);
  return disk;
}

inline void RenderInfoFrom32(object &dest, const RenderInfo32Bytes &disk) {
  auto *native = reinterpret_cast<ubyte *>(&dest.rtype);
  std::memset(native, 0, sizeof(dest.rtype));

  if (!(dest.flags & OF_POLYGON_OBJECT)) {
    std::memcpy(native, disk.data(), disk.size());
    return;
  }

  std::memcpy(native, disk.data(), 52);
  std::memcpy(native + 96, disk.data() + 76, 8);
}

inline void GoalTo32(ubyte *disk, const goal &source) {
  const auto *native = reinterpret_cast<const ubyte *>(&source);
  std::memset(disk, 0, 284);
  std::memcpy(disk, native, 36);
  std::memcpy(disk + 36, native + 40, 28);
  if (source.type == AIG_SCRIPTED)
    std::memset(disk + 52, 0, sizeof(std::uint32_t));
  std::memcpy(disk + 64, native + 72, 220);
}

inline void GoalFrom32(goal &dest, const ubyte *disk) {
  auto *native = reinterpret_cast<ubyte *>(&dest);
  std::memset(native, 0, sizeof(dest));
  std::memcpy(native, disk, 36);
  std::memcpy(native + 40, disk + 36, 28);
  if (dest.type == AIG_SCRIPTED)
    dest.g_info.scripted_data_ptr = nullptr;
  std::memcpy(native + 72, disk + 64, 220);
}

inline AiFrame32Bytes AiFrameTo32(const ai_frame &source) {
  AiFrame32Bytes disk{};
  const auto *native = reinterpret_cast<const ubyte *>(&source);
  std::memcpy(disk.data(), native, 164);
  for (std::size_t i = 0; i < MAX_GOALS; ++i)
    GoalTo32(disk.data() + 164 + i * 284, source.goals[i]);
  std::memcpy(disk.data() + 3004, native + 3128, 360);
  return disk;
}

inline void AiFrameFrom32(ai_frame &dest, const AiFrame32Bytes &disk) {
  auto *native = reinterpret_cast<ubyte *>(&dest);
  std::memset(native, 0, sizeof(dest));
  std::memcpy(native, disk.data(), 164);
  for (std::size_t i = 0; i < MAX_GOALS; ++i)
    GoalFrom32(dest.goals[i], disk.data() + 164 + i * 284);
  std::memcpy(native + 3128, disk.data() + 3004, 360);
}

} // namespace savegame_compat

#endif // _WIN64
#endif // D3_SAVEGAME_COMPAT_H
