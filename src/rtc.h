#pragma once

#include <Arduino.h>

struct RtcTime {
  uint16_t year = 2000;
  uint8_t month = 1;
  uint8_t date = 1;
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;
  uint8_t day = 1;
  bool valid = false;
};

struct RtcRawRegisters {
  uint8_t second = 0;
  uint8_t minute = 0;
  uint8_t hour = 0;
  uint8_t date = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  uint8_t year = 0;
  uint8_t control = 0;
};

void rtcBegin();
bool rtcReadTime(RtcTime &time);
RtcRawRegisters rtcReadRawRegisters();
bool rtcSetTime(const RtcTime &time);
