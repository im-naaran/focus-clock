#include <stdio.h>
#include <string.h>

#include "json_writer.h"

namespace {
int failures = 0;
void expect(bool condition, const char *message) {
  if (!condition) {
    ++failures;
    fprintf(stderr, "FAIL: %s\n", message);
  }
}
}  // namespace

int main() {
  char buffer[256];
  JsonWriter writer;
  jsonWriterBegin(writer, buffer, sizeof(buffer));
  const char value[] = {'a', '"', '\\', '\n', '\x01',
                        static_cast<char>(0xE6), static_cast<char>(0x97),
                        static_cast<char>(0xB6)};
  expect(jsonWriterAppendEscaped(writer, value, sizeof(value)),
         "escaped value fits in output");
  expect(strcmp(buffer, "a\\\"\\\\\\n\\u0001\xE6\x97\xB6") == 0,
         "quotes, slash, controls and UTF-8 are encoded correctly");

  expect(jsonWriteErrorEnvelope(buffer, sizeof(buffer), "BAD_\"CODE", "line\none"),
         "error envelope fits");
  expect(strcmp(buffer,
                "{\"ok\":false,\"error\":{\"code\":\"BAD_\\\"CODE\",\"message\":\"line\\none\"}}") == 0,
         "error envelope escapes dynamic strings");

  char tiny[4];
  jsonWriterBegin(writer, tiny, sizeof(tiny));
  expect(!jsonWriterAppend(writer, "long") && writer.overflow,
         "writer reports fixed-buffer overflow");

  if (failures != 0) {
    fprintf(stderr, "%d json writer test(s) failed\n", failures);
    return 1;
  }
  printf("json writer tests passed\n");
  return 0;
}
