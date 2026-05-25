#pragma once

#include <Arduino.h>

// DS1302 的星期字段为 1..7；本项目约定 1=周一，7=周日。
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

// 初始化 DS1302 使用的 GPIO 和默认电平。
void rtcBegin();

// 读取并校验 DS1302 当前时间；时钟停止或字段超出正常范围时返回 false。
bool rtcReadTime(RtcTime &time);
