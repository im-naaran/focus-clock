#pragma once

#include <stddef.h>

struct JsonWriter {
  char *buffer = nullptr;
  size_t capacity = 0;
  size_t length = 0;
  bool overflow = false;
};

void jsonWriterBegin(JsonWriter &writer, char *buffer, size_t capacity);
bool jsonWriterAppend(JsonWriter &writer, const char *value);
bool jsonWriterAppendEscaped(JsonWriter &writer,
                             const char *value,
                             size_t length);
bool jsonWriteErrorEnvelope(char *buffer,
                            size_t capacity,
                            const char *code,
                            const char *message);
