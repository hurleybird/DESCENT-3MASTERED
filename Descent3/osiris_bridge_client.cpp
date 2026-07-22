#include "osiris_bridge_client.h"
bool Osiris_GetMemoryInfo(void *mem_ptr, tOSIRISMEMCHUNK *chunk);

#if defined(_WIN64)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../osiris_bridge/pipe.h"
void AutomatedCaptureLog(const char *format, ...);
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

using namespace osiris_bridge;

namespace {

std::atomic<uint32_t> g_pipe_serial{1};

void bridge_log(const char *format, ...) {
  char text[1024];
  va_list args;
  va_start(args, format);
  _vsnprintf(text, sizeof(text) - 1, format, args);
  va_end(args);
  text[sizeof(text) - 1] = 0;
  OutputDebugStringA(text);
}

std::wstring widen(const char *text) {
  if (!text)
    return {};
  const int count = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
  if (count <= 0)
    return {};
  std::wstring result(static_cast<size_t>(count), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text, -1, &result[0], count);
  result.pop_back();
  return result;
}

std::wstring quote(const std::wstring &text) {
  std::wstring result = L"\"";
  size_t slashes = 0;
  for (wchar_t ch : text) {
    if (ch == L'\\') {
      ++slashes;
    } else if (ch == L'\"') {
      result.append(slashes * 2 + 1, L'\\');
      result.push_back(ch);
      slashes = 0;
    } else {
      result.append(slashes, L'\\');
      slashes = 0;
      result.push_back(ch);
    }
  }
  result.append(slashes * 2, L'\\');
  result.push_back(L'\"');
  return result;
}

void wire_to_msafe(const WireMsafe &wire, msafe_struct &native) {
  static_assert(offsetof(WireMsafe, list) < offsetof(WireMsafe, control_mask));
  std::memset(&native, 0, sizeof(native));
  std::memcpy(&native, &wire, offsetof(WireMsafe, list));
  native.list = nullptr;
  std::memcpy(&native.control_mask, &wire.control_mask,
              sizeof(WireMsafe) - offsetof(WireMsafe, control_mask));
}

void msafe_to_wire(const msafe_struct &native, WireMsafe &wire) {
  std::memset(&wire, 0, sizeof(wire));
  std::memcpy(&wire, &native, offsetof(WireMsafe, list));
  wire.list = 0;
  std::memcpy(&wire.control_mask, &native.control_mask,
              sizeof(WireMsafe) - offsetof(WireMsafe, control_mask));
}

template <typename Proc> Proc callback_proc(const tOSIRISModuleInit &init, uint32_t index) {
  return reinterpret_cast<Proc>(init.fp[index]);
}

} // namespace

struct OsirisBridgeClient::Impl {
  HANDLE pipe = INVALID_HANDLE_VALUE;
  HANDLE process = nullptr;
  HANDLE process_thread = nullptr;
  uint32_t next_id = 1;
  tOSIRISModuleInit callbacks{};
  void *active_file = nullptr;
  uint32_t next_file_token = 2;
  std::unordered_map<uint32_t, void *> files;
  struct BridgeMemory {
    void *pointer = nullptr;
    uint32_t size = 0;
    WireMemoryChunk chunk{};
  };
  uint32_t next_memory_token = 1;
  std::unordered_map<uint32_t, BridgeMemory> memory;
  std::vector<int> co_handles;
  std::vector<int> co_ids;

  uint32_t memory_token(void *pointer, uint32_t size, const WireMemoryChunk &chunk) {
    for (const auto &entry : memory)
      if (entry.second.pointer == pointer)
        return entry.first;
    const uint32_t token = next_memory_token++;
    memory[token] = {pointer, size, chunk};
    return token;
  }

  void write_memory_snapshot(Writer &writer) const {
    writer.pod(static_cast<uint32_t>(memory.size()));
    for (const auto &entry : memory) {
      writer.pod(entry.first);
      writer.pod(entry.second.size);
      if (entry.second.size)
        writer.bytes(entry.second.pointer, entry.second.size);
    }
  }

  bool apply_memory_snapshot(Reader &reader) {
    uint32_t count = 0;
    if (!reader.pod(count) || count > 100000)
      return false;
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t token = 0, size = 0;
      if (!reader.pod(token) || !reader.pod(size) || size > kMaxPayload)
        return false;
      const auto found = memory.find(token);
      if (found == memory.end() || found->second.size != size || reader.remaining < size)
        return false;
      if (!reader.bytes(found->second.pointer, size))
        return false;
    }
    return true;
  }

  void *resolve_file(uint32_t token) const {
    if (token == 1)
      return active_file;
    const auto found = files.find(token);
    return found == files.end() ? nullptr : found->second;
  }

  ~Impl() {
    if (pipe != INVALID_HANDLE_VALUE)
      CloseHandle(pipe);
    if (process) {
      if (WaitForSingleObject(process, 100) == WAIT_TIMEOUT)
        TerminateProcess(process, 0);
      CloseHandle(process);
    }
    if (process_thread)
      CloseHandle(process_thread);
  }

  bool dispatch_callback(const Message &request, Message &response) {
    Reader reader(request.payload);
    Writer writer;
    uint32_t index = 0;
    if (!reader.pod(index))
      return false;
    if (index == UINT32_MAX) {
      uint32_t unsupported_index = UINT32_MAX;
      reader.pod(unsupported_index);
      bridge_log("OSIRIS bridge: legacy module called unsupported callback %u\n", unsupported_index);
      AutomatedCaptureLog("osiris bridge unsupported callback=%u", unsupported_index);
      response.header.status = -2;
      return true;
    }

    switch (index) {
    case 0: {
      int level = 0;
      std::string text;
      if (!reader.pod(level) || !reader.string(text))
        return false;
      callback_proc<void (*)(int, char *, ...)>(callbacks, 0)(level, const_cast<char *>("%s"), text.c_str());
      if (text.rfind("BRIDGE_TEST", 0) == 0)
        AutomatedCaptureLog("%s", text.c_str());
      break;
    }
    case 1:
    case 2: {
      int type = 0;
      WireMsafe wire{};
      if (!reader.pod(type) || !reader.pod(wire))
        return false;
      uint32_t list_count = 0;
      if (index == 1 && !reader.pod(list_count))
        return false;
      msafe_struct native{};
      wire_to_msafe(wire, native);
      std::vector<tKillObjectItem> kill_items;
      if (type == MSAFE_OBJECT_DESTROY_ROBOTS_EXCEPT) {
        if (index != 1 || list_count > 100000 || list_count != static_cast<uint32_t>(native.count))
          return false;
        kill_items.resize(list_count);
        if (list_count && !reader.bytes(kill_items.data(), kill_items.size() * sizeof(kill_items[0])))
          return false;
        native.list = kill_items.empty() ? nullptr : kill_items.data();
      } else if (index == 1 && list_count != 0) {
        return false;
      }
      callback_proc<void (*)(int, msafe_struct *)>(callbacks, index)(type, &native);
      msafe_to_wire(native, wire);
      writer.pod(wire);
      break;
    }
    case 5: {
      std::string name;
      if (!reader.string(name))
        return false;
      callback_proc<void (*)(char *)>(callbacks, index)(&name[0]);
      break;
    }
    case 6:
    case 7:
    case 21:
    case 82:
    case 96:
    case 97:
    case 98:
    case 99:
    case 102:
    case 103:
    case 111:
    case 112:
    case 113:
    case 114: {
      std::string name;
      if (!reader.string(name))
        return false;
      writer.pod(callback_proc<int (*)(char *)>(callbacks, index)(&name[0]));
      break;
    }
    case 11: {
      int room = 0;
      if (!reader.pod(room))
        return false;
      writer.pod(callback_proc<ubyte (*)(int)>(callbacks, index)(room));
      break;
    }
    case 23: {
      int handle = 0;
      ubyte power_on = 0;
      if (!reader.pod(handle) || !reader.pod(power_on))
        return false;
      writer.pod(callback_proc<int (*)(int, ubyte)>(callbacks, index)(handle, power_on));
      break;
    }
    case 15: {
      int parent = 0, child = 0;
      char parent_ap = 0, child_ap = 0;
      ubyte aligned = 0;
      if (!reader.pod(parent) || !reader.pod(parent_ap) || !reader.pod(child) ||
          !reader.pod(child_ap) || !reader.pod(aligned))
        return false;
      writer.pod(callback_proc<int (*)(int, char, int, char, ubyte)>(callbacks, index)(
          parent, parent_ap, child, child_ap, aligned));
      break;
    }
    case 14: {
      int handle = 0; char attach_point = 0;
      if (!reader.pod(handle) || !reader.pod(attach_point))
        return false;
      writer.pod(callback_proc<int (*)(int, char)>(callbacks, index)(handle, attach_point));
      break;
    }
    case 22: {
      int handle = 0, path = 0, guid = 0, flags = 0, slot = 0;
      if (!reader.pod(handle) || !reader.pod(path) || !reader.pod(guid) ||
          !reader.pod(flags) || !reader.pod(slot))
        return false;
      writer.pod(callback_proc<int (*)(int, int, int, int, int)>(callbacks, index)(
          handle, path, guid, flags, slot));
      break;
    }
    case 28: {
      int handle = 0, goal_type = 0, level = 0, guid = 0, flags = 0;
      float influence = 0;
      if (!reader.pod(handle) || !reader.pod(goal_type) || !reader.pod(level) ||
          !reader.pod(influence) || !reader.pod(guid) || !reader.pod(flags))
        return false;
      auto proc = callback_proc<int (*)(int, int, int, float, int, int, ...)>(callbacks, index);
      int result = -1;
      switch (goal_type) {
      case AIG_GET_AWAY_FROM_OBJ: case AIG_GET_TO_OBJ: case AIG_GUARD_OBJ:
      case AIG_DODGE_OBJ: case AIG_MOVE_AROUND_OBJ: case AIG_MOVE_RELATIVE_OBJ:
      case AIG_GET_AROUND_OBJ: case AIG_FIRE_AT_OBJ: {
        int value = 0; if (!reader.pod(value)) return false;
        result = proc(handle, goal_type, level, influence, guid, flags, value); break;
      }
      case AIG_FOLLOW_PATH: {
        int a=0,b=0,c=0,d=0;
        if (!reader.pod(a)||!reader.pod(b)||!reader.pod(c)||!reader.pod(d)) return false;
        result = proc(handle, goal_type, level, influence, guid, flags, a,b,c,d); break;
      }
      case AIG_ATTACH_TO_OBJ: case AIG_PLACE_OBJ_ON_OBJ: {
        int object=0,parent_ap=0,child_ap=0,aligned=0,sphere=0; double radius=0;
        if (!reader.pod(object)||!reader.pod(parent_ap)||!reader.pod(child_ap)||
            !reader.pod(radius)||!reader.pod(aligned)||!reader.pod(sphere)) return false;
        result = proc(handle, goal_type, level, influence, guid, flags,
                      object,parent_ap,child_ap,radius,aligned,sphere); break;
      }
      case AIG_MOVE_RELATIVE_OBJ_VEC: case AIG_HIDE_FROM_OBJ: {
        int a=0,b=0; if (!reader.pod(a)||!reader.pod(b)) return false;
        result = proc(handle, goal_type, level, influence, guid, flags, a,b); break;
      }
      case AIG_GUARD_AREA: case AIG_GET_TO_POS: {
        WireVector wire{}; int room=0;
        if (!reader.pod(wire)||!reader.pod(room)) return false;
        vector position{}; std::memcpy(&position, &wire, sizeof(position));
        result = proc(handle, goal_type, level, influence, guid, flags, &position, room); break;
      }
      case AIG_FACE_DIR: {
        WireVector wire{}; if (!reader.pod(wire)) return false;
        vector direction{}; std::memcpy(&direction, &wire, sizeof(direction));
        result = proc(handle, goal_type, level, influence, guid, flags, &direction); break;
      }
      case AIG_MELEE_TARGET:
        result = proc(handle, goal_type, level, influence, guid, flags); break;
      default:
        return false;
      }
      writer.pod(result);
      break;
    }
    case 30:
    case 61:
    case 62: {
      int handle = 0, item_index = 0;
      char op = 0, vtype = 0;
      uint32_t size = 0;
      if (!reader.pod(handle) || !reader.pod(op) || !reader.pod(vtype) ||
          !reader.pod(item_index) || !reader.pod(size) || size == 0 || size > 4096)
        return false;
      std::vector<uint8_t> value(size, 0);
      if (op != VF_GET && !reader.bytes(value.data(), value.size()))
        return false;
      if (index == 30)
        callback_proc<void (*)(int, char, char, void *)>(callbacks, index)(handle, op, vtype, value.data());
      else
        callback_proc<void (*)(int, char, char, void *, int)>(callbacks, index)(handle, op, vtype,
                                                                                value.data(), item_index);
      if (op == VF_GET)
        writer.bytes(value.data(), value.size());
      break;
    }
    case 100:
    case 101: {
      int value = 0;
      if (!reader.pod(value))
        return false;
      writer.pod(callback_proc<int (*)(int)>(callbacks, index)(value));
      break;
    }
    case 37: {
      int count = 0;
      uint32_t token = 0;
      void *file = nullptr;
      if (!reader.pod(count) || !reader.pod(token) || !(file = resolve_file(token)) || count < 0 ||
          count > static_cast<int>(kMaxPayload))
        return false;
      std::vector<ubyte> bytes(static_cast<size_t>(count));
      const int result = callback_proc<int (*)(ubyte *, int, void *)>(callbacks, index)(bytes.data(), count, file);
      writer.pod(result);
      if (result > 0 && result <= count)
        writer.bytes(bytes.data(), result);
      break;
    }
    case 38: {
      uint32_t token = 0;
      void *file = nullptr;
      if (!reader.pod(token) || !(file = resolve_file(token)))
        return false;
      writer.pod(callback_proc<int (*)(void *)>(callbacks, index)(file));
      break;
    }
    case 39: {
      uint32_t token = 0;
      void *file = nullptr;
      if (!reader.pod(token) || !(file = resolve_file(token)))
        return false;
      writer.pod(callback_proc<short (*)(void *)>(callbacks, index)(file));
      break;
    }
    case 40: {
      uint32_t token = 0;
      void *file = nullptr;
      if (!reader.pod(token) || !(file = resolve_file(token)))
        return false;
      writer.pod(callback_proc<sbyte (*)(void *)>(callbacks, index)(file));
      break;
    }
    case 41: {
      uint32_t token = 0;
      void *file = nullptr;
      if (!reader.pod(token) || !(file = resolve_file(token)))
        return false;
      writer.pod(callback_proc<float (*)(void *)>(callbacks, index)(file));
      break;
    }
    case 42: {
      uint32_t token = 0;
      void *file = nullptr;
      if (!reader.pod(token) || !(file = resolve_file(token)))
        return false;
      writer.pod(callback_proc<double (*)(void *)>(callbacks, index)(file));
      break;
    }
    case 43: {
      uint32_t capacity = 0, token = 0;
      void *file = nullptr;
      if (!reader.pod(capacity) || !reader.pod(token) || !(file = resolve_file(token)) ||
          capacity > kMaxPayload)
        return false;
      std::vector<char> text((std::max)(capacity, 1u), 0);
      const int result = callback_proc<int (*)(char *, size_t, void *)>(callbacks, index)(text.data(), capacity, file);
      writer.pod(result);
      writer.string(text.data());
      break;
    }
    case 44: {
      int count = 0;
      uint32_t token = 0;
      void *file = nullptr;
      if (!reader.pod(count) || !reader.pod(token) || !(file = resolve_file(token)) || count < 0 ||
          reader.remaining < static_cast<size_t>(count))
        return false;
      std::vector<ubyte> bytes(static_cast<size_t>(count));
      if (!reader.bytes(bytes.data(), bytes.size()))
        return false;
      writer.pod(callback_proc<int (*)(const ubyte *, int, void *)>(callbacks, index)(bytes.data(), count, file));
      break;
    }
    case 45: {
      uint32_t token = 0;
      std::string text;
      void *file = nullptr;
      if (!reader.pod(token) || !(file = resolve_file(token)) || !reader.string(text))
        return false;
      writer.pod(callback_proc<int (*)(const char *, void *)>(callbacks, index)(text.c_str(), file));
      break;
    }
    case 46: {
      int value = 0;
      uint32_t token = 0;
      void *file = nullptr;
      if (!reader.pod(value) || !reader.pod(token) || !(file = resolve_file(token)))
        return false;
      callback_proc<void (*)(int, void *)>(callbacks, index)(value, file);
      break;
    }
    case 47: {
      short value = 0;
      uint32_t token = 0;
      void *file = nullptr;
      if (!reader.pod(value) || !reader.pod(token) || !(file = resolve_file(token)))
        return false;
      callback_proc<void (*)(short, void *)>(callbacks, index)(value, file);
      break;
    }
    case 48: {
      sbyte value = 0;
      uint32_t token = 0;
      void *file = nullptr;
      if (!reader.pod(value) || !reader.pod(token) || !(file = resolve_file(token)))
        return false;
      callback_proc<void (*)(sbyte, void *)>(callbacks, index)(value, file);
      break;
    }
    case 49: {
      float value = 0;
      uint32_t token = 0;
      void *file = nullptr;
      if (!reader.pod(value) || !reader.pod(token) || !(file = resolve_file(token)))
        return false;
      callback_proc<void (*)(float, void *)>(callbacks, index)(value, file);
      break;
    }
    case 50: {
      double value = 0;
      uint32_t token = 0;
      void *file = nullptr;
      if (!reader.pod(value) || !reader.pod(token) || !(file = resolve_file(token)))
        return false;
      callback_proc<void (*)(double, void *)>(callbacks, index)(value, file);
      break;
    }
    case 51: {
      WireMemoryChunk wire{};
      if (!reader.pod(wire) || wire.size == 0 || wire.size > kMaxPayload)
        return false;
      tOSIRISMEMCHUNK chunk{};
      chunk.my_id.type = static_cast<script_type>(wire.script_type);
      if (chunk.my_id.type == OBJECT_SCRIPT)
        chunk.my_id.objhandle = wire.owner;
      else if (chunk.my_id.type == TRIGGER_SCRIPT)
        chunk.my_id.triggernum = wire.owner;
      chunk.id = wire.id;
      chunk.size = static_cast<int>(wire.size);
      void *pointer = callback_proc<void *(*)(tOSIRISMEMCHUNK *)>(callbacks, index)(&chunk);
      writer.pod(pointer ? memory_token(pointer, wire.size, wire) : uint32_t{0});
      break;
    }
    case 52: {
      uint32_t token = 0;
      if (!reader.pod(token))
        return false;
      const auto found = memory.find(token);
      if (found == memory.end())
        return false;
      callback_proc<void (*)(void *)>(callbacks, index)(found->second.pointer);
      memory.erase(found);
      break;
    }
    case 53: {
      int id = 0;
      if (!reader.pod(id))
        return false;
      callback_proc<void (*)(int)>(callbacks, index)(id);
      break;
    }
    case 54: {
      uint8_t bytes[24]{};
      if (!reader.bytes(bytes, sizeof(bytes)))
        return false;
      tOSIRISTIMER timer{};
      static_assert(sizeof(timer) == sizeof(bytes));
      std::memcpy(&timer, bytes, sizeof(timer));
      writer.pod(callback_proc<int (*)(tOSIRISTIMER *)>(callbacks, index)(&timer));
      break;
    }
    case 57:
    case 58:
      writer.pod(callback_proc<float (*)()>(callbacks, index)());
      break;
    case 60: {
      int id = 0;
      if (!reader.pod(id))
        return false;
      writer.pod(callback_proc<ubyte (*)(int)>(callbacks, index)(id));
      break;
    }
    case 67: {
      int flag = 0;
      ubyte value = 0;
      if (!reader.pod(flag) || !reader.pod(value))
        return false;
      callback_proc<void (*)(int, ubyte)>(callbacks, index)(flag, value);
      break;
    }
    case 68: {
      int flag = 0;
      if (!reader.pod(flag))
        return false;
      writer.pod(callback_proc<int (*)(int)>(callbacks, index)(flag));
      break;
    }
    case 71: {
      int handle = 0;
      std::string text;
      if (!reader.pod(handle) || !reader.string(text))
        return false;
      callback_proc<void (*)(int, char *)>(callbacks, index)(handle, &text[0]);
      break;
    }
    case 70: {
      int handle = 0, sound_handle = 0;
      float start = 0, end = 0, time = 0;
      char flags = 0, next_anim_type = 0;
      if (!reader.pod(handle) || !reader.pod(start) || !reader.pod(end) || !reader.pod(time) ||
          !reader.pod(flags) || !reader.pod(sound_handle) || !reader.pod(next_anim_type))
        return false;
      callback_proc<void (*)(int, float, float, float, char, int, char)>(callbacks, index)(
          handle, start, end, time, flags, sound_handle, next_anim_type);
      break;
    }
    case 75: {
      std::string filename, mode;
      if (!reader.string(filename) || !reader.string(mode))
        return false;
      void *file = callback_proc<void *(*)(const char *, const char *)>(callbacks, index)(filename.c_str(), mode.c_str());
      uint32_t token = 0;
      if (file) {
        token = next_file_token++;
        files[token] = file;
      }
      writer.pod(token);
      break;
    }
    case 76: {
      uint32_t token = 0;
      void *file = nullptr;
      if (!reader.pod(token) || token == 1 || !(file = resolve_file(token)))
        return false;
      callback_proc<void (*)(void *)>(callbacks, index)(file);
      files.erase(token);
      break;
    }
    case 77: {
      uint32_t token = 0;
      void *file = nullptr;
      if (!reader.pod(token) || !(file = resolve_file(token)))
        return false;
      writer.pod(callback_proc<int (*)(void *)>(callbacks, index)(file));
      break;
    }
    case 78: {
      uint32_t token = 0;
      void *file = nullptr;
      if (!reader.pod(token) || !(file = resolve_file(token)))
        return false;
      writer.pod(callback_proc<ubyte (*)(void *)>(callbacks, index)(file));
      break;
    }
    case 120:
      writer.pod(callback_proc<char (*)()>(callbacks, index)());
      break;
    case 121:
      writer.pod(callback_proc<int (*)()>(callbacks, index)());
      break;
    case 110: {
      WireCannedCinematicInfo wire{};
      std::string text;
      if (!reader.pod(wire) || !reader.string(text))
        return false;
      tCannedCinematicInfo info{};
      info.type = wire.type;
      info.camera_pathid = wire.camera_pathid;
      info.target_pathid = wire.target_pathid;
      info.text_to_display = text.empty() ? nullptr : &text[0];
      info.target_objhandle = wire.target_objhandle;
      info.room = wire.room;
      info.time = wire.time;
      info.object_to_use_for_point = wire.object_to_use_for_point;
      std::memcpy(&info.pos, &wire.pos, sizeof(info.pos));
      std::memcpy(&info.orient, &wire.orient, sizeof(info.orient));
      callback_proc<void (*)(tCannedCinematicInfo *)>(callbacks, index)(&info);
      break;
    }
    case 117: {
      int handle=0, killer=0, flags=0; float damage=0, min_time=0, max_time=0;
      if (!reader.pod(handle)||!reader.pod(killer)||!reader.pod(damage)||!reader.pod(flags)||
          !reader.pod(min_time)||!reader.pod(max_time))
        return false;
      callback_proc<void (*)(int,int,float,int,float,float)>(callbacks, index)(
          handle, killer, damage, flags, min_time, max_time);
      break;
    }
    default:
      bridge_log("OSIRIS bridge: callback %u reached the client without a dispatcher\n", index);
      AutomatedCaptureLog("osiris bridge missing dispatcher callback=%u", index);
      response.header.status = -2;
      return true;
    }
    response.payload = std::move(writer.data);
    return true;
  }

  bool transact(Op op, const std::vector<uint8_t> &payload, Message &response) {
    Message request;
    request.header.kind = static_cast<uint32_t>(MessageKind::Request);
    request.header.op = static_cast<uint32_t>(op);
    request.header.id = next_id++;
    request.payload = payload;
    if (!send_message(pipe, request))
      return false;
    for (;;) {
      Message incoming;
      if (!receive_message(pipe, incoming))
        return false;
      if (incoming.header.kind == static_cast<uint32_t>(MessageKind::Response) &&
          incoming.header.id == request.header.id) {
        response = std::move(incoming);
        return response.header.status == 0;
      }
      if (incoming.header.kind != static_cast<uint32_t>(MessageKind::Request) ||
          incoming.header.op != static_cast<uint32_t>(Op::Callback))
        return false;
      Message callback_response;
      callback_response.header.kind = static_cast<uint32_t>(MessageKind::Response);
      callback_response.header.op = incoming.header.op;
      callback_response.header.id = incoming.header.id;
      if (!dispatch_callback(incoming, callback_response) && callback_response.header.status == 0)
        callback_response.header.status = -1;
      if (!send_message(pipe, callback_response))
        return false;
    }
  }
};

OsirisBridgeClient::OsirisBridgeClient() : impl_(std::make_unique<Impl>()) {}
OsirisBridgeClient::~OsirisBridgeClient() = default;

std::unique_ptr<OsirisBridgeClient> OsirisBridgeClient::Start(const char *module_path) {
  auto result = std::unique_ptr<OsirisBridgeClient>(new OsirisBridgeClient);
  const uint32_t serial = g_pipe_serial.fetch_add(1);
  wchar_t pipe_name[160];
  swprintf(pipe_name, _countof(pipe_name), L"\\\\.\\pipe\\PiccuOsiris32-%lu-%u",
           GetCurrentProcessId(), serial);
  result->impl_->pipe = CreateNamedPipeW(pipe_name, PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                          PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 65536, 65536,
                                          5000, nullptr);
  if (result->impl_->pipe == INVALID_HANDLE_VALUE)
    return nullptr;

  wchar_t executable_path[MAX_PATH];
  if (!GetModuleFileNameW(nullptr, executable_path, MAX_PATH))
    return nullptr;
  std::wstring host_path(executable_path);
  const size_t separator = host_path.find_last_of(L"\\/");
  host_path.resize(separator == std::wstring::npos ? 0 : separator + 1);
  host_path += L"OsirisHost32.exe";
  if (GetFileAttributesW(host_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
    bridge_log("OSIRIS bridge: OsirisHost32.exe is missing beside PiccuEngine.exe\n");
    return nullptr;
  }

  const std::wstring module = widen(module_path);
  std::wstring command = quote(host_path) + L" --pipe " + quote(pipe_name) + L" --module " + quote(module);
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(host_path.c_str(), &command[0], nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
    return nullptr;
  result->impl_->process = process.hProcess;
  result->impl_->process_thread = process.hThread;

  const BOOL connected = ConnectNamedPipe(result->impl_->pipe, nullptr);
  if (!connected && GetLastError() != ERROR_PIPE_CONNECTED)
    return nullptr;

  Message hello;
  if (!result->impl_->transact(Op::Hello, {}, hello))
    return nullptr;
  Reader reader(hello.payload);
  uint32_t version = 0, pointer_size = 0;
  if (!reader.pod(version) || !reader.pod(pointer_size) || version != kProtocolVersion || pointer_size != 4)
    return nullptr;
  AutomatedCaptureLog("osiris bridge host connected module=%s protocol=%u", module_path, version);
  return result;
}

bool OsirisBridgeClient::Initialize(tOSIRISModuleInit *init) {
  if (!init)
    return false;
  impl_->callbacks = *init;
  Writer writer;
  writer.pod(init->module_identifier);
  // Retail Win32 modules were built against the 32-bit structure checksum.
  writer.pod(uint32_t{2273873307u});
  writer.string(init->script_identifier);
  writer.pod(static_cast<uint32_t>((std::max)(init->string_count, 0)));
  for (int i = 0; i < init->string_count; ++i)
    writer.string(init->string_table[i]);
  Message response;
  if (!impl_->transact(Op::Initialize, writer.data, response))
    return false;
  Reader reader(response.payload);
  char success = 0;
  uint8_t is_static = 0;
  if (!reader.pod(success) || !reader.pod(is_static))
    return false;
  init->module_is_static = is_static != 0;
  AutomatedCaptureLog("osiris bridge initialize result=%d static=%d module=%s", success != 0,
                      is_static != 0, init->script_identifier ? init->script_identifier : "<null>");
  return success != 0;
}

void OsirisBridgeClient::Shutdown() {
  if (!IsAlive())
    return;
  Message response;
  impl_->transact(Op::Shutdown, {}, response);
}

int OsirisBridgeClient::GetGOScriptID(const char *name, ubyte is_door) {
  Writer writer;
  writer.string(name);
  writer.pod(is_door);
  Message response;
  int result = -1;
  if (impl_->transact(Op::GetGOScriptID, writer.data, response)) {
    Reader reader(response.payload);
    reader.pod(result);
  }
  return result;
}

int OsirisBridgeClient::GetTriggerScriptID(int room, int face) {
  Writer writer;
  writer.pod(room);
  writer.pod(face);
  Message response;
  int result = -1;
  if (impl_->transact(Op::GetTriggerScriptID, writer.data, response)) {
    Reader reader(response.payload);
    reader.pod(result);
  }
  return result;
}

int OsirisBridgeClient::GetCOScriptList(int **handles, int **ids) {
  Message response;
  if (!impl_->transact(Op::GetCOScriptList, {}, response))
    return 0;
  Reader reader(response.payload);
  int count = 0;
  if (!reader.pod(count) || count < 0 || count > 100000)
    return 0;
  impl_->co_handles.resize(count);
  impl_->co_ids.resize(count);
  for (int i = 0; i < count; ++i)
    if (!reader.pod(impl_->co_handles[i]) || !reader.pod(impl_->co_ids[i]))
      return 0;
  *handles = impl_->co_handles.data();
  *ids = impl_->co_ids.data();
  return count;
}

void *OsirisBridgeClient::CreateInstance(int id) {
  Writer writer;
  writer.pod(id);
  Message response;
  uint32_t instance = 0;
  if (impl_->transact(Op::CreateInstance, writer.data, response)) {
    Reader reader(response.payload);
    reader.pod(instance);
  }
  return reinterpret_cast<void *>(static_cast<uintptr_t>(instance));
}

void OsirisBridgeClient::DestroyInstance(int id, void *instance) {
  Writer writer;
  writer.pod(id);
  writer.pod(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(instance)));
  Message response;
  impl_->transact(Op::DestroyInstance, writer.data, response);
}

short OsirisBridgeClient::CallInstanceEvent(int id, void *instance, int event, tOSIRISEventInfo *data) {
  Writer writer;
  writer.pod(id);
  writer.pod(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(instance)));
  writer.pod(event);
  WireEventInfo wire{};
  void *previous_file = impl_->active_file;
  const bool file_event = data && (event == EVT_SAVESTATE || event == EVT_RESTORESTATE);
  if (data) {
    if (file_event) {
      impl_->active_file = event == EVT_SAVESTATE ? data->evt_savestate.fileptr
                                                  : data->evt_restorestate.fileptr;
      const uint32_t file_token = 1;
      std::memcpy(wire.event_data, &file_token, sizeof(file_token));
    } else if (event == EVT_MEMRESTORE) {
      tOSIRISMEMCHUNK chunk{};
      if (!Osiris_GetMemoryInfo(data->evt_memrestore.memory_ptr, &chunk) || chunk.size <= 0)
        return 0;
      WireMemoryChunk memory_wire{};
      memory_wire.script_type = chunk.my_id.type;
      memory_wire.owner = chunk.my_id.type == OBJECT_SCRIPT ? chunk.my_id.objhandle :
                          chunk.my_id.type == TRIGGER_SCRIPT ? chunk.my_id.triggernum : 0;
      memory_wire.id = chunk.id;
      memory_wire.size = static_cast<uint32_t>(chunk.size);
      const uint32_t token = impl_->memory_token(data->evt_memrestore.memory_ptr,
                                                 memory_wire.size, memory_wire);
      std::memcpy(wire.event_data, &data->evt_memrestore.id, sizeof(data->evt_memrestore.id));
      std::memcpy(wire.event_data + sizeof(data->evt_memrestore.id), &token, sizeof(token));
    } else {
      std::memcpy(wire.event_data, data, sizeof(wire.event_data));
    }
    wire.me_handle = data->me_handle;
    const int notify_type = *reinterpret_cast<const int *>(wire.event_data);
    if (data->extra_info && event == EVT_AI_NOTIFY && notify_type == 16) {
      bridge_log("OSIRIS bridge: event %d has unsupported extra_info; event suppressed\n", event);
      AutomatedCaptureLog("osiris bridge suppressed event=%d extra_info=1", event);
      return 0;
    }
  }
  writer.pod(wire);
  impl_->write_memory_snapshot(writer);
  Message response;
  short result = 0;
  if (impl_->transact(Op::CallInstanceEvent, writer.data, response)) {
    Reader reader(response.payload);
    if (!reader.pod(result) || !impl_->apply_memory_snapshot(reader))
      result = 0;
  }
  if (file_event)
    impl_->active_file = previous_file;
  return result;
}

int OsirisBridgeClient::SaveRestoreState(void *file, ubyte saving) {
  void *previous_file = impl_->active_file;
  impl_->active_file = file;
  Writer writer;
  writer.pod(uint32_t{1});
  writer.pod(saving);
  Message response;
  int result = 0;
  if (impl_->transact(Op::SaveRestoreState, writer.data, response)) {
    Reader reader(response.payload);
    reader.pod(result);
  }
  impl_->active_file = previous_file;
  return result;
}

void OsirisBridgeClient::ForgetMemoryForScript(const tOSIRISSCRIPTID *script) {
  if (!script || impl_->memory.empty() || !IsAlive())
    return;
  std::vector<uint32_t> tokens;
  for (const auto &entry : impl_->memory) {
    const auto &chunk = entry.second.chunk;
    if (chunk.script_type != script->type)
      continue;
    if (script->type == OBJECT_SCRIPT && chunk.owner != script->objhandle)
      continue;
    if (script->type == TRIGGER_SCRIPT && chunk.owner != script->triggernum)
      continue;
    tokens.push_back(entry.first);
  }
  if (tokens.empty())
    return;
  Writer writer;
  writer.pod(static_cast<uint32_t>(tokens.size()));
  for (uint32_t token : tokens)
    writer.pod(token);
  Message response;
  impl_->transact(Op::ForgetMemory, writer.data, response);
  for (uint32_t token : tokens)
    impl_->memory.erase(token);
}

bool OsirisBridgeClient::IsAlive() const {
  return impl_ && impl_->pipe != INVALID_HANDLE_VALUE && impl_->process &&
         WaitForSingleObject(impl_->process, 0) == WAIT_TIMEOUT;
}

bool OsirisBridgeClient::IsWin32Module(const char *path) {
  const std::wstring filename = widen(path);
  HANDLE file = CreateFileW(filename.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return false;
  IMAGE_DOS_HEADER dos{};
  DWORD read = 0;
  bool result = ReadFile(file, &dos, sizeof(dos), &read, nullptr) && read == sizeof(dos) &&
                dos.e_magic == IMAGE_DOS_SIGNATURE;
  if (result) {
    SetFilePointer(file, dos.e_lfanew + sizeof(DWORD), nullptr, FILE_BEGIN);
    IMAGE_FILE_HEADER header{};
    result = ReadFile(file, &header, sizeof(header), &read, nullptr) && read == sizeof(header) &&
             header.Machine == IMAGE_FILE_MACHINE_I386;
  }
  CloseHandle(file);
  return result;
}

#else

struct OsirisBridgeClient::Impl {};
OsirisBridgeClient::OsirisBridgeClient() = default;
OsirisBridgeClient::~OsirisBridgeClient() = default;
std::unique_ptr<OsirisBridgeClient> OsirisBridgeClient::Start(const char *) { return nullptr; }
bool OsirisBridgeClient::Initialize(tOSIRISModuleInit *) { return false; }
void OsirisBridgeClient::Shutdown() {}
int OsirisBridgeClient::GetGOScriptID(const char *, ubyte) { return -1; }
int OsirisBridgeClient::GetTriggerScriptID(int, int) { return -1; }
int OsirisBridgeClient::GetCOScriptList(int **, int **) { return 0; }
void *OsirisBridgeClient::CreateInstance(int) { return nullptr; }
void OsirisBridgeClient::DestroyInstance(int, void *) {}
short OsirisBridgeClient::CallInstanceEvent(int, void *, int, tOSIRISEventInfo *) { return 0; }
int OsirisBridgeClient::SaveRestoreState(void *, ubyte) { return 0; }
void OsirisBridgeClient::ForgetMemoryForScript(const tOSIRISSCRIPTID *) {}
bool OsirisBridgeClient::IsAlive() const { return false; }
bool OsirisBridgeClient::IsWin32Module(const char *) { return false; }

#endif
