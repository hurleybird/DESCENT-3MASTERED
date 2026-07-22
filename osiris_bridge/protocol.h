#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace osiris_bridge {

constexpr uint32_t kMagic = 0x3242534f; // "OSB2"
constexpr uint32_t kProtocolVersion = 1;
constexpr uint32_t kMaxPayload = 16 * 1024 * 1024;

enum class MessageKind : uint32_t {
  Request = 1,
  Response = 2,
};

enum class Op : uint32_t {
  Hello = 1,
  Initialize,
  Shutdown,
  GetGOScriptID,
  GetTriggerScriptID,
  GetCOScriptList,
  CreateInstance,
  DestroyInstance,
  CallInstanceEvent,
  SaveRestoreState,
  ForgetMemory,
  Callback = 0x1000,
};

#pragma pack(push, 1)
struct MessageHeader {
  uint32_t magic;
  uint32_t version;
  uint32_t kind;
  uint32_t op;
  uint32_t id;
  uint32_t payload_size;
  int32_t status;
};
#pragma pack(pop)

struct Message {
  MessageHeader header{};
  std::vector<uint8_t> payload;
};

class Writer {
public:
  template <typename T> void pod(const T &value) {
    static_assert(std::is_trivially_copyable<T>::value, "wire value must be trivially copyable");
    const auto *p = reinterpret_cast<const uint8_t *>(&value);
    data.insert(data.end(), p, p + sizeof(T));
  }

  void bytes(const void *ptr, size_t size) {
    const auto *p = reinterpret_cast<const uint8_t *>(ptr);
    data.insert(data.end(), p, p + size);
  }

  void string(const char *value) {
    const uint32_t size = value ? static_cast<uint32_t>(std::strlen(value)) : 0;
    pod(size);
    if (size)
      bytes(value, size);
  }

  std::vector<uint8_t> data;
};

class Reader {
public:
  explicit Reader(const std::vector<uint8_t> &bytes) : ptr(bytes.data()), remaining(bytes.size()) {}

  template <typename T> bool pod(T &value) {
    static_assert(std::is_trivially_copyable<T>::value, "wire value must be trivially copyable");
    if (remaining < sizeof(T))
      return false;
    std::memcpy(&value, ptr, sizeof(T));
    ptr += sizeof(T);
    remaining -= sizeof(T);
    return true;
  }

  bool bytes(void *value, size_t size) {
    if (remaining < size)
      return false;
    if (size)
      std::memcpy(value, ptr, size);
    ptr += size;
    remaining -= size;
    return true;
  }

  bool string(std::string &value) {
    uint32_t size = 0;
    if (!pod(size) || remaining < size)
      return false;
    value.assign(reinterpret_cast<const char *>(ptr), size);
    ptr += size;
    remaining -= size;
    return true;
  }

  const uint8_t *ptr;
  size_t remaining;
};

#pragma pack(push, 4)
struct WireVector {
  float x, y, z;
};

struct WireMatrix {
  WireVector rvec, uvec, fvec;
};

// Pointer-free representation of the retail Win32 msafe_struct.  `list` is
// deliberately an opaque token; operations that use it need explicit
// marshalling rather than passing a process-local address.
struct WireMsafe {
  int32_t roomnum;
  int16_t facenum, texnum, portalnum;
  float fog_r, fog_g, fog_b, fog_depth;
  WireVector wind;
  uint8_t pulse_time, pulse_offset;
  uint32_t objhandle, ithandle;
  float shields, energy;
  int16_t start_tick, end_tick;
  float cycle_time;
  int32_t type, id, aux_type, aux_id;
  uint32_t checksum;
  int32_t path_id;
  float amount;
  uint8_t damage_type;
  uint32_t killer_handle;
  float ammo;
  uint8_t playsound, remove, do_powerup;
  WireVector velocity, rot_velocity;
  float rot_drag;
  WireVector thrust, rot_thrust;
  int8_t control_type, movement_type;
  float creation_time;
  int32_t physics_flags;
  WireVector pos;
  WireMatrix orient;
  float anim_frame;
  uint8_t is_real, random, unused2;
  int8_t gunpoint;
  uint8_t effect_type, phys_info;
  float drag, mass;
  uint16_t unused;
  int32_t randval;
  uint8_t trigger_num;
  int32_t sound_handle;
  float volume;
  int32_t index;
  float scalar, interval;
  uint8_t state;
  int8_t slot;
  char message[255];
  char name[32];
  int32_t color;
  float longevity, lifetime, size, speed;
  int32_t count, flags;
  uint32_t list;
  uint32_t control_mask;
  uint8_t control_val;
  float light_distance;
  float r1, g1, b1, r2, g2, b2;
  float time_interval, flicker_distance, directional_dot;
  int32_t timebits;
  WireVector pos2;
  char message2[255];
};

struct WireEventInfo {
  uint8_t event_data[20];
  int32_t me_handle;
  uint32_t extra_size;
};

// Pointer-free form of the retail Win32 tCannedCinematicInfo.  The optional
// text follows this structure as a protocol string.
struct WireCannedCinematicInfo {
  int32_t type;
  int32_t camera_pathid;
  int32_t target_pathid;
  int32_t target_objhandle;
  int32_t room;
  float time;
  int32_t object_to_use_for_point;
  WireVector pos;
  WireMatrix orient;
};

struct WireMemoryChunk {
  int32_t script_type;
  int32_t owner;
  uint16_t id;
  uint32_t size;
};
#pragma pack(pop)

} // namespace osiris_bridge
