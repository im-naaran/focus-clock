#include "rtc.h"

#include "config.h"

using namespace AppConfig;

static void ds1302WriteByte(uint8_t value) {
  pinMode(PIN_DS1302_IO, OUTPUT);
  for (uint8_t i = 0; i < 8; ++i) {
    digitalWrite(PIN_DS1302_IO, value & 0x01);
    digitalWrite(PIN_DS1302_SCLK, HIGH);
    delayMicroseconds(2);
    digitalWrite(PIN_DS1302_SCLK, LOW);
    delayMicroseconds(2);
    value >>= 1;
  }
}

static uint8_t ds1302ReadByte() {
  uint8_t value = 0;
  pinMode(PIN_DS1302_IO, INPUT);
  for (uint8_t i = 0; i < 8; ++i) {
    if (digitalRead(PIN_DS1302_IO)) {
      value |= (1 << i);
    }
    digitalWrite(PIN_DS1302_SCLK, HIGH);
    delayMicroseconds(2);
    digitalWrite(PIN_DS1302_SCLK, LOW);
    delayMicroseconds(2);
  }
  return value;
}

static uint8_t fromBcd(uint8_t value) {
  return ((value >> 4) * 10) + (value & 0x0F);
}

static uint8_t ds1302ReadRegister(uint8_t reg) {
  digitalWrite(PIN_DS1302_CE, LOW);
  digitalWrite(PIN_DS1302_SCLK, LOW);
  digitalWrite(PIN_DS1302_CE, HIGH);
  delayMicroseconds(4);
  ds1302WriteByte(0x81 | ((reg & 0x1F) << 1));
  const uint8_t value = ds1302ReadByte();
  digitalWrite(PIN_DS1302_CE, LOW);
  delayMicroseconds(4);
  return value;
}

void rtcBegin() {
  pinMode(PIN_DS1302_CE, OUTPUT);
  pinMode(PIN_DS1302_SCLK, OUTPUT);
  pinMode(PIN_DS1302_IO, OUTPUT);
  digitalWrite(PIN_DS1302_CE, LOW);
  digitalWrite(PIN_DS1302_SCLK, LOW);
}

bool rtcReadTime(RtcTime &time) {
  const uint8_t rawSecond = ds1302ReadRegister(0x00);
  const uint8_t rawMinute = ds1302ReadRegister(0x01);
  const uint8_t rawHour = ds1302ReadRegister(0x02);
  const uint8_t rawDate = ds1302ReadRegister(0x03);
  const uint8_t rawMonth = ds1302ReadRegister(0x04);
  const uint8_t rawDay = ds1302ReadRegister(0x05);
  const uint8_t rawYear = ds1302ReadRegister(0x06);

  time.second = fromBcd(rawSecond & 0x7F);
  time.minute = fromBcd(rawMinute & 0x7F);
  time.hour = fromBcd(rawHour & 0x3F);
  time.date = fromBcd(rawDate & 0x3F);
  time.month = fromBcd(rawMonth & 0x1F);
  time.day = fromBcd(rawDay & 0x07);
  time.year = 2000 + fromBcd(rawYear);

  time.valid = (rawSecond & 0x80) == 0 &&
               time.second < 60 &&
               time.minute < 60 &&
               time.hour < 24 &&
               time.date >= 1 && time.date <= 31 &&
               time.month >= 1 && time.month <= 12 &&
               time.day >= 1 && time.day <= 7;
  return time.valid;
}
