#include <windows.h>

#include "pipe.h"

#define __OSIRIS_IMPORT_H_
#include "osiris_import.h"

#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

using namespace osiris_bridge;

namespace {

HANDLE g_pipe = INVALID_HANDLE_VALUE;
uint32_t g_next_id = 1;
HMODULE g_module = nullptr;

using InitializeDLLProc = char(__stdcall *)(tOSIRISModuleInit *);
using ShutdownDLLProc = void(__stdcall *)();
using GetGOScriptIDProc = int(__stdcall *)(char *, ubyte);
using GetTriggerScriptIDProc = int(__stdcall *)(int, int);
using GetCOScriptListProc = int(__stdcall *)(int **, int **);
using CreateInstanceProc = void *(__stdcall *)(int);
using DestroyInstanceProc = void(__stdcall *)(int, void *);
using CallInstanceEventProc = short(__stdcall *)(int, void *, int, tOSIRISEventInfo *);
using SaveRestoreStateProc = int(__stdcall *)(void *, ubyte);

InitializeDLLProc g_initialize = nullptr;
ShutdownDLLProc g_shutdown = nullptr;
GetGOScriptIDProc g_get_go_id = nullptr;
GetTriggerScriptIDProc g_get_trigger_id = nullptr;
GetCOScriptListProc g_get_co_list = nullptr;
CreateInstanceProc g_create_instance = nullptr;
DestroyInstanceProc g_destroy_instance = nullptr;
CallInstanceEventProc g_call_event = nullptr;
SaveRestoreStateProc g_save_restore = nullptr;

struct HostMemory {
  void *pointer = nullptr;
  uint32_t size = 0;
};
std::unordered_map<uint32_t, HostMemory> g_memory;
uint32_t g_active_cinematic_callback = 0;

bool apply_memory_snapshot(Reader &reader) {
  uint32_t count = 0;
  if (!reader.pod(count) || count > 100000)
    return false;
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t token = 0, size = 0;
    if (!reader.pod(token) || !reader.pod(size) || size > kMaxPayload)
      return false;
    auto found = g_memory.find(token);
    if (found == g_memory.end()) {
      void *pointer = std::malloc(size ? size : 1);
      if (!pointer)
        return false;
      found = g_memory.emplace(token, HostMemory{pointer, size}).first;
    }
    if (found->second.size != size || reader.remaining < size)
      return false;
    if (!reader.bytes(found->second.pointer, size))
      return false;
  }
  return true;
}

void write_memory_snapshot(Writer &writer) {
  writer.pod(static_cast<uint32_t>(g_memory.size()));
  for (const auto &entry : g_memory) {
    writer.pod(entry.first);
    writer.pod(entry.second.size);
    if (entry.second.size)
      writer.bytes(entry.second.pointer, entry.second.size);
  }
}

bool handle_export(const Message &request, Message &response);

bool transact(Op op, const std::vector<uint8_t> &payload, Message &response) {
  Message request;
  request.header.kind = static_cast<uint32_t>(MessageKind::Request);
  request.header.op = static_cast<uint32_t>(op);
  request.header.id = g_next_id++;
  request.payload = payload;
  if (!send_message(g_pipe, request))
    return false;

  for (;;) {
    Message incoming;
    if (!receive_message(g_pipe, incoming))
      return false;
    if (incoming.header.kind == static_cast<uint32_t>(MessageKind::Response) &&
        incoming.header.id == request.header.id) {
      response = std::move(incoming);
      return response.header.status == 0;
    }
    if (incoming.header.kind != static_cast<uint32_t>(MessageKind::Request))
      return false;
    Message nested_response;
    nested_response.header.kind = static_cast<uint32_t>(MessageKind::Response);
    nested_response.header.op = incoming.header.op;
    nested_response.header.id = incoming.header.id;
    if (!handle_export(incoming, nested_response) && nested_response.header.status == 0)
      nested_response.header.status = -1;
    if (!send_message(g_pipe, nested_response))
      return false;
  }
}

bool callback(uint32_t index, Writer &args, Message &response) {
  Writer payload;
  payload.pod(index);
  payload.bytes(args.data.data(), args.data.size());
  return transact(Op::Callback, payload.data, response);
}

[[noreturn]] void unsupported_callback(uint32_t index) {
  Writer args;
  args.pod(index);
  Message ignored;
  callback(UINT32_MAX, args, ignored);
  ExitProcess(86);
}

template <uint32_t Index> uintptr_t __cdecl unsupported() {
  unsupported_callback(Index);
}

template <size_t... Indices>
std::array<int *, sizeof...(Indices)> unsupported_table(std::index_sequence<Indices...>) {
  return {reinterpret_cast<int *>(&unsupported<static_cast<uint32_t>(Indices)>)...};
}

void __cdecl bridge_mprintf(int level, char *format, ...) {
  char text[2048];
  va_list args;
  va_start(args, format);
  _vsnprintf(text, sizeof(text) - 1, format ? format : "", args);
  va_end(args);
  text[sizeof(text) - 1] = 0;
  Writer writer;
  writer.pod(level);
  writer.string(text);
  Message response;
  callback(0, writer, response);
}

void __cdecl bridge_msafe_call(int type, msafe_struct *value) {
  static_assert(sizeof(WireMsafe) == sizeof(msafe_struct), "Win32 msafe wire layout changed");
  Writer writer;
  writer.pod(type);
  WireMsafe wire{};
  if (value)
    std::memcpy(&wire, value, sizeof(wire));
  writer.pod(wire);
  if (type == MSAFE_OBJECT_DESTROY_ROBOTS_EXCEPT && value && value->list && value->count > 0 &&
      value->count <= 100000) {
    writer.pod(static_cast<uint32_t>(value->count));
    writer.bytes(value->list, static_cast<size_t>(value->count) * sizeof(tKillObjectItem));
  } else {
    writer.pod(uint32_t{0});
  }
  Message response;
  if (callback(1, writer, response) && value && response.payload.size() == sizeof(wire))
    std::memcpy(value, response.payload.data(), sizeof(wire));
}

void __cdecl bridge_cine_start_canned(tCannedCinematicInfo *info) {
  if (!info)
    return;
  WireCannedCinematicInfo wire{};
  wire.type = info->type;
  wire.camera_pathid = info->camera_pathid;
  wire.target_pathid = info->target_pathid;
  wire.target_objhandle = info->target_objhandle;
  wire.room = info->room;
  wire.time = info->time;
  wire.object_to_use_for_point = info->object_to_use_for_point;
  std::memcpy(&wire.pos, &info->pos, sizeof(wire.pos));
  std::memcpy(&wire.orient, &info->orient, sizeof(wire.orient));
  Writer writer;
  writer.pod(wire);
  writer.string(info->text_to_display);
  Message response;
  callback(110, writer, response);
}

bool __cdecl bridge_cine_start(tGameCinematic *info, char *text) {
  if (!info)
    return false;

  static_assert(sizeof(void *) == sizeof(uint32_t), "cinematic bridge host must be Win32");
  WireGameCinematic wire{};
  wire.flags = info->flags;
  wire.target_objhandle = info->target_objhandle;
  wire.end_transition = info->end_transition;
  wire.start_transition = info->start_transition;
  wire.pathid = info->pathid;
  std::memcpy(&wire.position, &info->position, sizeof(wire.position));
  if (info->orient) {
    wire.has_orient = 1;
    std::memcpy(&wire.orient, info->orient, sizeof(wire.orient));
  }
  wire.room = info->room;
  wire.max_time_play = info->max_time_play;
  std::memcpy(&wire.text_display, &info->text_display, sizeof(wire.text_display));
  std::memcpy(&wire.track_target, &info->track_target, sizeof(wire.track_target));
  std::memcpy(&wire.player_disabled, &info->player_disabled, sizeof(wire.player_disabled));
  std::memcpy(&wire.in_camera_view, &info->in_camera_view, sizeof(wire.in_camera_view));
  std::memcpy(&wire.quick_exit, &info->quick_exit, sizeof(wire.quick_exit));
  wire.callback_token = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(info->callback));

  const uint32_t previous_callback = g_active_cinematic_callback;
  g_active_cinematic_callback = wire.callback_token;
  Writer writer;
  writer.pod(wire);
  writer.string(text);
  Message response;
  uint8_t result = 0;
  if (callback(94, writer, response)) {
    Reader reader(response.payload);
    reader.pod(result);
  }
  if (!result)
    g_active_cinematic_callback = previous_callback;
  return result != 0;
}

void __cdecl bridge_cine_stop() {
  Writer writer;
  Message response;
  callback(95, writer, response);
}

uint32_t lgoal_value_capacity(char vtype) {
  switch (vtype) {
  case LGSV_PC_GOAL_NAME:
  case LGSV_PC_LOCATION_NAME:
  case LGSV_PC_DESCRIPTION:
  case LGSV_PC_COMPLETION_MESSAGE:
    return 256;
  case LGSV_C_GOAL_LIST:
  case LGSSV_C_ITEM_TYPE:
  case LGSSV_B_ITEM_DONE:
    return 1;
  default:
    return sizeof(int32_t);
  }
}

bool lgoal_value_is_string(char vtype) {
  return vtype >= LGSV_PC_GOAL_NAME && vtype <= LGSV_PC_COMPLETION_MESSAGE;
}

void __cdecl bridge_lgoal_value(char op, char vtype, void *pointer, int goal_index,
                                int item_index) {
  if (!pointer)
    return;
  const uint32_t capacity = lgoal_value_capacity(vtype);
  uint32_t input_size = 0;
  if (op != VF_GET) {
    input_size = lgoal_value_is_string(vtype)
                     ? static_cast<uint32_t>(strnlen(static_cast<const char *>(pointer), capacity - 1) + 1)
                     : capacity;
  }
  Writer writer;
  writer.pod(op);
  writer.pod(vtype);
  writer.pod(goal_index);
  writer.pod(item_index);
  writer.pod(input_size);
  if (input_size)
    writer.bytes(pointer, input_size);
  Message response;
  if (callback(115, writer, response) && op == VF_GET && response.payload.size() == capacity)
    std::memcpy(pointer, response.payload.data(), capacity);
}

void __cdecl bridge_msafe_get(int type, msafe_struct *value) {
  static_assert(sizeof(WireMsafe) == sizeof(msafe_struct), "Win32 msafe wire layout changed");
  Writer writer;
  writer.pod(type);
  WireMsafe wire{};
  if (value)
    std::memcpy(&wire, value, sizeof(wire));
  writer.pod(wire);
  Message response;
  if (callback(2, writer, response) && value && response.payload.size() == sizeof(wire))
    std::memcpy(value, response.payload.data(), sizeof(wire));
}

float __cdecl bridge_game_time() {
  Writer writer;
  Message response;
  float result = 0;
  if (callback(57, writer, response)) {
    Reader reader(response.payload);
    reader.pod(result);
  }
  return result;
}

float __cdecl bridge_frame_time() {
  Writer writer;
  Message response;
  float result = 0;
  if (callback(58, writer, response)) {
    Reader reader(response.payload);
    reader.pod(result);
  }
  return result;
}

void __cdecl bridge_hud_message(int handle, char *text) {
  Writer writer;
  writer.pod(handle);
  writer.string(text);
  Message response;
  callback(71, writer, response);
}

void __cdecl bridge_flag_set(int flag, ubyte value) {
  Writer writer;
  writer.pod(flag);
  writer.pod(value);
  Message response;
  callback(67, writer, response);
}

int __cdecl bridge_flag_get(int flag) {
  Writer writer;
  writer.pod(flag);
  Message response;
  int result = 0;
  if (callback(68, writer, response)) {
    Reader reader(response.payload);
    reader.pod(result);
  }
  return result;
}

template <uint32_t Index> int __cdecl bridge_find_name(char *name) {
  Writer writer;
  writer.string(name);
  Message response;
  int result = -1;
  if (callback(Index, writer, response)) {
    Reader reader(response.payload);
    reader.pod(result);
  }
  return result;
}

ubyte __cdecl bridge_room_valid(int room) {
  Writer writer;
  writer.pod(room);
  Message response;
  ubyte result = 0;
  if (callback(11, writer, response)) {
    Reader reader(response.payload);
    reader.pod(result);
  }
  return result;
}

template <uint32_t Index> int __cdecl bridge_int_query(int value) {
  Writer writer;
  writer.pod(value);
  Message response;
  int result = -1;
  if (callback(Index, writer, response)) {
    Reader reader(response.payload);
    reader.pod(result);
  }
  return result;
}

uint32_t value_size(uint32_t index, char vtype, char op, const void *pointer) {
  if (index == 30) {
    if (vtype >= AIV_C_MOVEMENT_TYPE && vtype <= AIV_C_CURRENT_WB_FIRING)
      return 1;
    if (vtype == AIV_V_VEC_TO_TARGET || vtype == AIV_V_LAST_SEE_TARGET_POS ||
        vtype == AIV_V_MOVEMENT_DIR || vtype == AIV_V_ROT_THRUST_VECTOR)
      return sizeof(vector);
    return 4;
  }
  if (index == 61) {
    if (vtype == OBJV_US_ID)
      return 2;
    if (vtype == OBJV_V_POS || (vtype >= OBJV_V_VELOCITY && vtype <= OBJV_V_ROTTHRUST))
      return sizeof(vector);
    if (vtype == OBJV_M_ORIENT)
      return sizeof(matrix);
    if (vtype == OBJV_C_CONTROL_TYPE || vtype == OBJV_C_MOVEMENT_TYPE ||
        (vtype >= OBJV_C_VIRUS_INFECTED && vtype <= OBJV_C_IS_CLOAKED))
      return 1;
    if (vtype == OBJV_PC_MARKER_MESSAGE || vtype == OBJV_S_NAME)
      return 256;
    if (vtype == OBJV_PI_HACK_FVI_IGNORE_LIST && op == VF_SET && pointer) {
      const int *items = static_cast<const int *>(pointer);
      for (uint32_t count = 1; count <= 100; ++count)
        if (items[count - 1] == -1)
          return count * sizeof(int);
      return 0;
    }
    return 4;
  }
  if (index == 62) {
    if (vtype == MTNV_C_ATTACH_TYPE || vtype == MTNV_C_CONTROL_TYPE ||
        vtype == MTNV_C_CREATION_EFFECT || vtype == MTNV_C_NUM_SPAWN_PTS ||
        vtype == MTNV_C_NUM_PROD_TYPES)
      return 1;
    if (vtype == MTNV_S_CREATION_TEXTURE)
      return 2;
    if (vtype == MTNV_V_CREATE_POINT)
      return sizeof(vector);
    if (vtype == MTNV_PC_NAME)
      return 256;
    return 4;
  }
  return 0;
}

template <uint32_t Index> void bridge_value(int handle, char op, char vtype, void *pointer, int item_index = 0) {
  const uint32_t size = value_size(Index, vtype, op, pointer);
  if (!size || size > 4096 || !pointer)
    return;
  Writer writer;
  writer.pod(handle);
  writer.pod(op);
  writer.pod(vtype);
  writer.pod(item_index);
  writer.pod(size);
  if (op != VF_GET)
    writer.bytes(pointer, size);
  Message response;
  if (callback(Index, writer, response) && op == VF_GET && response.payload.size() == size)
    std::memcpy(pointer, response.payload.data(), size);
}

void __cdecl bridge_ai_value(int handle, char op, char vtype, void *pointer) {
  bridge_value<30>(handle, op, vtype, pointer);
}

void __cdecl bridge_obj_value(int handle, char op, char vtype, void *pointer, int index) {
  bridge_value<61>(handle, op, vtype, pointer, index);
}

void __cdecl bridge_matcen_value(int handle, char op, char vtype, void *pointer, int index) {
  bridge_value<62>(handle, op, vtype, pointer, index);
}

int __cdecl bridge_ai_power_switch(int handle, ubyte power_on) {
  Writer writer;
  writer.pod(handle);
  writer.pod(power_on);
  Message response;
  int result = 0;
  if (callback(23, writer, response)) {
    Reader reader(response.payload);
    reader.pod(result);
  }
  return result;
}

void __cdecl bridge_custom_anim(int handle, float start, float end, float time, char flags,
                                int sound_handle, char next_anim_type) {
  Writer writer;
  writer.pod(handle);
  writer.pod(start);
  writer.pod(end);
  writer.pod(time);
  writer.pod(flags);
  writer.pod(sound_handle);
  writer.pod(next_anim_type);
  Message response;
  callback(70, writer, response);
}

int __cdecl bridge_attach_ap(int parent, char parent_ap, int child, char child_ap, ubyte aligned) {
  Writer writer;
  writer.pod(parent);
  writer.pod(parent_ap);
  writer.pod(child);
  writer.pod(child_ap);
  writer.pod(aligned);
  Message response;
  int result = 0;
  if (callback(15, writer, response)) {
    Reader reader(response.payload);
    reader.pod(result);
  }
  return result;
}

int __cdecl bridge_attach_child(int handle, char attach_point) {
  Writer writer;
  writer.pod(handle); writer.pod(attach_point);
  Message response;
  int result = -1;
  if (callback(14, writer, response)) {
    Reader reader(response.payload); reader.pod(result);
  }
  return result;
}

void __cdecl bridge_obj_kill(int handle, int killer, float damage, int flags,
                             float min_time, float max_time) {
  Writer writer;
  writer.pod(handle); writer.pod(killer); writer.pod(damage); writer.pod(flags);
  writer.pod(min_time); writer.pod(max_time);
  Message response;
  callback(117, writer, response);
}

int __cdecl bridge_follow_path_simple(int handle, int path, int guid, int flags, int slot) {
  Writer writer;
  writer.pod(handle); writer.pod(path); writer.pod(guid); writer.pod(flags); writer.pod(slot);
  Message response;
  int result = -1;
  if (callback(22, writer, response)) {
    Reader reader(response.payload);
    reader.pod(result);
  }
  return result;
}

float __cdecl bridge_ai_add_goal(int handle, int goal_type, int level, float influence,
                                 int guid, int flags, ...) {
  Writer writer;
  writer.pod(handle); writer.pod(goal_type); writer.pod(level); writer.pod(influence);
  writer.pod(guid); writer.pod(flags);
  va_list args;
  va_start(args, flags);
  switch (goal_type) {
  case AIG_GET_AWAY_FROM_OBJ: case AIG_GET_TO_OBJ: case AIG_GUARD_OBJ:
  case AIG_DODGE_OBJ: case AIG_MOVE_AROUND_OBJ: case AIG_MOVE_RELATIVE_OBJ:
  case AIG_GET_AROUND_OBJ: case AIG_FIRE_AT_OBJ:
    writer.pod(va_arg(args, int));
    break;
  case AIG_FOLLOW_PATH:
    for (int i = 0; i < 4; ++i) writer.pod(va_arg(args, int));
    break;
  case AIG_ATTACH_TO_OBJ: case AIG_PLACE_OBJ_ON_OBJ:
    writer.pod(va_arg(args, int)); writer.pod(va_arg(args, int)); writer.pod(va_arg(args, int));
    writer.pod(va_arg(args, double)); writer.pod(va_arg(args, int)); writer.pod(va_arg(args, int));
    break;
  case AIG_MOVE_RELATIVE_OBJ_VEC: case AIG_HIDE_FROM_OBJ:
    writer.pod(va_arg(args, int)); writer.pod(va_arg(args, int));
    break;
  case AIG_GUARD_AREA: case AIG_GET_TO_POS: {
    vector *position = va_arg(args, vector *);
    WireVector wire{};
    if (position) std::memcpy(&wire, position, sizeof(wire));
    writer.pod(wire); writer.pod(va_arg(args, int));
    break;
  }
  case AIG_FACE_DIR: {
    vector *direction = va_arg(args, vector *);
    WireVector wire{};
    if (direction) std::memcpy(&wire, direction, sizeof(wire));
    writer.pod(wire);
    break;
  }
  case AIG_MELEE_TARGET:
    break;
  default:
    va_end(args);
    return -1.0f;
  }
  va_end(args);
  Message response;
  int result = -1;
  if (callback(28, writer, response)) {
    Reader reader(response.payload);
    reader.pod(result);
  }
  return static_cast<float>(result);
}

char __cdecl bridge_game_difficulty() {
  Writer writer;
  Message response;
  char result = 0;
  if (callback(120, writer, response)) {
    Reader reader(response.payload);
    reader.pod(result);
  }
  return result;
}

int __cdecl bridge_game_language() {
  Writer writer;
  Message response;
  int result = 0;
  if (callback(121, writer, response)) {
    Reader reader(response.payload);
    reader.pod(result);
  }
  return result;
}

int __cdecl bridge_timer_create(tOSIRISTIMER *timer) {
  Writer writer;
  uint8_t bytes[24]{};
  static_assert(sizeof(tOSIRISTIMER) == sizeof(bytes));
  if (timer)
    std::memcpy(bytes, timer, sizeof(bytes));
  writer.bytes(bytes, sizeof(bytes));
  Message response;
  int result = -1;
  if (callback(54, writer, response)) {
    Reader reader(response.payload);
    reader.pod(result);
  }
  return result;
}

void __cdecl bridge_timer_cancel(int id) {
  Writer writer;
  writer.pod(id);
  Message response;
  callback(53, writer, response);
}

void *__cdecl bridge_memory_alloc(tOSIRISMEMCHUNK *chunk) {
  if (!chunk || chunk->size <= 0 || static_cast<uint32_t>(chunk->size) > kMaxPayload)
    return nullptr;
  WireMemoryChunk wire{};
  wire.script_type = chunk->my_id.type;
  wire.owner = chunk->my_id.type == OBJECT_SCRIPT ? chunk->my_id.objhandle :
               chunk->my_id.type == TRIGGER_SCRIPT ? chunk->my_id.triggernum : 0;
  wire.id = chunk->id;
  wire.size = static_cast<uint32_t>(chunk->size);
  Writer writer;
  writer.pod(wire);
  Message response;
  uint32_t token = 0;
  if (!callback(51, writer, response))
    return nullptr;
  Reader reader(response.payload);
  if (!reader.pod(token) || token == 0)
    return nullptr;
  void *pointer = std::malloc(wire.size);
  if (!pointer)
    return nullptr;
  g_memory[token] = {pointer, wire.size};
  return pointer;
}

void __cdecl bridge_memory_free(void *pointer) {
  for (auto it = g_memory.begin(); it != g_memory.end(); ++it) {
    if (it->second.pointer == pointer) {
      Writer writer;
      writer.pod(it->first);
      Message response;
      callback(52, writer, response);
      std::free(pointer);
      g_memory.erase(it);
      return;
    }
  }
}

ubyte __cdecl bridge_timer_exists(int id) {
  Writer writer;
  writer.pod(id);
  Message response;
  ubyte result = 0;
  if (callback(60, writer, response)) {
    Reader reader(response.payload);
    reader.pod(result);
  }
  return result;
}

template <typename T> T __cdecl bridge_file_read_scalar(void *file) {
  Writer writer;
  writer.pod(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(file)));
  Message response;
  T result{};
  if (callback(std::is_same_v<T, int> ? 38 : std::is_same_v<T, short> ? 39 :
                   std::is_same_v<T, sbyte> ? 40 : std::is_same_v<T, float> ? 41 : 42,
               writer, response)) {
    Reader reader(response.payload);
    reader.pod(result);
  }
  return result;
}

int __cdecl bridge_file_read_bytes(ubyte *buffer, int count, void *file) {
  Writer writer;
  writer.pod(count);
  writer.pod(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(file)));
  Message response;
  int result = 0;
  if (callback(37, writer, response)) {
    Reader reader(response.payload);
    if (reader.pod(result) && result > 0 && result <= count)
      reader.bytes(buffer, result);
  }
  return result;
}

int __cdecl bridge_file_read_string(char *buffer, size_t capacity, void *file) {
  Writer writer;
  writer.pod(static_cast<uint32_t>(capacity));
  writer.pod(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(file)));
  Message response;
  int result = 0;
  std::string text;
  if (callback(43, writer, response)) {
    Reader reader(response.payload);
    if (reader.pod(result) && reader.string(text) && buffer && capacity) {
      const size_t size = (std::min)(capacity - 1, text.size());
      std::memcpy(buffer, text.data(), size);
      buffer[size] = 0;
    }
  }
  return result;
}

int __cdecl bridge_file_write_bytes(const ubyte *buffer, int count, void *file) {
  Writer writer;
  writer.pod(count);
  writer.pod(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(file)));
  if (count > 0)
    writer.bytes(buffer, count);
  Message response;
  int result = 0;
  if (callback(44, writer, response)) {
    Reader reader(response.payload);
    reader.pod(result);
  }
  return result;
}

int __cdecl bridge_file_write_string(const char *text, void *file) {
  Writer writer;
  writer.pod(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(file)));
  writer.string(text);
  Message response;
  int result = 0;
  if (callback(45, writer, response)) {
    Reader reader(response.payload);
    reader.pod(result);
  }
  return result;
}

template <typename T, uint32_t Index> void __cdecl bridge_file_write_scalar(T value, void *file) {
  Writer writer;
  writer.pod(value);
  writer.pod(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(file)));
  Message response;
  callback(Index, writer, response);
}

void *__cdecl bridge_file_open(const char *filename, const char *mode) {
  Writer writer;
  writer.string(filename);
  writer.string(mode);
  Message response;
  uint32_t token = 0;
  if (callback(75, writer, response)) {
    Reader reader(response.payload);
    reader.pod(token);
  }
  return reinterpret_cast<void *>(static_cast<uintptr_t>(token));
}

void __cdecl bridge_file_close(void *file) {
  Writer writer;
  writer.pod(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(file)));
  Message response;
  callback(76, writer, response);
}

int __cdecl bridge_file_tell(void *file) {
  Writer writer;
  writer.pod(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(file)));
  Message response;
  int result = 0;
  if (callback(77, writer, response)) {
    Reader reader(response.payload);
    reader.pod(result);
  }
  return result;
}

ubyte __cdecl bridge_file_eof(void *file) {
  Writer writer;
  writer.pod(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(file)));
  Message response;
  ubyte result = 1;
  if (callback(78, writer, response)) {
    Reader reader(response.payload);
    reader.pod(result);
  }
  return result;
}

FARPROC find_export(const char *name, int stack_bytes) {
  if (auto proc = GetProcAddress(g_module, name))
    return proc;
  char decorated[128];
  std::snprintf(decorated, sizeof(decorated), "_%s@%d", name, stack_bytes);
  return GetProcAddress(g_module, decorated);
}

bool load_module(const std::wstring &path) {
  g_module = LoadLibraryW(path.c_str());
  if (!g_module)
    return false;
  g_initialize = reinterpret_cast<InitializeDLLProc>(find_export("InitializeDLL", 4));
  g_shutdown = reinterpret_cast<ShutdownDLLProc>(find_export("ShutdownDLL", 0));
  g_get_go_id = reinterpret_cast<GetGOScriptIDProc>(find_export("GetGOScriptID", 8));
  g_get_trigger_id = reinterpret_cast<GetTriggerScriptIDProc>(find_export("GetTriggerScriptID", 8));
  g_get_co_list = reinterpret_cast<GetCOScriptListProc>(find_export("GetCOScriptList", 8));
  g_create_instance = reinterpret_cast<CreateInstanceProc>(find_export("CreateInstance", 4));
  g_destroy_instance = reinterpret_cast<DestroyInstanceProc>(find_export("DestroyInstance", 8));
  g_call_event = reinterpret_cast<CallInstanceEventProc>(find_export("CallInstanceEvent", 16));
  g_save_restore = reinterpret_cast<SaveRestoreStateProc>(find_export("SaveRestoreState", 8));
  return g_initialize && g_shutdown && g_get_go_id && g_create_instance && g_destroy_instance &&
         g_call_event && g_save_restore;
}

bool handle_export(const Message &request, Message &response) {
  Reader reader(request.payload);
  Writer writer;
  switch (static_cast<Op>(request.header.op)) {
  case Op::Hello: {
    writer.pod(kProtocolVersion);
    writer.pod(static_cast<uint32_t>(sizeof(void *)));
    break;
  }
  case Op::Initialize: {
    int module_id = 0;
    uint32_t checksum = 0;
    std::string identifier;
    uint32_t string_count = 0;
    if (!reader.pod(module_id) || !reader.pod(checksum) || !reader.string(identifier) ||
        !reader.pod(string_count))
      return false;
    std::vector<std::string> strings(string_count);
    std::vector<char *> string_ptrs(string_count);
    for (uint32_t i = 0; i < string_count; ++i) {
      if (!reader.string(strings[i]))
        return false;
      string_ptrs[i] = strings[i].data();
    }
    auto functions = unsupported_table(std::make_index_sequence<MAX_MODULEFUNCS>{});
    functions[0] = reinterpret_cast<int *>(&bridge_mprintf);
    functions[1] = reinterpret_cast<int *>(&bridge_msafe_call);
    functions[2] = reinterpret_cast<int *>(&bridge_msafe_get);
    functions[5] = reinterpret_cast<int *>(&bridge_find_name<5>);
    functions[6] = reinterpret_cast<int *>(&bridge_find_name<6>);
    functions[7] = reinterpret_cast<int *>(&bridge_find_name<7>);
    functions[11] = reinterpret_cast<int *>(&bridge_room_valid);
    functions[14] = reinterpret_cast<int *>(&bridge_attach_child);
    functions[15] = reinterpret_cast<int *>(&bridge_attach_ap);
    functions[21] = reinterpret_cast<int *>(&bridge_find_name<21>);
    functions[22] = reinterpret_cast<int *>(&bridge_follow_path_simple);
    functions[23] = reinterpret_cast<int *>(&bridge_ai_power_switch);
    functions[28] = reinterpret_cast<int *>(&bridge_ai_add_goal);
    functions[30] = reinterpret_cast<int *>(&bridge_ai_value);
    functions[37] = reinterpret_cast<int *>(&bridge_file_read_bytes);
    functions[38] = reinterpret_cast<int *>(&bridge_file_read_scalar<int>);
    functions[39] = reinterpret_cast<int *>(&bridge_file_read_scalar<short>);
    functions[40] = reinterpret_cast<int *>(&bridge_file_read_scalar<sbyte>);
    functions[41] = reinterpret_cast<int *>(&bridge_file_read_scalar<float>);
    functions[42] = reinterpret_cast<int *>(&bridge_file_read_scalar<double>);
    functions[43] = reinterpret_cast<int *>(&bridge_file_read_string);
    functions[44] = reinterpret_cast<int *>(&bridge_file_write_bytes);
    functions[45] = reinterpret_cast<int *>(&bridge_file_write_string);
    functions[46] = reinterpret_cast<int *>(&bridge_file_write_scalar<int, 46>);
    functions[47] = reinterpret_cast<int *>(&bridge_file_write_scalar<short, 47>);
    functions[48] = reinterpret_cast<int *>(&bridge_file_write_scalar<sbyte, 48>);
    functions[49] = reinterpret_cast<int *>(&bridge_file_write_scalar<float, 49>);
    functions[50] = reinterpret_cast<int *>(&bridge_file_write_scalar<double, 50>);
    functions[51] = reinterpret_cast<int *>(&bridge_memory_alloc);
    functions[52] = reinterpret_cast<int *>(&bridge_memory_free);
    functions[53] = reinterpret_cast<int *>(&bridge_timer_cancel);
    functions[54] = reinterpret_cast<int *>(&bridge_timer_create);
    functions[57] = reinterpret_cast<int *>(&bridge_game_time);
    functions[58] = reinterpret_cast<int *>(&bridge_frame_time);
    functions[60] = reinterpret_cast<int *>(&bridge_timer_exists);
    functions[61] = reinterpret_cast<int *>(&bridge_obj_value);
    functions[62] = reinterpret_cast<int *>(&bridge_matcen_value);
    functions[67] = reinterpret_cast<int *>(&bridge_flag_set);
    functions[68] = reinterpret_cast<int *>(&bridge_flag_get);
    functions[70] = reinterpret_cast<int *>(&bridge_custom_anim);
    functions[71] = reinterpret_cast<int *>(&bridge_hud_message);
    functions[75] = reinterpret_cast<int *>(&bridge_file_open);
    functions[76] = reinterpret_cast<int *>(&bridge_file_close);
    functions[77] = reinterpret_cast<int *>(&bridge_file_tell);
    functions[78] = reinterpret_cast<int *>(&bridge_file_eof);
    functions[82] = reinterpret_cast<int *>(&bridge_find_name<82>);
    functions[94] = reinterpret_cast<int *>(&bridge_cine_start);
    functions[95] = reinterpret_cast<int *>(&bridge_cine_stop);
    functions[96] = reinterpret_cast<int *>(&bridge_find_name<96>);
    functions[97] = reinterpret_cast<int *>(&bridge_find_name<97>);
    functions[98] = reinterpret_cast<int *>(&bridge_find_name<98>);
    functions[99] = reinterpret_cast<int *>(&bridge_find_name<99>);
    functions[100] = reinterpret_cast<int *>(&bridge_int_query<100>);
    functions[101] = reinterpret_cast<int *>(&bridge_int_query<101>);
    functions[102] = reinterpret_cast<int *>(&bridge_find_name<102>);
    functions[103] = reinterpret_cast<int *>(&bridge_find_name<103>);
    functions[110] = reinterpret_cast<int *>(&bridge_cine_start_canned);
    functions[111] = reinterpret_cast<int *>(&bridge_find_name<111>);
    functions[112] = reinterpret_cast<int *>(&bridge_find_name<112>);
    functions[113] = reinterpret_cast<int *>(&bridge_find_name<113>);
    functions[114] = reinterpret_cast<int *>(&bridge_find_name<114>);
    functions[115] = reinterpret_cast<int *>(&bridge_lgoal_value);
    functions[117] = reinterpret_cast<int *>(&bridge_obj_kill);
    functions[120] = reinterpret_cast<int *>(&bridge_game_difficulty);
    functions[121] = reinterpret_cast<int *>(&bridge_game_language);

    tOSIRISModuleInit init{};
    std::copy(functions.begin(), functions.end(), init.fp);
    init.string_table = string_ptrs.empty() ? nullptr : string_ptrs.data();
    init.string_count = static_cast<int>(string_count);
    init.module_identifier = module_id;
    init.module_is_static = false;
    init.script_identifier = identifier.data();
    init.game_checksum = checksum;
    const char result = g_initialize(&init);
    writer.pod(result);
    writer.pod(static_cast<uint8_t>(init.module_is_static));
    break;
  }
  case Op::Shutdown:
    g_shutdown();
    break;
  case Op::GetGOScriptID: {
    std::string name;
    ubyte is_door = 0;
    if (!reader.string(name) || !reader.pod(is_door))
      return false;
    writer.pod(g_get_go_id(name.data(), is_door));
    break;
  }
  case Op::GetTriggerScriptID: {
    int room = 0, face = 0;
    if (!g_get_trigger_id || !reader.pod(room) || !reader.pod(face))
      return false;
    writer.pod(g_get_trigger_id(room, face));
    break;
  }
  case Op::GetCOScriptList: {
    if (!g_get_co_list)
      return false;
    int *handles = nullptr, *ids = nullptr;
    const int count = g_get_co_list(&handles, &ids);
    writer.pod(count);
    for (int i = 0; i < count; ++i) {
      writer.pod(handles[i]);
      writer.pod(ids[i]);
    }
    break;
  }
  case Op::CreateInstance: {
    int id = 0;
    if (!reader.pod(id))
      return false;
    writer.pod(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_create_instance(id))));
    break;
  }
  case Op::DestroyInstance: {
    int id = 0;
    uint32_t instance = 0;
    if (!reader.pod(id) || !reader.pod(instance))
      return false;
    g_destroy_instance(id, reinterpret_cast<void *>(static_cast<uintptr_t>(instance)));
    break;
  }
  case Op::CallInstanceEvent: {
    int id = 0, event = 0;
    uint32_t instance = 0;
    WireEventInfo wire{};
    if (!reader.pod(id) || !reader.pod(instance) || !reader.pod(event) || !reader.pod(wire) ||
        reader.remaining < wire.extra_size)
      return false;
    tOSIRISEventInfo info{};
    std::memcpy(&info, wire.event_data, sizeof(wire.event_data));
    info.me_handle = wire.me_handle;
    std::vector<uint8_t> extra(wire.extra_size);
    if (wire.extra_size && !reader.bytes(extra.data(), extra.size()))
      return false;
    info.extra_info = extra.empty() ? nullptr : extra.data();
    if (!apply_memory_snapshot(reader))
      return false;
    if (event == EVT_MEMRESTORE) {
      uint32_t token = 0;
      std::memcpy(&token, wire.event_data + sizeof(info.evt_memrestore.id), sizeof(token));
      const auto found = g_memory.find(token);
      if (found == g_memory.end())
        return false;
      info.evt_memrestore.memory_ptr = found->second.pointer;
    }
    writer.pod(g_call_event(id, reinterpret_cast<void *>(static_cast<uintptr_t>(instance)), event, &info));
    write_memory_snapshot(writer);
    break;
  }
  case Op::SaveRestoreState: {
    uint32_t file_token = 0;
    ubyte saving = 0;
    if (!reader.pod(file_token) || !reader.pod(saving))
      return false;
    writer.pod(g_save_restore(reinterpret_cast<void *>(static_cast<uintptr_t>(file_token)), saving));
    break;
  }
  case Op::ForgetMemory: {
    uint32_t count = 0;
    if (!reader.pod(count) || count > 100000)
      return false;
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t token = 0;
      if (!reader.pod(token))
        return false;
      const auto found = g_memory.find(token);
      if (found != g_memory.end()) {
        std::free(found->second.pointer);
        g_memory.erase(found);
      }
    }
    break;
  }
  case Op::CinematicCallback: {
    uint32_t callback_token = 0;
    int type = 0;
    if (!reader.pod(callback_token) || !reader.pod(type) || !callback_token ||
        callback_token != g_active_cinematic_callback)
      return false;
    auto callback_proc = reinterpret_cast<void (__cdecl *)(int)>(
        static_cast<uintptr_t>(callback_token));
    callback_proc(type);
    if (type == GCCT_STOP && g_active_cinematic_callback == callback_token)
      g_active_cinematic_callback = 0;
    break;
  }
  default:
    return false;
  }
  response.payload = std::move(writer.data);
  return true;
}

std::wstring argument(int argc, wchar_t **argv, const wchar_t *name) {
  for (int i = 1; i + 1 < argc; ++i)
    if (_wcsicmp(argv[i], name) == 0)
      return argv[i + 1];
  return {};
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  const std::wstring pipe_name = argument(argc, argv, L"--pipe");
  const std::wstring module_path = argument(argc, argv, L"--module");
  if (pipe_name.empty() || module_path.empty() || !load_module(module_path))
    return 2;

  g_pipe = CreateFileW(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  if (g_pipe == INVALID_HANDLE_VALUE)
    return 3;

  for (;;) {
    Message request;
    if (!receive_message(g_pipe, request))
      break;
    if (request.header.kind != static_cast<uint32_t>(MessageKind::Request))
      return 4;
    Message response;
    response.header.kind = static_cast<uint32_t>(MessageKind::Response);
    response.header.op = request.header.op;
    response.header.id = request.header.id;
    if (!handle_export(request, response))
      response.header.status = -1;
    if (!send_message(g_pipe, response))
      break;
  }

  if (g_module)
    FreeLibrary(g_module);
  if (g_pipe != INVALID_HANDLE_VALUE)
    CloseHandle(g_pipe);
  return 0;
}
