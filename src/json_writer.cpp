#include "json_writer.h"

#include <stdint.h>
#include <string.h>

namespace {

bool appendBytes(JsonWriter &writer, const char *value, size_t length) {
  if (writer.buffer == nullptr || writer.capacity == 0 || value == nullptr ||
      length > writer.capacity - 1 - writer.length) {
    writer.overflow = true;
    return false;
  }
  memcpy(writer.buffer + writer.length, value, length);
  writer.length += length;
  writer.buffer[writer.length] = '\0';
  return true;
}

char hexDigit(uint8_t value) {
  return value < 10 ? static_cast<char>('0' + value)
                    : static_cast<char>('A' + value - 10);
}

}  // namespace

void jsonWriterBegin(JsonWriter &writer, char *buffer, size_t capacity) {
  writer.buffer = buffer;
  writer.capacity = capacity;
  writer.length = 0;
  writer.overflow = buffer == nullptr || capacity == 0;
  if (!writer.overflow) {
    buffer[0] = '\0';
  }
}

bool jsonWriterAppend(JsonWriter &writer, const char *value) {
  return value != nullptr && appendBytes(writer, value, strlen(value));
}

bool jsonWriterAppendEscaped(JsonWriter &writer,
                             const char *value,
                             size_t length) {
  if (value == nullptr) {
    writer.overflow = true;
    return false;
  }
  for (size_t index = 0; index < length; ++index) {
    const uint8_t byte = static_cast<uint8_t>(value[index]);
    if (byte == '"' || byte == '\\') {
      const char escaped[] = {'\\', static_cast<char>(byte)};
      if (!appendBytes(writer, escaped, sizeof(escaped))) {
        return false;
      }
    } else if (byte == '\b') {
      if (!appendBytes(writer, "\\b", 2)) return false;
    } else if (byte == '\f') {
      if (!appendBytes(writer, "\\f", 2)) return false;
    } else if (byte == '\n') {
      if (!appendBytes(writer, "\\n", 2)) return false;
    } else if (byte == '\r') {
      if (!appendBytes(writer, "\\r", 2)) return false;
    } else if (byte == '\t') {
      if (!appendBytes(writer, "\\t", 2)) return false;
    } else if (byte < 0x20) {
      const char unicode[] = {'\\', 'u', '0', '0', hexDigit(byte >> 4),
                              hexDigit(byte & 0x0F)};
      if (!appendBytes(writer, unicode, sizeof(unicode))) {
        return false;
      }
    } else if (!appendBytes(writer, reinterpret_cast<const char *>(&byte), 1)) {
      return false;
    }
  }
  return true;
}

bool jsonWriteErrorEnvelope(char *buffer,
                            size_t capacity,
                            const char *code,
                            const char *message) {
  JsonWriter writer;
  jsonWriterBegin(writer, buffer, capacity);
  return jsonWriterAppend(writer, "{\"ok\":false,\"error\":{\"code\":\"") &&
         jsonWriterAppendEscaped(writer, code, strlen(code)) &&
         jsonWriterAppend(writer, "\",\"message\":\"") &&
         jsonWriterAppendEscaped(writer, message, strlen(message)) &&
         jsonWriterAppend(writer, "\"}}");
}
