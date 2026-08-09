#pragma once

#include <stddef.h>
#include <stdint.h>

bool portalParseUnsigned(const char *value,
                         size_t length,
                         uint16_t minimum,
                         uint16_t maximum,
                         uint16_t &result);
bool portalRtcDateTimeIsValid(uint16_t year,
                              uint8_t month,
                              uint8_t date,
                              uint8_t hour,
                              uint8_t minute,
                              uint8_t second);
uint8_t portalWeekdayFromDate(uint16_t year, uint8_t month, uint8_t date);
