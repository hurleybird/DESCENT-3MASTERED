#pragma once

#include "protocol.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace osiris_bridge {

inline bool write_all(HANDLE pipe, const void *data, uint32_t size) {
  const auto *src = static_cast<const uint8_t *>(data);
  while (size) {
    DWORD written = 0;
    if (!WriteFile(pipe, src, size, &written, nullptr) || !written)
      return false;
    src += written;
    size -= written;
  }
  return true;
}

inline bool read_all(HANDLE pipe, void *data, uint32_t size) {
  auto *dst = static_cast<uint8_t *>(data);
  while (size) {
    DWORD received = 0;
    if (!ReadFile(pipe, dst, size, &received, nullptr) || !received)
      return false;
    dst += received;
    size -= received;
  }
  return true;
}

inline bool send_message(HANDLE pipe, const Message &message) {
  MessageHeader header = message.header;
  header.magic = kMagic;
  header.version = kProtocolVersion;
  header.payload_size = static_cast<uint32_t>(message.payload.size());
  return write_all(pipe, &header, sizeof(header)) &&
         (header.payload_size == 0 || write_all(pipe, message.payload.data(), header.payload_size));
}

inline bool receive_message(HANDLE pipe, Message &message) {
  if (!read_all(pipe, &message.header, sizeof(message.header)) || message.header.magic != kMagic ||
      message.header.version != kProtocolVersion || message.header.payload_size > kMaxPayload)
    return false;
  message.payload.resize(message.header.payload_size);
  return message.payload.empty() || read_all(pipe, message.payload.data(), message.header.payload_size);
}

} // namespace osiris_bridge
