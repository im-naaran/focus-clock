#include <stdio.h>
#include "portal_validation.h"

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
  uint16_t value = 99;
  expect(portalParseUnsigned("5", 1, 1, 5, value) && value == 5,
         "bounded unsigned value parses");
  expect(!portalParseUnsigned("-1", 2, 0, 10, value),
         "negative value is rejected");
  expect(!portalParseUnsigned("6", 1, 1, 5, value),
         "out of range value is rejected");
  expect(!portalParseUnsigned("1x", 2, 0, 10, value),
         "non-decimal value is rejected");

  expect(portalRtcDateTimeIsValid(2024, 2, 29, 23, 59, 59),
         "leap day is valid in leap year");
  expect(!portalRtcDateTimeIsValid(2023, 2, 29, 0, 0, 0),
         "leap day is invalid outside leap year");
  expect(!portalRtcDateTimeIsValid(2024, 4, 31, 0, 0, 0),
         "month-specific day limit is enforced");
  expect(portalWeekdayFromDate(2026, 8, 8) == 6,
         "weekday uses Monday=1 through Sunday=7");

  if (failures != 0) {
    fprintf(stderr, "%d portal validation test(s) failed\n", failures);
    return 1;
  }
  printf("portal validation tests passed\n");
  return 0;
}
