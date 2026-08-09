#include "portal_validation.h"

namespace {

bool leapYear(uint16_t year) {
  return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

uint8_t daysInMonth(uint16_t year, uint8_t month) {
  static const uint8_t days[] = {31, 28, 31, 30, 31, 30,
                                 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) {
    return 0;
  }
  return month == 2 && leapYear(year) ? 29 : days[month - 1];
}

}  // namespace

bool portalParseUnsigned(const char *value,
                         size_t length,
                         uint16_t minimum,
                         uint16_t maximum,
                         uint16_t &result) {
  if (value == nullptr || length == 0) {
    return false;
  }
  uint32_t parsed = 0;
  for (size_t index = 0; index < length; ++index) {
    if (value[index] < '0' || value[index] > '9') {
      return false;
    }
    parsed = parsed * 10 + static_cast<uint8_t>(value[index] - '0');
    if (parsed > maximum) {
      return false;
    }
  }
  if (parsed < minimum) {
    return false;
  }
  result = static_cast<uint16_t>(parsed);
  return true;
}

bool portalRtcDateTimeIsValid(uint16_t year,
                              uint8_t month,
                              uint8_t date,
                              uint8_t hour,
                              uint8_t minute,
                              uint8_t second) {
  return year >= 2000 && year <= 2099 && month >= 1 && month <= 12 &&
         date >= 1 && date <= daysInMonth(year, month) && hour < 24 &&
         minute < 60 && second < 60;
}

uint8_t portalWeekdayFromDate(uint16_t year, uint8_t month, uint8_t date) {
  static const uint8_t monthOffsets[] = {0, 3, 2, 5, 0, 3,
                                         5, 1, 4, 6, 2, 4};
  uint16_t adjustedYear = year;
  if (month < 3) {
    --adjustedYear;
  }
  const uint8_t sundayBased = static_cast<uint8_t>(
      (adjustedYear + adjustedYear / 4 - adjustedYear / 100 +
       adjustedYear / 400 + monthOffsets[month - 1] + date) % 7);
  return sundayBased == 0 ? 7 : sundayBased;
}
